/*
 * European Train Control System
 * Copyright (C) 2019-2023  César Benito <cesarbema2009@hotmail.com>
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "vbc.h"
std::set<virtual_balise_cover> vbcs;
#include <fstream>
void write_vbcs()
{
    std::ofstream file("vbcs.dat");
    for (auto vbc : vbcs) {
        file<<vbc.NID_C<<" "<<vbc.NID_VBCMK<<" "<<vbc.validity<<"\n";
    }
}
void load_vbcs()
{
    std::ifstream file("vbcs.dat");
    while (!file.eof()) {
        int nid_c, nid_vbcmk;
        int64_t validity;
        file>>nid_c;
        file>>nid_vbcmk;
        file>>validity;
        if (file.fail()) break;
        if (validity > get_milliseconds())
            vbcs.insert({nid_c, nid_vbcmk, validity});
    }
}
void set_vbc(virtual_balise_cover vbc)
{
    vbcs.insert(vbc);
    write_vbcs();
}
void remove_vbc(virtual_balise_cover vbc)
{
    vbcs.erase(vbc);
    write_vbcs();
}
bool vbc_ignored(int nid_c, int nid_vbcmk)
{
    auto it = vbcs.find({nid_c, nid_vbcmk, 0});
    return it != vbcs.end() && it->validity > get_milliseconds() && it->NID_C == nid_c;
}