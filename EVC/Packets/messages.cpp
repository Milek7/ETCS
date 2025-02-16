/*
 * European Train Control System
 * Copyright (C) 2019-2023  César Benito <cesarbema2009@hotmail.com>
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "messages.h"
#include "information.h"
#include "radio.h"
#include "logging.h"
#include "vbc.h"
#include "0.h"
#include "12.h"
#include "21.h"
#include "27.h"
#include "41.h"
#include "42.h"
#include "65.h"
#include "131.h"
#include "136.h"
#include "180.h"
#include "../Supervision/emergency_stop.h"
#include "../Procedures/mode_transition.h"
#include "../Procedures/level_transition.h"
#include "../Procedures/override.h"
#include "../Procedures/start.h"
#include "../Procedures/train_trip.h"
#include "../Position/geographical.h"
#include "../TrainSubsystems/brake.h"
#include "../TrainSubsystems/train_interface.h"
#include "../DMI/track_ahead_free.h"
#include "../DMI/windows.h"
#include "../Version/version.h"
#include "../Version/translate.h"
#include <algorithm>
#include <iostream>
static int reading_nid_bg=-1;
static int reading_nid_c=-1;
static std::vector<eurobalise_telegram> telegrams;
static distance last_passed_distance;
static bool reffound=false;
static bool refmissed=false;
static bool dupfound=false;
static bool refpassed=false;
static bool linked = false;
static int prevpig=-1;
static int totalbg=8;
static bool reading = false;
static int rams_lost_count = 0;
static int dir = -1;
static int orientation = -1;
static int64_t first_balise_time;
static distance bg_reference1;
static distance bg_reference1max;
static distance bg_reference1min;
static distance bg_reference;
static distance bg_referencemax;
static distance bg_referencemin;
static bool stop_checking_linking=false;
std::deque<std::pair<eurobalise_telegram, std::pair<distance,int64_t>>> pending_telegrams;
optional<link_data> rams_reposition_mitigation;
void trigger_reaction(int reaction);
void handle_telegrams(std::vector<eurobalise_telegram> message, distance dist, int dir, int64_t timestamp, bg_id nid_bg, int m_version);
void handle_radio_message(std::shared_ptr<euroradio_message> message);
void check_valid_data(std::vector<eurobalise_telegram> telegrams, distance bg_reference, bool linked, int64_t timestamp);
void update_track_comm();
void balise_group_passed();
void check_linking(bool group_passed=false);
void expect_next_linking();
std::vector<etcs_information*> construct_information(ETCS_packet *packet, euroradio_message *msg);
void trigger_reaction(int reaction)
{
    switch (reaction) {
        case 1:
            if (mode != Mode::SL && mode != Mode::PT && mode != Mode::NL && mode != Mode::RV && mode != Mode::PS)
                trigger_brake_reason(0);
            break;
        case 2:
            //No reaction
            break;
        default:
            //Train Trip - Balise read error - Linking"
            trigger_condition(17);
            break;
    }
}
void reset_eurobalise_data()
{
    telegrams.clear();
    reading_nid_bg = -1;
    reading_nid_c=-1;
    prevpig = -1;
    totalbg = 8;
    reading = false;
    dir = -1;
    orientation = -1;
    refmissed = false;
    reffound = false;
    dupfound = false;
    refpassed = false;
    linked = false;
    stop_checking_linking = false;
}
void expect_next_linking()
{
    if (link_expected != linking.end())
        ++link_expected;
}
void check_linking(bool group_passed)
{
    if (mode != Mode::FS && mode != Mode::OS && mode != Mode::LS) {
        rams_lost_count = 0;
        rams_reposition_mitigation = {};
    }
    if (rams_reposition_mitigation && rams_reposition_mitigation->max() < d_minsafefront(rams_reposition_mitigation->max())-L_antenna_front)
        rams_reposition_mitigation = {};
    if (link_expected == linking.end())
        rams_lost_count = 0;
    if (link_expected!=linking.end() && !stop_checking_linking) {
        auto link_bg=linking.end();
        for (auto it = link_expected; it!=linking.end(); ++it) {
            if (it->nid_bg == bg_id({reading_nid_c, reading_nid_bg}) || (it->nid_bg.NID_C == reading_nid_c && it->nid_bg.NID_BG == 16383)) {
                link_bg = it;
                break;
            }
        }
        bool isexpected = linked && refpassed && link_expected==link_bg;
        bool c1 = isexpected && (link_expected->min() > bg_referencemax || link_expected->nid_bg.NID_BG == 16383);
        bool c2 = (!isexpected || link_expected->max() < bg_referencemin) && link_expected->max() < d_minsafefront(odometer_orientation, 0)-L_antenna_front;
        bool c3 = linked && link_bg!=linking.end() && link_bg != link_expected;
        if (c1 || c2 || c3) {
            // TODO: If check_linking() were not called in other modes, what would happen to link_expected when entering FS,OS,LS?
            if (mode == Mode::FS || mode == Mode::OS || mode == Mode::LS) {
                std::cout<<"Balise read error: check_linking() c1="<<c1<<" c2="<<c2<<" c3="<<c3<<std::endl;
            
                if (c2 || c3)
                    rams_lost_count++;
                if (rams_lost_count > 1 && link_expected->reaction == 2 && (c2 || c3)) {
                    trigger_reaction(1);
                    rams_lost_count = 0;
                    std::cout<<"RAMS supervision"<<std::endl;
                } else {
                    trigger_reaction(link_expected->reaction);
                }
                if (rams_lost_count > 1)
                    rams_lost_count = 0;
            }
            expect_next_linking();
            if (c3)
                check_linking();
            else if (c1)
                stop_checking_linking = true;
        } else if (linked && link_expected==link_bg && (refpassed || group_passed)) {
            stop_checking_linking = true;
            if (refpassed && bg_referencemax>=link_expected->min() && bg_referencemin<=link_expected->max())
                rams_lost_count = 0;
            expect_next_linking();
        }
    }
}
void balise_group_passed()
{
    if (!reffound) {
        refmissed = true;
        if (dupfound) {
            bg_reference = bg_reference1;
            bg_referencemax = bg_reference1max;
            bg_referencemin = bg_reference1min;
            refpassed = true;
        }
    }
    check_linking(true);
    bool linking_rejected=false;
    if (linked && reffound && !linking.empty() && (mode == Mode::FS || mode == Mode::OS || mode == Mode::LS)) {
        linking_rejected = true;
        for (auto it = linking.begin(); it != linking.end(); ++it) {
            link_data &l = *it;
            if (l.nid_bg == bg_id({reading_nid_c, reading_nid_bg})) {
                if (dir != -1 && dir != l.reverse_dir) {
                    std::cout<<"Balise error: group passed in wrong direction. Expected "<<l.reverse_dir<<", passed "<<dir<<std::endl;
                    trigger_condition(66);
                }
                rams_reposition_mitigation = {};
                if (l.max() >= bg_referencemin && l.min() <= bg_referencemax)
                    linking_rejected = false;
                break;
            }
            if (l.nid_bg.NID_C == reading_nid_c && l.nid_bg.NID_BG == 16383) {
                bool repositioning = false;
                for (auto tel : telegrams) {
                    for (auto pack : tel.packets) {
                        if (pack->NID_PACKET == 16) {
                            int Q_DIR = ((ETCS_directional_packet*)pack.get())->Q_DIR;
                            if (Q_DIR == Q_DIR_t::Both || (Q_DIR == Q_DIR_t::Nominal && dir == 0) || (Q_DIR == Q_DIR_t::Reverse && dir == 1)) {
                                repositioning = true;
                                break;
                            }
                        }
                    }
                    if (repositioning)
                        break;
                }
                if (dir != -1 && dir == l.reverse_dir && repositioning && l.max() >= bg_referencemin) {
                    if (it != linking.begin()) {
                        auto it2 = it;
                        --it2;
                        if (it2->dist <= bg_referencemax)
                            linking_rejected = false;
                    } else {
                        linking_rejected = false;
                    }
                }
                break;
            }
        }
        if (rams_reposition_mitigation) {
            bool repositioning = false;
            for (auto tel : telegrams) {
                for (auto pack : tel.packets) {
                    if (pack->NID_PACKET == 16) {
                        int Q_DIR = ((ETCS_directional_packet*)pack.get())->Q_DIR;
                        if (Q_DIR == Q_DIR_t::Both || (Q_DIR == Q_DIR_t::Nominal && dir == 0) || (Q_DIR == Q_DIR_t::Reverse && dir == 1)) {
                            repositioning = true;
                            break;
                        }
                    }
                }
                if (repositioning)
                    break;
            }
            if (dir != -1 && dir == rams_reposition_mitigation->reverse_dir && repositioning && rams_reposition_mitigation->max() >= bg_referencemin) {
                linking_rejected = true;
                rams_reposition_mitigation = {};
                trigger_reaction(1);
            }
        }
    }
    if (!linking_rejected) check_valid_data(telegrams, bg_reference, linked, first_balise_time);
    linking.erase(linking.begin(), link_expected);
    reset_eurobalise_data();
}
void update_track_comm()
{
    update_radio();
    if (pending_telegrams.empty()) {
        check_linking();
        if (reading) {
            double elapsed = d_estfront_dir[odometer_orientation == -1]-L_antenna_front-last_passed_distance;
            /*if (prevpig==-1) {
                if (elapsed > 12*8)
                    balise_group_passed();
            } else {
                int v1 = (totalbg-prevpig-1);
                int v2 = prevpig;
                if (dir == 0 && elapsed > v1*12)
                    balise_group_passed();
                else if (dir == 1 && elapsed > v2*12)
                    balise_group_passed();
                else if (dir == -1 && elapsed > std::max(v1, v2)*12)
                    balise_group_passed();
            }*/
            if (elapsed > 12)
                balise_group_passed();
        }
    } else {
        eurobalise_telegram t = pending_telegrams.front().first;
        distance passed_dist = pending_telegrams.front().second.first-L_antenna_front;
        log_message(std::shared_ptr<ETCS_message>(new eurobalise_telegram(t)), pending_telegrams.front().second.first, pending_telegrams.front().second.second);
        pending_telegrams.pop_front();
        extern optional<float> rmp_position;
        int rev = ((mode == Mode::PT || mode == Mode::RV) ? -1 : 1)*odometer_orientation;
        if (rmp_position && (*rmp_position - odometer_value)*rev > 0.1) {
            update_track_comm();
            return;
        }
        distance prev_distance = last_passed_distance;
        last_passed_distance = passed_dist;
        reading = true;
        if (!t.readerror) {
            linked = t.Q_LINK == Q_LINK_t::Linked;
            if (reading_nid_bg != -1 && reading_nid_bg != t.NID_BG)
                balise_group_passed();
            if (reading_nid_bg != t.NID_BG) {
                first_balise_time = get_milliseconds();
            }
            reading_nid_bg = t.NID_BG;
            reading_nid_c = t.NID_C;
            totalbg = t.N_TOTAL+1;
            if (prevpig != -1) {
                if (dir == -1)
                    dir = (prevpig>t.N_PIG) ? 1 : 0;
                if (orientation == -1)
                    orientation = (passed_dist<prev_distance) ? 1 : 0;
                if (t.N_PIG>prevpig && !reffound)
                    refmissed = true;
            }
            prevpig = t.N_PIG;
            if (t.N_PIG == 1 && t.M_DUP == M_DUP_t::DuplicateOfPrev) {
                dupfound = true;
                bg_reference1 = passed_dist;
                bg_reference1max = d_maxsafe(passed_dist);
                bg_reference1min = d_minsafe(passed_dist);
            }
            if (t.N_PIG == 0) {
                reffound = true;
                bg_reference = passed_dist;
                bg_referencemax = d_maxsafe(passed_dist);
                bg_referencemin = d_minsafe(passed_dist);
                refpassed = true;
                check_linking();
            }
            if ((dir==0 && t.N_PIG == t.N_TOTAL) || (dir == 1 && t.N_PIG == 0)) {
                telegrams.push_back(t);
                balise_group_passed();
                return;
            }
        }
        telegrams.push_back(t);
        if (refmissed && dupfound) {
            bg_reference = bg_reference1;
            bg_referencemax = bg_reference1max;
            bg_referencemin = bg_reference1min;
            refpassed = true;
            check_linking();
        }
        update_track_comm();
    }
}
void check_valid_data(std::vector<eurobalise_telegram> telegrams, distance bg_reference, bool linked, int64_t timestamp)
{
    int nid_bg=-1;
    int nid_c=-1;
    int m_version=-1;
    int dir=-1;
    int prevno=-1;
    int n_total=-1;
    std::vector<eurobalise_telegram> read_telegrams;
    for (int i=0; i<telegrams.size(); i++) {
        eurobalise_telegram t = telegrams[i];
        if (!t.readerror) {
            m_version = t.M_VERSION;
            nid_bg = t.NID_BG;
            nid_c = t.NID_C;
            if (prevno == -1)
                prevno = t.N_PIG;
            else
                dir = t.N_PIG<prevno;
            n_total = t.N_TOTAL;
            read_telegrams.push_back(t);
        }
    }
    if (VERSION_X(m_version) == 0)
        return;
    bool higherver = true;
    for (int ver : supported_versions) {
        if (VERSION_X(ver) >= VERSION_X(m_version))
            higherver = false;
    }
    if (higherver) {
        trigger_condition(65);
        return;
    }
    if (sh_balises && sh_balises->find({nid_c, nid_bg}) == sh_balises->end() && !overrideProcedure)
        trigger_condition(52);
    if (dir == 1)
        std::reverse(read_telegrams.begin(), read_telegrams.end());

    if (orientation == -1) {
        if (odometer_direction == -odometer_orientation)
            orientation = 1;
        else if (odometer_direction == odometer_orientation)
            orientation = 0;
    }
    if (orientation == 1) {
        if (mode == Mode::SH || mode == Mode::PS || mode == Mode::SL) {
            bg_reference = distance(bg_reference.get(), -odometer_orientation, 0);
        } else {
            if (dir != -1)
                dir = 1-dir;
        }
    }

    bool accepted1 = true;
    if (read_telegrams.size() == 0)
        accepted1 = false;

    link_data balise_link;
    bool containedinlinking=false;
    if (linked && !linking.empty()) {
        bool unknown = false;
        for (link_data l : linking) {
            if (l.nid_bg.NID_C == nid_c && l.nid_bg.NID_BG == 16383)
                unknown = true;
            if (l.nid_bg == bg_id({nid_c, nid_bg})) {
                containedinlinking = true;
                balise_link = l;
            }
        }
        if (!containedinlinking && !unknown && (mode == Mode::FS || mode == Mode::OS || mode == Mode::LS))
            return;
    }

    if (n_total == -1) {
        accepted1 = false;
    } else {
        for (int pig=0; pig<=n_total; pig++) {
            bool reject=true;
            for (int i=0; i<read_telegrams.size() && reject; i++) {
                eurobalise_telegram t = read_telegrams[i];
                if (t.N_PIG == pig) {
                    reject = false;
                } else if ((t.M_DUP == M_DUP_t::DuplicateOfNext && t.N_PIG+1==pig) || (t.M_DUP == M_DUP_t::DuplicateOfPrev && t.N_PIG==pig+1)) {
                    if (containedinlinking && (mode == Mode::FS || mode == Mode::OS || mode == Mode::LS)) {
                        reject = false;
                    } else {
                        if (dir!=-1) {
                            reject = false;
                        } else {
                            bool directional = false;
                            for (int j=0; j<t.packets.size(); j++) {
                                ETCS_packet *p = t.packets[i].get();
                                if (p->directional && ((ETCS_directional_packet*)p)->Q_DIR != Q_DIR_t::Both)
                                    directional = true;
                            }
                            if (!directional)
                                reject = false;
                        }
                    }
                    break;
                }
            }
            if (reject)
                accepted1 = false;
        }
    }

    std::vector<eurobalise_telegram> message;
    for (int i=0; i<read_telegrams.size(); i++) {
        eurobalise_telegram t = read_telegrams[i];
        bool c1 = t.M_DUP == M_DUP_t::NoDuplicates;
        bool c2 = t.M_DUP == M_DUP_t::DuplicateOfNext && i+1<read_telegrams.size() && t.N_PIG+1==read_telegrams[i+1].N_PIG;
        bool c3 = t.M_DUP == M_DUP_t::DuplicateOfPrev && i>1 && t.N_PIG==read_telegrams[i-1].N_PIG+1;
        bool ignored = false;
        for (auto p : t.packets) {
            if (p->NID_PACKET == 0 || p->NID_PACKET == 200) {
                auto *vbc = (VirtualBaliseCoverMarker*)p.get();
                if (vbc_ignored(nid_c, vbc->NID_VBCMK))
                    ignored = true;
            } else {
                break;
            }
        }
        if (ignored)
            continue;
        if (c1 || !(c2||c3))
            message.push_back(t);
        if ((c2 && dir==0) || (c3 && dir==1)) {
            eurobalise_telegram first = t;
            eurobalise_telegram second = c2 ? read_telegrams[i+1] : read_telegrams[i-1];
            bool seconddefault = false;
            for (int j=0; j<second.packets.size(); j++) {
                if (second.packets[j]->NID_PACKET == 254) {
                    seconddefault = true;
                    break;
                }
            }
            message.push_back(seconddefault ? first : second);
        }
    }
    std::vector<std::shared_ptr<ETCS_packet>> packets;
    for (auto &t : message) {
        packets.insert(packets.end(), t.packets.begin(), t.packets.end());
    }
    for (auto &t : message) {
        auto tpacks = t.packets;
        t.packets.clear();
        for (auto &p : tpacks) {
            auto p2 = translate_packet(p, packets, m_version);
            if (p2 != nullptr)
                t.packets.push_back(p2);
        }
    }
    bool accepted2=true;
    int mcount=-1;
    for (int i=0; i<message.size(); i++) {
        if (!message[i].valid)
            accepted2 = false;
        if (message[i].M_MCOUNT==M_MCOUNT_t::NeverFitsTelegrams) {
            accepted2 = false;
        } else if(message[i].M_MCOUNT!=M_MCOUNT_t::FitsAllTelegrams) {
            if (mcount==-1)
                mcount = message[i].M_MCOUNT;
            else if (mcount != message[i].M_MCOUNT)
                accepted2 = false;
        }
    }
    bool accepted = accepted1 && accepted2;
    if (!accepted) {
        if (containedinlinking && (mode == Mode::FS || mode == Mode::OS || mode == Mode::LS)) {
            std::cout<<"Balise error. Linked BG not accepted. accepted1="<<accepted1<<", accepted2="<<accepted2<<std::endl;
            trigger_reaction(balise_link.reaction);
        } else {
            if (accepted2) {
                for (int i=0; i<message.size(); i++) {
                    eurobalise_telegram t = message[i];
                    if (t.packets.empty())
                        continue;
                    for (int j=0; j<t.packets.size()-1; j++) {
                        if (t.packets[j]->NID_PACKET == 145) {
                            auto &Q_DIR = ((ETCS_directional_packet*)t.packets[j].get())->Q_DIR;
                            if (Q_DIR == Q_DIR_t::Both || (Q_DIR == Q_DIR_t::Nominal && dir == 0) || (Q_DIR == Q_DIR_t::Reverse && dir == 1))
                                return;
                        }
                    }
                }
            }
            std::cout<<"Balise error. Telegram not accepted. accepted1="<<accepted1<<", accepted2="<<accepted2<<std::endl;
            trigger_reaction(1);
        }
        return;
    }
    
    if (dir == -1 && containedinlinking && (mode == Mode::FS || mode == Mode::OS || mode == Mode::LS))
        dir = balise_link.reverse_dir;
    if (dir == -1 && orientation == -1)
        bg_reference = distance(bg_reference.get(), 0, 0);
    bg_reference = update_location_reference({nid_c, nid_bg}, dir, bg_reference, linked, containedinlinking ? balise_link : optional<link_data>());

    handle_telegrams(message, bg_reference, dir, timestamp, {nid_c, nid_bg}, m_version);
    if (dir != -1)
        geographical_position_handle_bg_passed({nid_c, nid_bg}, bg_reference, dir == 1);
}
bool info_compare(const std::shared_ptr<etcs_information> &i1, const std::shared_ptr<etcs_information> &i2)
{
    if (!i1->infill && i2->infill)
        return true;
    if ((i1->index_level == 8 || i1->index_level == 9) && (i2->index_level != 8 && i2->index_level != 9))
        return true;
    if (i1->index_level == 1 && i2->index_level != 1)
        return true;
    if (i2->index_level == 3 && i1->index_level != 3)
        return true;
    return false;
}
void handle_telegrams(std::vector<eurobalise_telegram> message, distance dist, int dir, int64_t timestamp, bg_id nid_bg, int m_version)
{
    if (!ongoing_transition) {
        transition_buffer.clear();
    } else {
        if (transition_buffer.size() == 3)
            transition_buffer.pop_front();
        transition_buffer.push_back({});
    }
    if (NV_NID_Cs.find(nid_bg.NID_C) == NV_NID_Cs.end()) {
        reset_national_values();
        operate_version(m_version, false);
    }
    if (VERSION_X(m_version) > VERSION_X(operated_version))
        operate_version(m_version, false);
    std::set<virtual_balise_cover> old_vbcs = vbcs;
    for (auto it = old_vbcs.begin(); it != old_vbcs.end(); ++it) {
        if (it->NID_C != nid_bg.NID_C)
            remove_vbc(*it);
    }

    std::list<std::shared_ptr<etcs_information>> ordered_info;
    for (int i=0; i<message.size(); i++) {
        eurobalise_telegram t = message[i];
        distance ref = dist;
        bool infill=false;
        for (int j=0; j<t.packets.size(); j++) {
            ETCS_packet *p = t.packets[j].get();
            if (p->directional) {
                auto *dp = (ETCS_directional_packet*)p;
                if (dir == -1 || (dp->Q_DIR == Q_DIR_t::Nominal && dir == 1) || (dp->Q_DIR == Q_DIR_t::Reverse && dir == 0))
                    continue;
            }
            if (p->NID_PACKET == 136) {
                if ((level != Level::N1 && (!ongoing_transition || ongoing_transition->leveldata.level != Level::N1 || (level != Level::N2 && level != Level::N3))) || (mode != Mode::FS && mode != Mode::OS))
                    break;

                ordered_info.sort(info_compare);
                for (auto it = ordered_info.begin(); it!=ordered_info.end(); ++it)
                {
                    try_handle_information(*it, ordered_info);
                }
                ordered_info.clear();

                InfillLocationReference ilr = *((InfillLocationReference*)p);
                bool found = false;
                for (auto it = link_expected; it!=linking.end(); ++it) {
                    link_data &l = *it;
                    if (l.nid_bg == bg_id({ilr.Q_NEWCOUNTRY == Q_NEWCOUNTRY_t::SameCountry ? nid_bg.NID_C : ilr.NID_C, (int)ilr.NID_BG})) {
                        found = true;
                        infill = true;
                        ref = l.dist;
                        break;
                    }
                }
                if (!found)
                    return;
            } else if (p->NID_PACKET == 80 || p->NID_PACKET == 49) {
                for (auto it = ordered_info.rbegin(); it!=ordered_info.rend(); ++it) {
                    if (it->get()->index_level == 3 || it->get()->index_level == 39) {
                        it->get()->linked_packets.push_back(t.packets[j]);
                        break;
                    }
                }
            }
            std::vector<etcs_information*> info = construct_information(p, nullptr);
            for (int i=0; i<info.size(); i++) {
                info[i]->linked_packets.push_back(t.packets[j]);
                info[i]->ref = ref;
                info[i]->infill = infill;
                info[i]->dir = dir;
                info[i]->fromRBC = nullptr;
                info[i]->timestamp = timestamp;
                info[i]->nid_bg = nid_bg;
                info[i]->version = m_version;
                ordered_info.push_back(std::shared_ptr<etcs_information>(info[i]));
            }
        }
    }
    ordered_info.sort(info_compare);
    for (auto it = ordered_info.begin(); it!=ordered_info.end(); ++it)
    {
        try_handle_information(*it, ordered_info);
    }
}
void handle_radio_message(std::shared_ptr<euroradio_message> message, communication_session *session)
{
    if (!ongoing_transition) {
        transition_buffer.clear();
    } else {
        if (transition_buffer.size() == 3)
            transition_buffer.pop_front();
        transition_buffer.push_back({});
    }
    message = translate_message(message, session->version);
    std::list<std::shared_ptr<etcs_information>> ordered_info;
    bg_id lrbg = message->NID_LRBG.get_value();
    distance ref = distance(0, odometer_orientation, 0);
    bool valid_lrbg = false;
    int dir = -1;
    for (lrbg_info info : lrbgs) {
        if (info.nid_lrbg == lrbg) {
            valid_lrbg = true;
            ref = info.position;
            std::cout<<"Ref: "<<ref.get()<<std::endl;
            dir = info.dir;
            break;
        }
    }
    if (!valid_lrbg && message->NID_LRBG!=NID_LRBG_t::Unknown)
        return;
    switch (message->NID_MESSAGE) {
        case 15: {
            auto *emerg = (conditional_emergency_stop*)message.get();
            ref += emerg->D_REF.get_value(emerg->Q_SCALE);
            break;
        }
        case 33: {
            auto *ma = (MA_shifted_message*)message.get();
            ref += ma->D_REF.get_value(ma->Q_SCALE);
            break;
        }
        case 34: {
            auto *taf = (taf_request_message*)message.get();
            ref += taf->D_REF.get_value(taf->Q_SCALE);
            break;
        }
        default:
            break;
    }
    {
        etcs_information* info = nullptr;
        switch (message->NID_MESSAGE) {
            case 2:
                info = new SR_authorisation_info();
                break;
            case 6:
                info = new etcs_information(37, 39, []() {
                    trip_exit_acknowledged = true;
                });
                break;
            case 8:
                info = new etcs_information(38, 40, [session]() {
                    session->train_data_ack_pending = false;
                });
                break;
            case 15:{
                auto *emerg = (conditional_emergency_stop*)message.get();
                if (!((emerg->Q_DIR == Q_DIR_t::Nominal && dir == 1) && (emerg->Q_DIR == Q_DIR_t::Reverse && dir == 0))) {
                    info = new etcs_information(41, 43, [emerg,ref]() {
                        int result = handle_conditional_emergency_stop(emerg->NID_EM, ref+emerg->D_EMERGENCYSTOP.get_value(emerg->Q_SCALE));
                        emergency_acknowledgement_message *ack = new emergency_acknowledgement_message();
                        ack->NID_EM = emerg->NID_EM;
                        ack->Q_EMERGENCYSTOP.rawdata = result;
                        fill_message(ack);
                        supervising_rbc->send(std::shared_ptr<euroradio_message_traintotrack>(ack));
                    });
                }
                break;
            }
            case 16:
                info = new etcs_information(40, 42, [message]() {
                    auto *emerg = (unconditional_emergency_stop*)message.get();
                    handle_unconditional_emergency_stop(emerg->NID_EM);
                    emergency_acknowledgement_message *ack = new emergency_acknowledgement_message();
                    ack->NID_EM = emerg->NID_EM;
                    ack->Q_EMERGENCYSTOP.rawdata = 2;
                    fill_message(ack);
                    supervising_rbc->send(std::shared_ptr<euroradio_message_traintotrack>(ack));
                });
                break;
            case 18:
                info = new etcs_information(42, 44, [message]() {
                    auto *emerg = (emergency_stop_revocation*)message.get();
                    revoke_emergency_stop(emerg->NID_EM);
                });
                break;
            case 27:
                info = new etcs_information(43, 45, []() {
                    update_dialog_step("SH refused", "");
                });
                break;
            case 28:
                info = new SH_authorisation_info();
                break;
            case 34:{
                auto *taf = (taf_request_message*)message.get();
                if (!((taf->Q_DIR == Q_DIR_t::Nominal && dir == 1) && (taf->Q_DIR == Q_DIR_t::Reverse && dir == 0))) {
                    info = new etcs_information(47, 49, [taf,ref]() {
                        distance dist = ref + taf->D_TAFDISPLAY.get_value(taf->Q_SCALE);
                        double length = taf->L_TAFDISPLAY.get_value(taf->Q_SCALE);
                        request_track_ahead_free(dist, length);
                    });
                }
                break;
            }
            case 40:
                info = new etcs_information(50, 52, []() {
                    if (som_status == D33 || som_status == D22)
                        som_status = A38;
                });
                break;
            case 41:
                info = new etcs_information(51, 53, []() {
                    if (som_status == D33 || som_status == D22)
                        som_status = A23;
                });
                break;
            case 43:
                info = new etcs_information(52, 54, [](){ position_valid = true; });
                break;
            case 45:
                info = new coordinate_system_information();
                break;
            default:
                break;
        }
        if (info != nullptr) {
            info->ref = ref;
            info->dir = dir;
            info->fromRBC = session->isRBC ? session : nullptr;
            info->nid_bg = lrbg;
            info->infill = false;
            info->timestamp = message->T_TRAIN.get_value();
            info->message = message;
            info->version = session->version;
            ordered_info.push_back(std::shared_ptr<etcs_information>(info));
        }
    }
    bool infill=false;
    for (int j=0; j<message->packets.size(); j++) {
        ETCS_packet *p = message->packets[j].get();
        if (p->directional) {
            auto *dp = (ETCS_directional_packet*)p;
            if ((dp->Q_DIR == Q_DIR_t::Nominal && dir == 1) || (dp->Q_DIR == Q_DIR_t::Reverse && dir == 0))
                continue;
        }
        if (p->NID_PACKET == 136) {
            InfillLocationReference ilr = *((InfillLocationReference*)p);
            bool found = false;
            for (auto it = link_expected; it!=linking.end(); ++it) {
                link_data &l = *it;
                if (l.nid_bg == bg_id({ilr.Q_NEWCOUNTRY == Q_NEWCOUNTRY_t::SameCountry ? lrbg.NID_C : ilr.NID_C, (int)ilr.NID_BG})) {
                    found = true;
                    infill = true;
                    ref = l.dist;
                    break;
                }
            }
            if (!found)
                break;
        } else if (p->NID_PACKET == 80) {
            for (auto it = ordered_info.rbegin(); it!=ordered_info.rend(); ++it) {
                if (it->get()->index_level == 3 || it->get()->index_level == 39) {
                    it->get()->linked_packets.push_back(message->packets[j]);
                    break;
                }
            }
        } else if (p->NID_PACKET == 49) {
            for (auto it = ordered_info.rbegin(); it!=ordered_info.rend(); ++it) {
                if (it->get()->index_level == 3 || it->get()->index_level == 39 || it->get()->index_level == 44) {
                    it->get()->linked_packets.push_back(message->packets[j]);
                    break;
                }
            }
        } else if (p->NID_PACKET == 63) {
            for (auto it = ordered_info.begin(); it!=ordered_info.end(); ++it) {
                if (it->get()->index_level == 14) {
                    it->get()->linked_packets.push_back(message->packets[j]);
                    break;
                } 
            }
        }
        std::vector<etcs_information*> info = construct_information(p, message.get());
        for (int i=0; i<info.size(); i++) {
            info[i]->linked_packets.push_back(message->packets[j]);
            info[i]->ref = ref;
            info[i]->dir = dir;
            info[i]->fromRBC = session->isRBC ? session : nullptr;
            info[i]->nid_bg = lrbg;
            info[i]->infill = infill;
            info[i]->timestamp = message->T_TRAIN.get_value()*10;
            info[i]->message = message;
            info[i]->version = session->version;
            ordered_info.push_back(std::shared_ptr<etcs_information>(info[i]));
        }
    }
    ordered_info.sort(info_compare);
    for (auto it = ordered_info.begin(); it!=ordered_info.end(); ++it)
    {
        try_handle_information(*it, ordered_info);
    }
}
struct level_filter_data
{
    int num;
    Level level;
    bool fromRBC;
    bool operator<(const level_filter_data &o) const
    {
        if (num==o.num) {
            if (level == o.level)
                return fromRBC<o.fromRBC;
            return level<o.level;
        }
        return num<o.num;
    }
};
struct accepted_condition
{
    bool reject;
    std::set<int> exceptions;
};
std::map<level_filter_data, accepted_condition> level_filter_index;
bool level_filter(std::shared_ptr<etcs_information> info, std::list<std::shared_ptr<etcs_information>> message) 
{
    accepted_condition s = level_filter_index[{info->index_level, level, info->fromRBC != nullptr}];
    if (!s.reject) {
        if (s.exceptions.find(3) != s.exceptions.end()) {
            if (supervising_rbc && supervising_rbc->train_data_ack_pending)
                return false;
        }
        if (s.exceptions.find(4) != s.exceptions.end()) {
            if (info->linked_packets.begin()->get()->NID_PACKET == 12) {
                Level1_MA ma = *((Level1_MA*)info->linked_packets.begin()->get());
                movement_authority MA = movement_authority(info->ref, ma, info->timestamp);
                distance end = MA.get_abs_end();
                if (get_SSP().empty() || (--get_SSP().end())->get_end()<end || get_SSP().begin()->get_start() > d_estfront)
                    return false;
                if (get_gradient().empty() || (--get_gradient().end())->first<end || get_gradient().begin()->first > d_estfront)
                    return false;   
            }
        }
        if (s.exceptions.find(5) != s.exceptions.end()) {
            if (!emergency_stops.empty())
                return false;
        }
        if (s.exceptions.find(8) != s.exceptions.end()) {
            TemporarySpeedRestriction tsr = *((TemporarySpeedRestriction*)info->linked_packets.begin()->get());
            if(tsr.NID_TSR != NID_TSR_t::NonRevocable && inhibit_revocable_tsr) return false;
        }
        if (s.exceptions.find(9) != s.exceptions.end()) {
            if (!ongoing_transition || (ongoing_transition->leveldata.level != Level::N2 && ongoing_transition->leveldata.level != Level::N3))
                return false;
        }
        if (s.exceptions.find(10) != s.exceptions.end()) {
            auto &msg = *((coordinate_system_assignment*)info->message->get());
            bg_id prvlrbg = {-1,-1};
            bg_id memorized_lrbg;
            for (auto &lrbg : lrbgs) {
                if (lrbg.nid_lrbg == msg.NID_LRBG.get_value() && prvlrbg.NID_BG >= 0) {
                    if (memorized_lrbg.NID_BG >= 0 && memorized_lrbg != prvlrbg)
                        return false;
                    else
                        memorized_lrbg = prvlrbg;
                }
                prvlrbg = lrbg.nid_lrbg;
            }
        }
        if (s.exceptions.find(11) != s.exceptions.end()) {
            if (ongoing_transition)
                return false;
            for (auto m : message) {
                if (m->index_level == 8)
                    return false;
            }
        }
        if (s.exceptions.find(13) != s.exceptions.end()) {
            bool ltr_order_received = false;
            for (auto m : message) {
                if (m->index_level == 8) {
                    LevelTransitionOrder LTO = *(LevelTransitionOrder*)m->linked_packets.front().get();
                    Level lv = level_transition_information(LTO, m->ref).leveldata.level;
                    if (lv == Level::N1 || lv == Level::N2 || level == Level::N3)
                        ltr_order_received = true;
                } else if (m->index_level == 9) {
                    ConditionalLevelTransitionOrder CLTO = *(ConditionalLevelTransitionOrder*)m->linked_packets.front().get();
                    Level lv = level_transition_information(CLTO, m->ref).leveldata.level;
                    if (lv == Level::N1 || lv == Level::N2 || level == Level::N3)
                        ltr_order_received = true;
                }
            }
            if (!ltr_order_received)
                return false;
        }
        if (s.exceptions.find(14) != s.exceptions.end()) {
            SessionManagement &session = *(SessionManagement*)info->linked_packets.front().get();
            contact_info info = {session.NID_C, session.NID_RBC, session.NID_RADIO};
            if (session.Q_RBC == Q_RBC_t::EstablishSession) {
                if (accepting_rbc && accepting_rbc->contact == info)
                    return false;
                for (auto m : message) {
                    if (m->index_level == 16) {
                        RBCTransitionOrder o = *(RBCTransitionOrder*)m->linked_packets.front().get();
                        contact_info info2 = {o.NID_C, o.NID_RBC, o.NID_RADIO.get_value()};
                        if (info2 == info)
                            return false;
                    }
                }
            }
            return true;
        }
        return true;
    } else {
        if (s.exceptions.find(1) != s.exceptions.end()) {
            if (ongoing_transition && ongoing_transition->leveldata.level == Level::N1)
                transition_buffer.back().push_back(info);
            return false;
        }
        if (s.exceptions.find(2) != s.exceptions.end()) {
            if (ongoing_transition && (ongoing_transition->leveldata.level == Level::N2 || ongoing_transition->leveldata.level == Level::N3))
                transition_buffer.back().push_back(info);
            return false;
        }
        return false;
    }
    return false;
}
void set_level_filter()
{
    std::vector<std::vector<std::string>> conds = {
        {"A","A","A","A","A","R2","R2","R2","A","A"},
        {"R1","R1","A","R1","R1","R2","R2","R2","A3","A3"},
        {"R1","R1","A","R1","R1","","","","",""},
        {"R1","R1","A4","R1","R1","R2","R2","R2","A3,4,5","A3,4,5"},
        {"R","R","A","R","R","","","","",""},
        {"R1","R1","A","R1","R1","R2","R2","R2","A3","A3"},
        {"R1","R1","A","R1","R1","R2","R2","R2","A3","A3"},
        {"R1","R1","A","R1","R1","R2","R2","R2","A3","A3"},
        {"A","A","A","A","A","A","A","A","A","A"},
        {"A11","A11","A11","A11","A11","","","","",""},
        {"A","A","A","A14","A14","A","A","A","A","A"},
        {"A","A","A","A","A","A","A","A","A","A"},
        {"","","","","","A","A","A","A","A"},
        {"","","","","","A","A","A","A","A"},
        {"","","","","","R","R","R","A3","A3"},
        {"R","R","A","A","A","","","","",""},
        {"R","R","A","R","R","","","","",""},
        {"A","R1,2","A","A8","A8","R2","R2","R2","A3","A3"},
        {"A","R1,2","A","A","A","R2","R2","R2","A3","A3"},
        {"","","","","","R2","R2","R2","A","A"},
        {"A","R1,2","A","A","A","","","","",""},
        {"R1","R1","A","R1","R1","R2","R2","R2","A3","A3"},
        {"R1","R1","A","R","R","R2","R2","R2","A","A"},
        {"A","R1,2","A","A","A","R2","R2","R2","A12","A12"},
        {"A","R1,2","A","A","A","R2","R2","R2","A12","A12"},
        {"A","R1,2","A","A","A","R2","R2","R2","A","A"},
        {"R","R","R","A","A","R","R","R","A3","A3"},
        {"A13","A13","A","A","A","","","","",""},
        {"A","A","A","A","A","","","","",""},
        {"R","R","A","R1","R1","","","","",""},
        {"R","R","A","R","R","","","","",""},
        {"A","A","A","A","A","","","","",""},
        {"","","","","","A10","A10","A10","A10","A10"},
        {"R","R","A","R1","R1","","","","",""},
        {"R1","R1","A","R1","R1","R2","R2","R2","A3","A3"},
        {"A","A","A","A","A","","","","",""},
        {"A","A","A","A","A","","","","",""},
        {"","","","","","R","R","R","A","A"},
        {"","","","","","A","A","A","A","A"},
        {"","","","","","R","R","R","A3,4,5","A3,4,5"},
        {"","","","","","R2","R2","R2","A","A"},
        {"","","","","","R2","R2","R2","A","A"},
        {"","","","","","R","R","R","A3","A3"},
        {"","","","","","R","R","R","A3","A3"},
        {"A","A","A","A","A","A","A","A","A","A"},
        {"A","A","A","A","A","","","","",""},
        {"","","","","","R","R","R","A3","A3"},
        {"","","","","","R","R","R","A3","A3"},
        {"A","A","A","A","A","A","A","A","A","A"},
        {"","","","","","R","R","R","A","A"},
        {"","","","","","R","R","R","A","A"},
        {"","","","","","R","R","R","A","A"},
        {"R1","R1","A","R1","R1","R2","R2","R2","A3","A3"},
        {"R1","R1","A","R1","R1","R2","R2","R2","A3","A3"},
        {"A","A","A","A","A","","","","",""},
        {"A9","A9","A9","R","R","","","","",""},
        {"R1","R1","A","R1","R1","R2","R2","R2","A3","A3"},
        {"R1,2","R1,2","A","A","A","R2","R2","R2","A3","A3"},
        {"A","A","A","A","A","","","","",""},
        {"A","A","A","A","A","","","","",""},
        {"R1","R1","A","R1","R1","R2","R2","R2","A3,5","A3,5"},
        {"R","R","A","R","R","R","R","R","A","A"},
        {"A","A","A","A","A","A","A","A","A","A"}
    };
    Level levels[] = {Level::N0, Level::NTC, Level::N1, Level::N2, Level::N3};
    for (int i=0; i<conds.size(); i++) {
        for (int j=0; j<10; j++) {
            std::string str = conds[i][j];
            if (str != "") {
                bool rej = str[0] == 'R';
                str = str.substr(1);
                std::set<int> except;
                for(;!str.empty();) {
                    std::size_t index = str.find_first_of(',');
                    except.insert(std::stoi(str.substr(0, index)));
                    if (index != std::string::npos)
                        str = str.substr(index+1);
                    else
                        break;
                }
                level_filter_index[{i, levels[j%5], j>4}] = {rej, except};
            }
        }
    }
}
struct mode_filter_data
{
    int num;
    Mode mode;
    bool operator<(const mode_filter_data &o) const
    {
        if (num==o.num) {
            return mode<o.mode;
        }
        return num<o.num;
    }
};
std::map<mode_filter_data, accepted_condition> mode_filter_index;
void set_mode_filter()
{
    std::vector<std::vector<std::string>> conds = {
        {"NR","A2","A","A","A","A","A","A","A","A","A","A","A1","NR","NR","A","A"},
        {"NR","A2,4","R","R","A","A","A","A","R","A","A","R","A1","NR","NR","A","R"},
        {"NR","A2,4","R","R","A","A","A","A","R","R","A","R","A1","NR","NR","A","R"},
        {"NR","A2,4","R","R","A","A","A","A","R","R","A","R","A1","NR","NR","A","R"},
        {"NR","R","R","R","A","A","R","A","R","R","R","R","R","NR","NR","R","R"},
        {"NR","A2,4","R","R","A","A","A","A","R","R","A","R","A1","NR","NR","A","R"},
        {"NR","A2,4","R","R","A","A","A","A","R","R","A","R","A1","NR","NR","A","R"},
        {"NR","A2,4","R","R","A","A","A","A","R","R","A","R","A1","NR","NR","A","R"},
        {"NR","A2","R","R","A","A","A","A","R","R","A","A","A1","NR","NR","A","R"},
        {"NR","A2","R","R","A","A","A","A","R","R","A","A","A1","NR","NR","R","R"},
        {"NR","A2","A7","A7","A","A","A","A","A","A","A","A","A1,5","NR","NR","A","R"},
        {"NR","A","A3","A3","A","A","A","A","A","A","A","A","A1","NR","NR","A","A"},
        {"NR","A2","A","A","A","A","A","A","A","A","A","A","A1","NR","NR","A","A"},
        {"NR","A2","R","R","A","A","A","A","R","R","A","R","A1","NR","NR","A","R"},
        {"NR","A2","R","R","A","A","A","A","R","A","A","R","A1","NR","NR","A","A"},
        {"NR","A2,4","R","R","R","R","A","R","R","R","R","R","A1","NR","NR","R","R"},
        {"NR","R","R","R","R","R","A","R","R","R","R","R","R","NR","NR","R","R"},
        {"NR","R","R","R","R","R","A6","R","R","R","R","R","R","NR","NR","R","R"},
        {"NR","A2,4","R","R","A","A","A","A","R","R","A","A","A1","NR","NR","A","R"},
        {"NR","A2,4","R","R","A","A","A","A","R","R","A","A","A1","NR","NR","A","R"},
        {"NR","A2","R","R","A","A","A","A","R","R","A","A","A1","NR","NR","A","R"},
        {"NR","A2,4","R","R","A","A","A","A","R","R","A","A","A1","NR","NR","A","R"},
        {"NR","A2,4","R","R","A","A","A","A","R","R","A","R","A1","NR","NR","A","R"},
        {"NR","A2,4","R","R","A","A","A","A","R","R","A","R","A1","NR","NR","A","R"},
        {"NR","A2","R","R","A","A","A","A","R","R","A","A","A1","NR","NR","A","A"},
        {"NR","A2","R","R","A","A","A","A","R","R","A","A","A1","NR","NR","A","A"},
        {"NR","A2","R","R","A","A","A","A","R","A","A","A","A1","NR","NR","A","R"},
        {"NR","A2,4","A8","A8","A","A","A","A","A","A","R","A","A1","NR","NR","R","R"},
        {"NR","R","R","A","R","R","R","R","R","R","R","R","R","NR","NR","R","R"},
        {"NR","R","A","R","R","R","R","R","R","R","R","R","R","NR","NR","R","R"},
        {"NR","R","R","R","A","A","A","A", "R", "R", "R", "R", "R","NR","NR","R","R"},
        {"NR","R","R","R","A","A","A","A", "R", "R", "R", "R", "R","NR","NR","R","R"},
        {"NR","R","R","A","A","A","A","A","A","A","A","A","R","NR","NR","A","A"},
        {"NR","A2","R","R","R","R","A","R","R","A","A","R","A1","NR","N","R","A","R"},
        {"NR","R","R","R","A","A","R","R","R","R","R","R","R","NR","NR","R","R"},
        {"NR","A2,4","R","R","A","A","A","A","R","A","A","A","A1","NR","NR","A","R"},
        {"NR","A2,4","R","R","A","A","A","A","R","R","A","R","A1","NR","NR","A","R"},
        {"NR","A2,4","A","A","A","A","A","A","A","A","A","A","A1","NR","NR","A","R"},
        {"NR","A2","A","A","A","A","A","A","A","A","A","A","A","NR","NR","A","A"},
        {"NR","R","R","R","R","R","R","R","R","R","R","R","A","NR","NR","R","R"},
        {"NR","A2","R","R","A","A","A","A","R","R","A","A","A","NR","NR","A","A"},
        {"NR","R","R","R","A","A","R","A","R","R","R","R","R","NR","NR","R","R"},
        {"NR","A2","R","R","A","A","A","A","R","R","A","R","R","NR","NR","A","R"},
        {"NR","R","R","R","A","A","R","A","R","R","A","R","R","NR","NR","A","R"},
        {"NR","R","R","R","A","A","R","A","R","R","R","R","A1","NR","NR","R","R"},
        {"NR","A2","R","R","A","A","A","A","R","R","R","R","A1","NR","NR","R","R"},
        {"NR","A2","R","R","A","A","A","A","R","R","R","R","A1","NR","NR","R","R"},
        {"NR","A","A","A","A","A","A","A","A","A","A","A","A","NR","NR","A","A"},
        {"NR","A","A","A","A","A","A","A","A","A","A","A","A","NR","NR","A","A"},
        {"NR","A2","R","R","R","A","A","A","R","R","R","R","A1","NR","NR","R","R"},
        {"NR","A2","R","R","A","A","A","A","R","A","R","A","A","NR","NR","R","A"},
        {"NR","A","A","A","A","A","A","A","A","A","A","A","A","NR","NR","A","A"},
        {"NR","A2","R","R","R","R","R","R","R","R","R","R","R","NR","NR","R","R"},
        {"NR","A2","R","R","R","R","R","R","R","R","R","R","R","NR","NR","R","R"},
        {"NR","A2","R","R","R","R","R","R","R","R","R","R","R","NR","NR","R","R"},
        {"NR","A2,4","R","R","A","A","A","A","R","R","A","R","A1","NR","NR","A","A"},
        {"NR","A2,4","R","R","A","A","A","A","R","R","A","R","A1","NR","NR","A","A"},
        {"NR","A2","A","A","A","A","A","A","A","A","A","A","A","NR","NR","A","A"},
        {"NR","A2","R","R","A","A","A","A","R","R","A","A","A","NR","NR","A","R"},
        {"NR","A2,4","R","R","A","A","A","A","R","R","A","R","A1","NR","NR","A","R"},
        {"NR","A2,4","R","R","A","A","A","A","R","R","A","R","A1","NR","NR","A","R"},
        {"NR","A","A","A","A","A","A","A","A","A","A","A","A","NR","NR","A","A"},
        {"NR","A","A","A","A","A","A","A","A","A","A","A","A","NR","NR","A","A"},
        {"NR","R","R","R","A9","A","A9","A9","R","R","A9","R","R","NR","NR","A9","R"},
        {"NR","R","R","R","R","A","R","R","R","R","R","R","R","NR","NR","R","R"},
        {"NR","A","A","A","A","A","A","A","A","A","A","A","A","NR","NR","A","A"}
    };
    Mode modes[] = {Mode::NP, Mode::SB, Mode::PS, Mode::SH, Mode::FS, Mode::LS, Mode::SR, Mode::OS, Mode::SL, Mode::NL, Mode::UN, Mode::TR, Mode::PT, Mode::SF, Mode::IS, Mode::SN, Mode::RV};
    for (int i=0; i<conds.size(); i++) {
        for (int j=0; j<17; j++) {
            std::string str = conds[i][j];
            if (str != "" && str != "NR") {
                bool rej = str[0] == 'R';
                str = str.substr(1);
                std::set<int> except;
                for(;!str.empty();) {
                    std::size_t index = str.find_first_of(',');
                    except.insert(std::stoi(str.substr(0, index)));
                    if (index != std::string::npos)
                        str = str.substr(index+1);
                    else
                        break;
                }
                mode_filter_index[{i, modes[j]}] = {rej, except};
            }
        }
    }
}
void set_message_filters()
{
    set_mode_filter();
    set_level_filter();
}
bool second_filter(std::shared_ptr<etcs_information> info, std::list<std::shared_ptr<etcs_information>> message)
{
    if (!info->fromRBC || info->fromRBC == supervising_rbc)
        return true;
    if (info->fromRBC == handing_over_rbc)
        return info->index_level == 10;
    if (info->index_level == 42 || info->index_level == 39 || info->index_level == 61 || info->index_level == 62)
        return false;
    if (info->index_level == 10 || info->index_level == 37)
        return true;
    transition_buffer.back().push_back(info);
    return false;
}
bool mode_filter(std::shared_ptr<etcs_information> info, std::list<std::shared_ptr<etcs_information>> message)
{
    if (info->infill && mode != Mode::FS && mode != Mode::LS)
        return false;
    accepted_condition s = mode_filter_index[{info->index_mode, mode}];
    if (s.reject) {
        return false;
    } else {
        if (s.exceptions.find(1) != s.exceptions.end()) {
            if (level == Level::N1 || !trip_exit_acknowledged/*|| info->timestamp < trip_exit_timestamp*/) return false;
        }
        if (s.exceptions.find(2) != s.exceptions.end()) {
            if (!cab_active[0] && !cab_active[1]) return false;
        }
        if (s.exceptions.find(4) != s.exceptions.end()) {
            if (!train_data_valid) return false;
        }
        if (s.exceptions.find(5) != s.exceptions.end()) {
            if (info->index_level == 8) {
                LevelTransitionOrder &LTO = *(LevelTransitionOrder*)info->linked_packets.front().get();
                if (LTO.D_LEVELTR == D_LEVELTR_t::Now) return false;
            }
            if (info->index_level == 9)
                return false;
        }
        if (s.exceptions.find(6) != s.exceptions.end()) {
            if (overrideProcedure) return false;
        }
        if (s.exceptions.find(7) != s.exceptions.end()) {
            if (info->index_level == 8) {
                LevelTransitionOrder &LTO = *(LevelTransitionOrder*)info->linked_packets.front().get();
                if (LTO.D_LEVELTR != D_LEVELTR_t::Now) return false;
            }
        }
        if (s.exceptions.find(8) != s.exceptions.end()) {
            if (info->index_level == 10) {
                RBCTransitionOrder &o = *(RBCTransitionOrder*)info->linked_packets.front().get();
                if (o.D_RBCTR != 0) return false;
            }
        }
        if (s.exceptions.find(9) != s.exceptions.end()) {
            bool inside_ls = false;
            for (auto i : message) {
                if (i->index_mode == 3) {
                    for (auto it = ++i->linked_packets.begin(); it != i->linked_packets.end(); ++it) {
                        if (it->get()->NID_PACKET == 80) {
                            ModeProfile &profile = *(ModeProfile*)(it->get());
                            std::vector<MP_element_packet> mps;
                            mps.push_back(profile.element);
                            mps.insert(mps.end(), profile.elements.begin(), profile.elements.end());
                            distance start = i->ref;
                            for (auto it2 = mps.begin(); it2 != mps.end(); ++it2) {
                                start += it2->D_MAMODE.get_value(profile.Q_SCALE);
                                distance end = start+it2->L_MAMODE;
                                if (start < d_maxsafefront(start) && d_maxsafefront(end) < end && it2->M_MAMODE == M_MAMODE_t::LS)
                                    inside_ls = true;
                            }
                        }
                    }
                }
            }
            if (!inside_ls)
                return false;
        }
        return true;
    }
}
void try_handle_information(std::shared_ptr<etcs_information> info, std::list<std::shared_ptr<etcs_information>> message)
{
    if (!level_filter(info, message)) return;
    if (!second_filter(info, message)) return;
    if (!mode_filter(info, message)) return;
    info->handle();
}
std::vector<etcs_information*> construct_information(ETCS_packet *packet, euroradio_message *msg)
{
    int packet_num = packet->NID_PACKET.rawdata;
    std::vector<etcs_information*> info;
    if (packet_num == 2) {
        info.push_back(new version_order_information());
    } else if (packet_num == 3) {
        info.push_back(new national_values_information());
    } else if (packet_num == 5) {
        info.push_back(new linking_information());
    } else if (packet_num == 6) {
        info.push_back(new vbc_order());
    } else if (packet_num == 12) {
        info.push_back(new ma_information());
        info.push_back(new signalling_information());
    } else if (packet_num == 15) {
        if (msg != nullptr && msg->NID_MESSAGE == 9)
            info.push_back(new ma_shortening_information());
        else
            info.push_back(new ma_information_lv2());
    } else if (packet_num == 16) {
        info.push_back(new repositioning_information());
    } else if (packet_num == 21) {
        info.push_back(new gradient_information());
    } else if (packet_num == 27) {
        info.push_back(new issp_information());
    } else if (packet_num == 39) {
        info.push_back(new track_condition_information());
    } else if (packet_num == 40) {
        info.push_back(new track_condition_information());
    } else if (packet_num == 41) {
        info.push_back(new leveltr_order_information());
    } else if (packet_num == 42) {
        info.push_back(new session_management_information());
    } else if (packet_num == 46) {
        info.push_back(new condleveltr_order_information());
    } else if (packet_num == 52) {
        info.push_back(new pbd_information());
    } else if (packet_num == 57) {
        info.push_back(new ma_request_params_info());
    } else if (packet_num == 58) {
        info.push_back(new position_report_params_info());
    } else if (packet_num == 65) {
        info.push_back(new TSR_information());
    } else if (packet_num == 66) {
        info.push_back(new TSR_revocation_information());
    } else if (packet_num == 67) {
        info.push_back(new track_condition_big_metal_information());
    } else if (packet_num == 68) {
        info.push_back(new track_condition_information());
        info.push_back(new track_condition_information2());
    } else if (packet_num == 69) {
        info.push_back(new track_condition_information());
    } else if (packet_num == 70) {
        info.push_back(new route_suitability_information());
    } else if (packet_num == 72) {
        info.push_back(new plain_text_information());
    } else if (packet_num == 76) {
        info.push_back(new fixed_text_information());
    } else if (packet_num == 79) {
        info.push_back(new geographical_position_information());
    } else if (packet_num == 88) {
        info.push_back(new level_crossing_information());
    } else if (packet_num == 90) {
        info.push_back(new taf_level23_information());
    } else if (packet_num == 131) {
        info.push_back(new rbc_transition_information());
    } else if (packet_num == 132) {
        info.push_back(new danger_for_SH_information());
    } else if (packet_num == 137) {
        info.push_back(new stop_if_in_SR_information());
    } else if (packet_num == 140) {
        info.push_back(new train_running_number_information());
    } else if (packet_num == 141) {
        info.push_back(new TSR_gradient_information());
    } else if (packet_num == 180) {
        auto *order = (LSSMAToggleOrder*)packet;
        if (order->Q_LSSMA == Q_LSSMA_t::ToggleOff)
            info.push_back(new lssma_display_off_information());
        else
            info.push_back(new lssma_display_on_information());
    } else if (packet_num == 181) {
        info.push_back(new generic_ls_marker_information());
    }
    return info;
}
