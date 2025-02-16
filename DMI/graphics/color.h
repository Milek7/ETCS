/*
 * European Train Control System
 * Copyright (C) 2019-2023  César Benito <cesarbema2009@hotmail.com>
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef _COLOR_H
#define _COLOR_H
struct Color
{
    int R;
    int G;
    int B;
    bool operator==(Color b)
    {
        return R == b.R && G==b.G && B==b.B;
    }
    bool operator!=(Color b)
    {
        return !(*this==b);
    }
};
const Color White = {255,255,255};
const Color Black = {0,0,0};
const Color Grey = {195,195,195};
const Color MediumGrey = {150,150,150};
const Color DarkGrey = {85,85,85};
const Color Yellow = {223,223,0};
const Color Shadow = {8, 24, 57};
const Color Red = {191,0,2};
const Color Orange = {234,145,0};
const Color DarkBlue = {3, 17, 34};
const Color PASPdark = {33, 49, 74};
const Color PASPlight = {41, 74, 107};
const Color Blue = {0, 0, 234};
const Color Green = {0, 234, 0};
const Color LightRed = {255, 96, 96};
const Color LightGreen = {96, 255, 96};
#endif