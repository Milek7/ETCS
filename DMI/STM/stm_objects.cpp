/*
 * European Train Control System
 * Copyright (C) 2019-2023  César Benito <cesarbema2009@hotmail.com>
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "stm_objects.h"
#include "../monitor.h"
#include "../graphics/display.h"
#include "../graphics/text_button.h"
#include "../messages/messages.h"
#include "../time_etcs.h"
#include "../../EVC/Packets/STM/15.h"
#include "../../EVC/Packets/STM/32.h"
#include "../../EVC/Packets/STM/35.h"
#include "../../EVC/Packets/STM/38.h"
#include "../../EVC/Packets/STM/39.h"
#include <fstream>
std::map<int, std::vector<int>> but_pos; 
std::map<int, std::vector<int>> ind_pos;
ntc_window *active_ntc_window;
std::map<int,ntc_window*> ntc_windows;
std::map<std::string, std::vector<int>> areas;
std::string stm_layout_file = "stm_windows.json";
void setup_areas()
{
    areas["A1"] = {0, 15, 54, 54};
    areas["A2"] = {0, 69, 54, 30};
    areas["A3"] = {0, 99, 54, 191};
    areas["A4"] = {0, 290, 54, 25};
    areas["B3"] = {140, 271, 36, 36};
    areas["B4"] = {176, 271, 36, 36};
    areas["B5"] = {212, 271, 36, 36};
    areas["C8"] = {0, 315, 54, 25};
    areas["C9"] = {0, 340, 54, 25};
    areas["C2"] = {54, 315, 37, 50};
    areas["C3"] = {91, 315, 37, 50};
    areas["C4"] = {128, 315, 37, 50};
    areas["C1"] = {165, 315, 58, 50};
    areas["C5"] = {223, 315, 37, 50};
    areas["C6"] = {260, 315, 37, 50};
    areas["C7"] = {297, 315, 37, 50};
    for (int i=1; i<=4; i++) {
        areas["E"+std::to_string(i)] = {0, 365 + 20*(i-1), 54, 25};
    }
    for (int i=5; i<=9; i++) {
        areas["E"+std::to_string(i)] = {54, 365 + 20*(i-5), 234, 20};
    }
    areas["E10"] = {288, 365, 46, 50};
    areas["E11"] = {288, 415, 46, 50};
    for (int i=5; i<=9; i++) {
        areas["E"+std::to_string(i)] = {54, 365 + 20*(i-5), 234, 20};
    }
    for (int i=1; i<=9; i++) {
        areas["F"+std::to_string(i)] = {580, 15 + 50*(i-1), 60, 50};
    }
    for (int i=1; i<=10; i++) {
        areas["G"+std::to_string(i)] = {334 + ((i-1)%5)*49, 315 + ((i-1)/5) * 50, (i==5 || i==10) ? 50 : 49, 50};
    }
    areas["G11"] = {334, 415, 63, 50};
    areas["G12"] = {397, 415, 120, 50};
    areas["G13"] = {517, 415, 63, 50};
}
ntc_window::ntc_window(int nid_stm) : nid_stm(nid_stm)
{
    #ifdef __ANDROID__
    extern std::string filesDir;
    std::ifstream file(filesDir+"/"+stm_layout_file);
#else
    std::ifstream file(stm_layout_file);
#endif
    json j;
    file >> j;
    for (json &stm : j["STM"])
    {
        if (stm["nid_stm"].get<int>() == nid_stm)
        {
            customized = new customized_dmi(stm);
            break;
        }
    }
    constructfun = [this](window *w) {construct_main(w, customized != nullptr);};
}
void ntc_window::display_indicator(int id, int position, int icon, std::string text, int properties, bool isButton)
{
    auto it = indicators.find((isButton ? 256 : 0) + id);
    if (it != indicators.end()) {
        remove(it->second);
        delete it->second;
        indicators.erase(it);
    }
    bool displayed = (properties>>9)&1;
    if (!displayed)
        return;
    std::vector<int> pos;
    if (customized != nullptr)
    {
        if (isButton)
        {
            if (customized->button_positions.find(position) == customized->button_positions.end())
                return;
            pos = customized->button_positions[position];
        }
        else
        {
            if (customized->positions.find(position) == customized->positions.end())
                return;
            pos = customized->positions[position];
        }
    }
    else
    {
        std::string area;
        if (position < 4)
            area = "B"+std::to_string(position+2);
        else if (position == 4)
            area = "H1";
        else if (position < 10)
            area = "C"+std::to_string(position-3);
        else if (position < 20)
            area = "G"+std::to_string(position-9);
        /*{
            if (position < 3)
                area = "F"+std::to_string(position+7);
            else if (position < 8)
                area = "C"+std::to_string(position-1);
            else if (position < 18)
                area = "G"+std::to_string(position-7);
        }*/
        if (areas.find(area) == areas.end())
            return;
        pos = areas[area];
    }
    Component *c;
    if (isButton) c = new Button(pos[2], pos[3]);
    else c = new Component(pos[2],pos[3]);
    Color bg = get_color((properties>>3)&7, true);
    Color fg = get_color(properties&7, false);
    c->setBackgroundColor(bg);
    c->setForegroundColor(fg);
    bool text_also;
    if (customized != nullptr)
    {
        bool text_also = true;
        if (icon > 0 && customized->icons.find(icon) != customized->icons.end())
        {
            auto &ic = customized->icons[icon];
            text_also = ic.text_also;
            c->addImage("symbols/STM/"+ic.file);
        }
        if (text_also && text.size() > 0)
        {
            customized_dmi::indicator ind;
            ind.font_size = 12;
            ind.align = CENTER;
            if (customized != nullptr)
            {
                if (!isButton && customized->indicators.find(id) != customized->indicators.end()) ind = customized->indicators[id];
                else if (isButton && customized->buttons.find(id) != customized->buttons.end()) ind = customized->buttons[id];
            }
            c->addText(text, 0, 0, ind.font_size, fg, ind.align);
        }
    }
    else
    {
        if (text.size() > 0)
            c->addText(text, 0, 0, 12, fg);
    }
    bool counterflash = (properties>>8)&1;
    int flash = (properties>>6)&3;
    if (flash != 0)
    {
        c->flash_style = (flash-1) | (counterflash<<1);
        if (customized != nullptr && customized->flash_style == 1) c->flash_style |= 4;
    }
    indicators[id] = c;
    addToLayout(c, new RelativeAlignment(nullptr, pos[0], pos[1]));
}
void ntc_window::display_text(int id, bool ack, std::string text, int properties)
{
    bool firstGroup = ((properties>>9) & 1) == 0;
    int flash = (properties>>6)&3;
    bool counter = (properties>>8) & 1;
    Color bg = get_color((properties>>3)&7, true);
    Color fg = get_color(properties&7, false);
    Message m(id|0xff00, text, getHour(), getMinute(), firstGroup, ack, 0, fg, bg);
    messages[id] = m;
    if (active_ntc_window == this) addMsg(m);
}
void initialize_stm_windows()
{
    setup_areas();

    /*std::string val = "00101011""0000001100100""01""0100000000""0010000""0000000000""0100000010""000001000000000"
        "000""010""11""011""10""000""00""101""00""11";
    parse_stm_message(val);*/
}
void parse_stm_message(const stm_message &message)
{
    if (!message.valid) return;
    int nid_stm = message.NID_STM.rawdata;
    stm_state state = stm_state::NP;
    for (auto &pack : message.packets)
    {
        if (pack->NID_PACKET == 15)
        {
            state = (stm_state)((STMStateReport*)pack.get())->NID_STMSTATE.rawdata;
        }
    }
    if (state != stm_state::DA && state != stm_state::HS)
    {
        if (ntc_windows.find(nid_stm) != ntc_windows.end())
        {
            ntc_windows[nid_stm]->state = state;
        }
        return;
    }
    if (ntc_windows.find(nid_stm) == ntc_windows.end())
    {
        ntc_windows[nid_stm] = new ntc_window(nid_stm);
        ntc_windows[nid_stm]->construct();
    }
    auto *window = ntc_windows[nid_stm];
    window->last_time = get_milliseconds();
    window->state = state;
    for (auto &pack : message.packets)
    {
        if (pack->NID_PACKET == 43)
        {
            STMSupervisionInformation &info = *((STMSupervisionInformation*)pack.get());
            Vperm = info.V_PERMIT.rawdata;
            Vtarget = info.V_TARGET.rawdata*5;
            Vrelease = info.V_RELEASE.rawdata;
            Vsbi = info.V_INTERV.rawdata;
            Dtarg = (int)info.D_TARGET.get_value(info.Q_SCALE);
            window->monitoring_data = stm_monitoring_data(info);
        }
        else if (pack->NID_PACKET == 32)
        {
            STMButtonRequest &buttons = *((STMButtonRequest*)pack.get());
            for (auto &button : buttons.elements)
            {
                std::string text;
                for (int i=0; i<button.X_CAPTION.size(); i++)
                {
                    unsigned char c = button.X_CAPTION[i];
                    if (button.X_CAPTION[i] < 0x80)
                    {
                        text += c;
                    }
                    else
                    {
                        text += 0xc2+(c>0xbf);
                        text += (c&0x3f)+0x80;
                    }
                }
                window->display_indicator(button.NID_BUTTON, button.NID_BUTPOS, button.NID_ICON, text, button.M_BUT_ATTRIB, true);
            }
            /*for (auto &var : r.log_entries)
            {
                std::cout<<var.first<<"\t"<<var.second<<"\n";
            }*/
        }
        else if (pack->NID_PACKET == 35)
        {
            STMIconRequest &icons = *((STMIconRequest*)pack.get());
            for (auto &icon : icons.elements)
            {
                std::string text;
                for (int i=0; i<icon.X_CAPTION.size(); i++)
                {
                    unsigned char c = icon.X_CAPTION[i];
                    if (icon.X_CAPTION[i] < 0x80)
                    {
                        text += c;
                    }
                    else
                    {
                        text += 0xc2+(c>0xbf);
                        text += (c&0x3f)+0x80;
                    }
                }
                window->display_indicator(icon.NID_INDICATOR, icon.NID_INDPOS, icon.NID_ICON, text, icon.M_IND_ATTRIB, false);
            }
            /*for (auto &var : r.log_entries)
            {
                std::cout<<var.first<<"\t"<<var.second<<"\n";
            }*/
        }
        else if (pack->NID_PACKET == 38)
        {
            STMTextMessage &msg = *((STMTextMessage*)pack.get());
            std::string text;
            for (int i=0; i<msg.X_TEXT.size(); i++)
            {
                unsigned char c = msg.X_TEXT[i];
                if (msg.X_TEXT[i] < 0x80)
                {
                    text += c;
                }
                else
                {
                    text += 0xc2+(c>0xbf);
                    text += (c&0x3f)+0x80;
                }
            }
            window->display_text(msg.NID_XMESSAGE.rawdata, msg.Q_ACK == Q_ACK_t::AcknowledgementRequired, text, msg.M_XATTRIBUTE.rawdata);
        }
        else if (pack->NID_PACKET == 39)
        {
            STMDeleteTextMessage &del = *((STMDeleteTextMessage*)pack.get());
            window->messages.erase(del.NID_XMESSAGE);
            if (active_ntc_window == window) revokeMessage(del.NID_XMESSAGE.rawdata | 0xff00);
        } 
        else if (pack->NID_PACKET == 46)
        {
            if (state != stm_state::DA) continue;
            STMSoundCommand &snds = *((STMSoundCommand*)pack.get());

            for (auto &snd : snds.sounds)
            {
                if (snd.Q_SOUND == Q_SOUND_t::Stop)
                {
                    if (window->customized != nullptr && window->customized->sounds.find(snd.NID_SOUND) != window->customized->sounds.end())
                    {
                        stopSound(window->customized->sounds[snd.NID_SOUND]);
                    }
                    for (auto it = window->generated_sounds.begin(); it != window->generated_sounds.end();)
                    {
                        if (it->first == snd.NID_SOUND)
                        {
                            stopSound(it->second);
                            delete it->second;
                            it = window->generated_sounds.erase(it);
                            continue;
                        }
                        ++it;
                    }
                }
                else
                {
                    sdlsounddata *s;
                    if (window->customized != nullptr && window->customized->sounds.find(snd.NID_SOUND) != window->customized->sounds.end())
                    {
                        s = window->customized->sounds[snd.NID_SOUND];
                    }
                    else
                    {
                        s = loadSound(snd);
                        window->generated_sounds.push_front({snd.NID_SOUND, s});
                    }
                    play(s, snd.Q_SOUND == Q_SOUND_t::PlayContinuously);
                }
            }
            int count = 0;
            for (auto it = window->generated_sounds.begin(); it != window->generated_sounds.end();)
            {
                count++;
                if (count > 2)
                {
                    stopSound(it->second);
                    delete it->second;
                    it = window->generated_sounds.erase(it);
                    continue;
                }
                ++it;
            }
        }
    }
}
void update_stm_windows()
{
    extern int maxSpeed;
    extern int etcsDialMaxSpeed;
    if (default_window == nullptr) return;
    window *prev_default_window = default_window;
    default_window = &etcs_default_window;
    active_ntc_window = nullptr;
    for (auto it = ntc_windows.begin(); it != ntc_windows.end(); )
    {
        if (/*get_milliseconds() - it->second->last_time > 2000 ||*/
            (it->second->state != stm_state::DA && prev_default_window == it->second) ||
            (it->second->state != stm_state::DA && it->second->state != stm_state::HS))
        {
            if (it->second == prev_default_window)
            {
                prev_default_window = nullptr;
                active_ntc_window = nullptr;
            }
            active_windows.erase(it->second);
            delete it->second;
            it = ntc_windows.erase(it);
            continue;
        }
        if (it->second->state == stm_state::DA)
            default_window = active_ntc_window = it->second;
        ++it;
    }

    if (default_window != prev_default_window)
    {
        if (prev_default_window != nullptr)
        {
            prev_default_window->active = false;
            active_windows.erase(prev_default_window);
            if (prev_default_window != &etcs_default_window)
            {
                auto *ntc = (ntc_window*)prev_default_window;
                for (auto &kvp : ntc->messages)
                {
                    revokeMessage(kvp.first);
                }
            }
        }
        default_window->clearLayout();
        default_window->construct();
        if (active_ntc_window != nullptr && active_ntc_window->customized != nullptr)
        {
            //extern TextButton main_button;
            //extern TextButton override_button;
            //extern TextButton dataview_button;
            //extern TextButton special_button;
            //extern TextButton settings_button;
            extern Component a4;
            extern Component modeRegion;
            extern Component levelRegion;
            extern Component c1;
            extern Component c7;
            extern Component c9;
            extern Component e1;
            for (auto &kvp : active_ntc_window->customized->moved_areas)
            {
                Component *moved = nullptr;
                if (kvp.first == "A4") moved = &a4;
                //else if (kvp.first == "F1") moved = &main_button;
                //else if (kvp.first == "F2") moved = &override_button;
                //else if (kvp.first == "F3") moved = &dataview_button;
                //else if (kvp.first == "F4") moved = &special_button;
                //else if (kvp.first == "F5") moved = &settings_button;
                else if (kvp.first == "B7") moved = &modeRegion;
                else if (kvp.first == "C8") moved = &levelRegion;
                else if (kvp.first == "C1") moved = &c1;
                else if (kvp.first == "C7") moved = &c7;
                else if (kvp.first == "C9") moved = &c9;
                else if (kvp.first == "E1") moved = &e1;
                if (moved != nullptr)
                {
                    active_ntc_window->remove(moved);
                    active_ntc_window->addToLayout(moved, new RelativeAlignment(0, kvp.second.x, kvp.second.y));
                }
            }
        }
        if (active_ntc_window != nullptr && active_ntc_window->customized != nullptr && !active_ntc_window->customized->etcs_supervision)
        {
            extern Component csg;
            extern Component a2;
            extern Component a23;
            extern Component distanceBar;
            extern Component releaseRegion;
            active_ntc_window->remove(&csg);
            active_ntc_window->remove(&a2);
            active_ntc_window->remove(&a23);
            active_ntc_window->remove(&distanceBar);
            active_ntc_window->remove(&releaseRegion);
        }
        else if (active_ntc_window != nullptr && active_ntc_window->customized != nullptr && active_ntc_window->customized->etcs_dial_range > 0)
            maxSpeed = active_ntc_window->customized->etcs_dial_range;
        else
            maxSpeed = etcsDialMaxSpeed;
        default_window->active = true;
        active_windows.insert(default_window);
        if (active_ntc_window != nullptr)
        {
            for (auto &kvp : active_ntc_window->messages)
            {
                addMsg(kvp.second);
            }
        }
    }
}