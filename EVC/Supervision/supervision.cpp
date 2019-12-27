#include <map>
#include "national_values.h"
#include "fixed_values.h"
#include "distance.h"
#include "train_data.h"
#include "speed_profile.h"
#include "targets.h"
#include "supervision.h"
#include "../antenna.h"
#include <iostream>
double V_est=0;
double V_ura = 0;
double A_est1 = 0;
double A_est2 = 0;
double calc_ceiling_limit(distance min=d_minsafefront, distance max=d_maxsafefront)
{
    std::map<distance,double> MRSP = mrsp_candidates.get_MRSP();
    //TODO: LOA speed, same formulas
    auto it1 = --MRSP.upper_bound(min);
    auto it2 = MRSP.upper_bound(max);
    double V_MRSP = 1000;
    for (auto it = it1; it!=it2; ++it) {
        V_MRSP = std::min(it->second, V_MRSP);
    }
    return V_MRSP;
}
double V_perm;
double V_target;
double V_sbi;
double D_target;
bool EB=false;
bool SB=false;
bool TCO=false;
MonitoringStatus monitoring = CSM;
SupervisionStatus supervision = NoS;
target MRDT;
distance d_startRSM;
target RSMtarget;
double V_release = 0;
static std::set<target> supervised_targets;
distance get_d_startRSM(double V_release)
{
    distance d_SvL = SvL->get_location();
    distance d_EoA = EoA->get_location();
    target tEoA = *supervised_targets.find(target(d_EoA, 0, target_class::EoA));
    int alpha = 1; //TODO: 0 if level!=1
    int Q_locacc_refBG = 0;
    distance d_tripEoA = d_EoA+alpha*L_antenna_front + std::max(2*Q_locacc_refBG+10+d_EoA.get()/10,d_maxsafefront-d_minsafefront);
    
    distance d_startRSM;
    
    distance d_sbi1 = tEoA.get_distance_curve(V_release)-V_release*T_bs1;
    distance d_sbi2(d_SvL);
    std::set<target> candidates;
    for (auto it=supervised_targets.begin(); it!=supervised_targets.end(); ++it) {
        if (it->is_EBD_based() && d_tripEoA < it->get_target_position() && it->get_target_position() <= d_SvL)
            candidates.insert(*it);
    }
    candidates.insert(target(d_SvL, 0, target_class::SvL));
    for (target t : candidates) {
        double V_delta0rs = 0;
        distance d_ebit = t.get_distance_curve(V_release + V_delta0rs)-(V_release*V_delta0rs)*(T_berem+T_traction);
        distance d_sbi2t = d_ebit - V_release * T_bs2;
        if (d_sbi2t<d_sbi2) {
            d_sbi2 = d_sbi2t;
            RSMtarget = t;
        }
    }
    if (d_sbi2-d_sbi1>=d_maxsafefront-d_estfront) {
        d_startRSM = d_sbi1;
        RSMtarget = tEoA;
    } else {
        d_startRSM = d_sbi2;
    }
    return d_startRSM;
}
double calculate_V_release()
{
    distance d_SvL = SvL->get_location();
    distance d_EoA = EoA->get_location();
    double L_antenna_front = 0;
    int alpha = 1; //TODO: 0 if level!=1
    int Q_locacc_refBG = 0;
    distance d_tripEoA = d_EoA+alpha*L_antenna_front + std::max(2*Q_locacc_refBG+10+d_EoA.get()/10,d_maxsafefront-d_minsafefront);
    double V_release = 100;
    std::set<target> candidates;
    for (auto it=supervised_targets.begin(); it!=supervised_targets.end(); ++it) {
        if (it->is_EBD_based() && d_tripEoA < it->get_target_position() && it->get_target_position() <= d_SvL)
            candidates.insert(*it);
    }
    candidates.insert(target(d_SvL, 0, target_class::SvL));
    for (target t : candidates) {
        double V_target = t.get_target_speed();
        double V_delta0rsob = 0;
        double V_releaset = V_target;
        double V_test = V_target;
        while (V_test <= V_release) {
            double D_bec = (V_test+V_delta0rsob)*(T_traction+T_berem);
            if (d_tripEoA+D_bec<=t.get_distance_curve(V_target) && std::abs(V_test-(t.get_speed_curve(d_tripEoA+D_bec)-V_delta0rsob))<=(1.0/3.6)) {
                V_releaset = V_test;
                break;
            }
            V_test += 1.0/3.6;
        }
        V_release = std::min(V_release, V_releaset);
    }
    distance d_start = get_d_startRSM(V_release);
    double V_MRSP = calc_ceiling_limit(d_start, d_tripEoA);
    return std::min(V_release, V_MRSP);
}
void update_monitor_transitions(bool suptargchang)
{
    MonitoringStatus nmonitor = monitoring;
    bool c1 = false; 
    for (target t : supervised_targets) {
        c1 |= (t.get_target_speed()<=V_est) && ((t.is_EBD_based() ? d_maxsafefront : d_estfront) > t.d_I);
    }
    c1 &= (V_est>=V_release);
    bool c2 = V_release>0 && (RSMtarget.is_EBD_based() ? d_maxsafefront : d_estfront) > d_startRSM;
    bool c3 = !c1 && !c2 && supervised_targets.find(MRDT)==supervised_targets.end();
    bool c4 = c1 && suptargchang;
    bool c5 = c2 && suptargchang;
    if (c1 && monitoring == CSM) {
        nmonitor = TSM;
    }
    if (c2 && monitoring != RSM)
        nmonitor = RSM;
    if (c3 && monitoring != CSM)
        nmonitor = CSM;
    if (c4 && monitoring != TSM)
        nmonitor = TSM;
    if (c5 && monitoring != RSM)
        nmonitor = RSM;
    if (monitoring!=nmonitor) {
        if (monitoring == TSM)
            TCO = false;
        if (monitoring == TSM && nmonitor == RSM)
            SB = false;
        monitoring = nmonitor;
    }
}
void update_supervision()
{
    bool suptargchang = supervised_targets_changed();
    
    double V_MRSP = calc_ceiling_limit();
    V_perm = V_target = V_MRSP;
    V_sbi = V_MRSP + dV_sbi(V_MRSP);
    
    target tEoA;
    target tSvL;
    supervised_targets = get_supervised_targets();
    for (auto it=supervised_targets.begin(); it!=supervised_targets.end(); ++it) {
        it->calculate_curves();
        if (it->type == target_class::SvL)
            tSvL = *it;
        if (it->type == target_class::EoA)
            tEoA = *it;
    }
    if (EoA != nullptr && SvL != nullptr) {
        if (V_release == 0)
            V_release = calculate_V_release(); 
        d_startRSM = get_d_startRSM(V_release);
    } else {
        V_release = 0;
    }
    update_monitor_transitions(suptargchang);
    if (monitoring == CSM) {
        bool t1 = V_est <= V_MRSP;
        bool t2 = V_est > V_MRSP;
        bool t3 = V_est > V_MRSP + dV_warning(V_MRSP);
        bool t4 = V_est > V_MRSP + dV_sbi(V_MRSP);
        bool t5 = V_est > V_MRSP + dV_ebi(V_MRSP);
        bool r0 = V_est == 0;
        bool r1 = V_est <= V_MRSP;
        if (t2 && (supervision == NoS || supervision == IndS))
            supervision = OvS;
        if (t3 && (supervision == NoS || supervision == IndS || supervision == OvS))
            supervision = WaS;
        if ((t4 || t5) && (supervision == NoS || supervision == IndS || supervision == OvS || supervision == WaS))
            supervision = IntS;
        if (r1 && (supervision == IndS || supervision == OvS || supervision == WaS))
            supervision = NoS;
        if ((r0 || (r1 && (!EB || Q_NVEMRRLS))) && supervision == IntS)
            supervision = NoS;
        if (t4)
            SB = true;
        if (t5)
            EB = true;
        if (r0)
            EB = false;
        if (r1) {
            SB = false;
            if (Q_NVEMRRLS)
                EB = false;
        }
    } else if (monitoring == TSM) {
        std::set<target> MRDTtarg;
        for (target t : supervised_targets) {
            if ((t.type == target_class::EoA ? d_estfront : d_maxsafefront) > t.d_I && V_est>=t.get_target_speed())
                MRDTtarg.insert(t);
        }
        if (!MRDTtarg.empty())
        {
            std::vector<target> MRDT;
            MRDT.push_back(*MRDTtarg.begin());
            for (target t : MRDTtarg) {
                if (t.V_P < MRDT[0].V_P)
                    MRDT[0] = t;
            }
            for (int i=1; i<MRDTtarg.size(); i++) {
                bool mask = false;
                for (target t : MRDTtarg) {
                    bool already = false;
                    for (int j=0; j<i; j++) {
                        if (MRDT[j]==t) {
                            already = true;
                            break;
                        }
                    }
                    if (already)
                        continue;
                    t.calculate_curves(MRDT[i-1].get_target_speed());
                    if (t.d_I < MRDT[i-1].d_P) {
                        mask = true;
                        MRDT.push_back(t);
                        break;
                    }
                }
                if (!mask)
                    break;
            }
            ::MRDT = MRDT[MRDT.size()-1];
        }
        bool t3=false;
        bool t4=false;
        bool t6=false;
        bool t7=false;
        bool t9 = false;
        bool t10=false;
        bool t12=false;
        bool t13=false;
        bool t15=false;
        bool r0 = true;
        bool r1 = true;
        bool r3 = true;
        for (target t : supervised_targets) {
            double V_target = t.get_target_speed();
            distance d_EBI = t.d_EBI;
            distance d_SBI2 = t.d_SBI2;
            distance d_SBI1 = t.d_SBI1;
            distance d_W = t.d_W;
            distance d_P = t.d_P;
            distance d_I = t.d_I;
            if (t.type == target_class::MRSP || t.type == target_class::LoA) {
                t3 |= V_target<V_est && V_est<=V_MRSP && d_I<d_maxsafefront && d_maxsafefront <=d_P;
                t4 |= V_target<V_est && V_est<=V_MRSP && d_maxsafefront > d_P;
                t6 |= V_MRSP<V_est && V_est<=V_MRSP+dV_warning(V_MRSP) && d_I<d_maxsafefront && d_maxsafefront<=d_W;
                t7 |= V_target + dV_warning(V_target) < V_est && V_est <= V_MRSP + dV_warning(V_MRSP) && d_maxsafefront > d_W;
                t9 |= V_MRSP+dV_warning(V_MRSP)<V_est && V_est <= V_MRSP + dV_warning(V_MRSP) && d_I<d_maxsafefront && d_maxsafefront <= d_SBI2;
                t10 |= V_target + dV_sbi(V_target) < V_est && V_est <= V_MRSP + dV_sbi(V_MRSP) && d_maxsafefront > d_SBI2;
                t12 |= V_MRSP + dV_sbi(V_MRSP) < V_est && V_est <= V_MRSP + dV_ebi(V_MRSP) && d_I<d_maxsafefront && d_maxsafefront <= d_EBI;
                t13 |= V_target + dV_ebi(V_target) < V_est && V_est <= V_MRSP + dV_ebi(V_MRSP) && d_maxsafefront > d_EBI;
                t15 |= V_est > V_MRSP + dV_ebi(V_MRSP) && d_maxsafefront > d_I;
                r0 &= V_est == 0;
                r1 &= V_est<=V_target;
                r3 &= V_target<V_est && V_est<=V_MRSP && d_maxsafefront<=d_P;
                V_sbi = std::min(V_sbi, t.V_SBI2);
            } else if (t.type == target_class::EoA) {
                V_sbi = std::min(V_sbi, std::max(t.V_SBI1, V_release));
            } else if (t.type == target_class::SvL) {
                V_sbi = std::min(V_sbi, std::max(t.V_SBI2, V_release));
            } else if (t.type == target_class::SR_distance) {
                t3 |= 0<V_est && V_est<=V_MRSP && d_maxsafefront>t.d_I && d_maxsafefront<=t.d_P;
                t4 |= 0<V_est && V_est<=V_MRSP && d_maxsafefront>t.d_P;
                t6 |= V_MRSP<V_est && V_est <= V_MRSP + dV_warning(V_MRSP) && d_maxsafefront>t.d_I && d_maxsafefront<=t.d_W;
                t7 |= 0<V_est && V_est <= V_MRSP + dV_warning(V_MRSP) && d_maxsafefront>t.d_W;
                t9 |= V_MRSP + dV_warning(V_MRSP) < V_est && V_est <= V_MRSP + dV_sbi(V_MRSP) && d_maxsafefront>t.d_I && d_maxsafefront<=t.d_SBI2;
                t10 |= 0<V_est && V_est <= V_MRSP + dV_sbi(V_MRSP) && d_maxsafefront>t.d_SBI2;
                t12 |= V_MRSP + dV_sbi(V_MRSP) < V_est && V_est <= V_MRSP + dV_ebi(V_MRSP) && d_maxsafefront>t.d_I && d_maxsafefront<=t.d_EBI;
                t13 |= 0<V_est && V_est <= V_MRSP + dV_ebi(V_MRSP) && d_maxsafefront>t.d_EBI;
                t15 |= V_est > V_MRSP + dV_ebi(V_MRSP) && d_maxsafefront>t.d_I;
                r0 &= V_est == 0;
                r1 &= V_est<=0;
                r3 &= 0<V_est && V_est<=V_MRSP && d_maxsafefront<t.d_P;
            }
            V_perm = std::min(V_perm, t.V_P);
        }
        
        MRDT.calculate_curves(MRDT.get_target_speed());
        V_target = MRDT.get_target_speed();
        if (MRDT.type == target_class::EoA || MRDT.type == target_class::SvL)
            D_target = std::max(std::min(EoA->get_location()-d_estfront, SvL->get_location()-d_maxsafefront), 0.0);
        else
            D_target = std::max(MRDT.d_P-d_maxsafefront, 0.0);
        
        if (EoA != nullptr && SvL != nullptr) {
            t3 |= V_release<V_est && V_est<=V_MRSP && (d_maxsafefront>tSvL.d_I || d_estfront>tEoA.d_I) && (d_maxsafefront<=tSvL.d_P && d_estfront<=tEoA.d_P);
            t4 |= V_release<V_est && V_est<=V_MRSP && (d_maxsafefront>tSvL.d_P || d_estfront > tEoA.d_P);
            t6 |= V_MRSP<V_est && V_est <= V_MRSP + dV_warning(V_MRSP) && (d_maxsafefront>tSvL.d_I || d_estfront>tEoA.d_I) && (d_maxsafefront<=tSvL.d_W && d_estfront <= tEoA.d_W);
            t7 |= V_release<V_est && V_est <= V_MRSP + dV_warning(V_MRSP) && (d_maxsafefront>tSvL.d_W || d_estfront>tEoA.d_W);
            t9 |= V_MRSP + dV_warning(V_MRSP) < V_est && V_est <= V_MRSP + dV_sbi(V_MRSP) && (d_maxsafefront>tSvL.d_I || d_estfront>tEoA.d_I) && (d_maxsafefront<=tSvL.d_SBI2 && d_estfront <= tEoA.d_SBI1);
            t10 |= V_release<V_est && V_est <= V_MRSP + dV_sbi(V_MRSP) && (d_maxsafefront>tSvL.d_SBI2 || d_estfront>tEoA.d_SBI1);
            t12 |= V_MRSP + dV_sbi(V_MRSP) < V_est && V_est <= V_MRSP + dV_ebi(V_MRSP) && (d_maxsafefront>tSvL.d_I || d_estfront>tEoA.d_I) && d_maxsafefront<=tSvL.d_EBI;
            t13 |= V_release<V_est && V_est <= V_MRSP + dV_ebi(V_MRSP) && d_maxsafefront>tSvL.d_EBI;
            t15 |= V_est > V_MRSP + dV_ebi(V_MRSP) && (d_maxsafefront>tSvL.d_I || d_estfront>tEoA.d_I);
            r0 &= V_est == 0;
            r1 &= V_est<=V_release;
            r3 &= V_release<V_est && V_est<=V_MRSP && d_estfront<tEoA.d_P && d_maxsafefront<tSvL.d_P;
        }
        if (t3 && supervision == NoS)
            supervision = IndS;
        if ((t4 || t6) && (supervision == NoS || supervision == IndS))
            supervision = OvS;
        if ((t7 || t9) && (supervision == NoS || supervision == IndS || supervision == OvS))
            supervision = WaS;
        if ((t10 || t12 || t13 || t15) && (supervision == NoS || supervision == IndS || supervision == OvS || supervision == WaS))
            supervision = IntS;
        if ((r1 || r3) && (supervision == WaS || supervision == OvS))
            supervision = IndS;
        if ((r0 || ((r1 || r3) && (!EB || Q_NVEMRRLS))) && supervision == IntS)
            supervision = IndS;
        if (t7 || t9)
            TCO = true;
        if (t10 || t12)
            SB = true;
        if (t13 || t15)
            EB= true;
        if (r0)
            EB = false;
        if (r1) {
            TCO = false;
            SB = false;
            if (Q_NVEMRRLS)
                EB = false;
        }
        if (r3) {
            TCO = false;
            SB = false;
            if (Q_NVEMRRLS)
                EB = false;
        }
    } else if (monitoring == RSM) {
        for (target t : supervised_targets) {
            V_perm = std::min(V_perm, t.V_P);
            D_target = std::max(std::min(EoA->get_location()-d_estfront, SvL->get_location()-d_maxsafefront), 0.0);
        }
        V_target = 0;
        V_sbi = std::min(V_sbi, V_release);
        bool t1 = V_est <= V_release;
        bool t2 = V_est > V_release;
        bool r0 = V_est == 0;
        bool r1 = V_est <= V_release;
        if (t1 && supervision == NoS)
            supervision = IndS;
        if (t2 && (supervision == NoS || supervision == IndS || supervision == OvS || supervision == WaS))
            supervision = IntS;
        if ((r1 && (supervision == OvS || supervision == WaS)) || (r0 && supervision == IntS))
            supervision = IndS;
        if (t2)
            EB = true;
        if (r0)
            EB = false;
    }
    V_est = (V_est + 1 > V_perm) ? (V_est-0.15) : (V_est + 0.15);
}
