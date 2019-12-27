#include "targets.h"
#include "curve_calc.h"
#include "speed_profile.h"
#include "national_values.h"
#include <set>
#include <iostream>
distance target::get_distance_curve(double velocity) const
{
    /*if (type == target_class::MRSP) {
        distance a = distance_curve(A_safe, d_target, V_target+dV_ebi(V_target), velocity).get();
        double v = V_target+dV_ebi(V_target);
        distance x = d_target;
        bool inc = velocity > v;
        double dt = 0.01;
        while(inc ? (v < velocity) : (v > velocity)) {
            double pv = v;
            v += (inc ? 1 : -1)*A_safe.accel(v,x)*dt;
            x += (inc ? -1 : 1)*dt*0.5*(pv + v + A_safe.accel(pv,x)*dt);
        }
        double diff = std::abs(x-a);
        if (diff > 2)
            std::cout<<diff<<std::endl;
        return a;
    }*/
    if (type == target_class::MRSP || type == target_class::LoA)
        return distance_curve(A_safe, d_target, V_target+dV_ebi(V_target), velocity);
    else if (type == target_class::SvL || type == target_class::SR_distance)
        return distance_curve(A_safe, d_target, 0, velocity);
    else if (type == target_class::EoA)
        return distance_curve(A_expected, d_target, 0, velocity);
    else
        return distance(0);
}
double target::get_speed_curve(distance dist) const
{
    if (type == target_class::MRSP || type == target_class::LoA)
        return speed_curve(A_safe, d_target, V_target+dV_ebi(V_target), dist);
    else if (type == target_class::SvL || type == target_class::SR_distance)
        return speed_curve(A_safe, d_target, 0, dist);
    else if (type == target_class::EoA)
        return speed_curve(A_expected, d_target, 0, dist);
    else return 0;
}
distance target::get_distance_gui_curve(double velocity) const
{
    distance guifoot(0);
    if (type == target_class::EoA || type == target_class::SvL) {
        guifoot = d_target;
    } else {
        double V_delta0t = 0;
        distance debi = get_distance_curve(V_target+V_delta0t)-(V_target+V_delta0t)*(T_berem+T_traction);
        guifoot = debi-V_target*(T_driver+T_bs2);
    }
    return distance_curve(A_normal_service, guifoot, V_target, velocity);
}
double target::get_speed_gui_curve(distance dist) const
{
    distance guifoot(0);
    if (type == target_class::EoA || type == target_class::SvL) {
        guifoot = d_target;
    } else {
        double V_delta0t = 0;
        distance debi = get_distance_curve(V_target+V_delta0t)-(V_target+V_delta0t)*(T_berem+T_traction);
        guifoot = debi-V_target*(T_driver+T_bs2);
    }
    return speed_curve(A_normal_service, guifoot, V_target, dist);
}
void target::calculate_curves(double V_est) const
{
    if (is_EBD_based()) {
        double V_delta0 = Q_NVINHSMICPERM ? 0 : V_ura;
        double V_delta1 = A_est1*T_traction;
        double V_delta2 = A_est2*T_berem;
        double V_bec = std::max(V_est+V_delta0+V_delta1, V_target)+V_delta2;
        double D_bec = std::max(V_est+V_delta0+V_delta1/2, V_target)*T_traction + (std::max(V_est+V_delta0+V_delta1, V_target)+V_delta2/2)*T_berem;
        d_EBI = get_distance_curve(V_bec)-D_bec;
        d_SBI2 = d_EBI - V_est*T_bs2;
        d_W = d_SBI2 - V_est*T_warning;
        d_P = d_SBI2 - V_est*T_driver;
        double T_indication = std::max(0.8*T_bs, 5.0) + T_driver;
        d_I = d_P - T_indication*V_est;
        
        double D_be_display = (V_est+V_delta0+V_delta1/2)*T_traction + (V_est + V_delta0 + V_delta1 + V_delta2/2)*T_berem;
        distance v_sbi_dappr = d_maxsafefront + V_est*T_bs2 + D_be_display;
        V_SBI2 = v_sbi_dappr < get_distance_curve(V_target) ? (get_speed_curve(v_sbi_dappr)-(V_delta0+V_delta1+V_delta2),V_target + dV_sbi(V_target)) : (V_target + dV_sbi(V_target));
        
        //GUI disabled
        distance v_p_dappr = d_maxsafefront + V_est*(T_driver+T_bs2) + D_be_display;
        V_P = v_p_dappr < get_distance_curve(V_target) ? std::max(get_speed_curve(v_p_dappr) - (V_delta0+V_delta1+V_delta2), V_target) : V_target;
    } else {
        d_SBI1 = get_distance_curve(V_est) - T_bs1*V_est;
        d_W = d_SBI1 - T_warning*V_est;
        d_P = d_SBI1 - T_driver*V_est;
        double T_indication = std::max(0.8*T_bs, 5.0) + T_driver;
        d_I = d_P - T_indication*V_est;
        
        distance v_sbi_dappr = d_estfront + V_est*T_bs1;
        V_SBI1 = v_sbi_dappr < d_target ? get_speed_curve(v_sbi_dappr) : 0;
            
        //GUI disabled
        distance v_p_dappr = d_estfront + V_est*(T_driver + T_bs1);
        V_P = v_p_dappr < d_target ? get_speed_curve(v_p_dappr) : 0;
    }
}
EndOfAuthority *EoA = new EndOfAuthority();
SupervisionLimit *SvL = new SupervisionLimit();
std::set<target> supervised_targets;
bool changed = false;
void set_supervised_targets()
{
    changed = true;
    supervised_targets.clear();
    std::map<distance, double> MRSP = mrsp_candidates.get_MRSP();
    auto minMRSP = --MRSP.upper_bound(d_maxsafefront);
    auto prev = minMRSP;
    for (auto it=++minMRSP; it!=MRSP.end(); ++it) {
        if (it->second < prev->second)
            supervised_targets.insert(target(it->first, it->second, target_class::MRSP));
        prev = it;
    }
    target t1 = target(SvL->get_location(), 0, target_class::SvL);
    target t2 = target(EoA->get_location(), 0, target_class::EoA);
    if (SvL != nullptr)
        supervised_targets.insert(target(SvL->get_location(), 0, target_class::SvL));
    if (EoA != nullptr)
        supervised_targets.insert(target(EoA->get_location(), 0, target_class::EoA));
    /*if (LoA != nullptr)
        supervised_targets.insert(target(LoA->pos, LoA->speed, target_class::LoA));
    if (SRdist != nullptr)
        supervised_targets.insert(SRdist->distance, 0, target_class::SR_distance);*/
}
bool supervised_targets_changed()
{
    std::set<target> old;
    for (auto it = supervised_targets.begin(); it!=supervised_targets.end(); ++it) {
        if (it->type == target_class::MRSP && d_maxsafefront >= it->get_target_position())
            old.insert(*it);
    }
    for (auto it = old.begin(); it!=old.end(); ++it)
        supervised_targets.erase(*it);
    if (changed || !old.empty()) {
        changed = false;
        return true;
    }
    return false;
}
std::set<target> get_supervised_targets()
{
    return supervised_targets;
}
