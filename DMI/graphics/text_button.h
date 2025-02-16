/*
 * European Train Control System
 * Copyright (C) 2019-2023  César Benito <cesarbema2009@hotmail.com>
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef _TEXT_BUTTON_H
#define _TEXT_BUTTON_H
#include "button.h"
class TextButton : public Button
{
    text_graphic *enabled_text=nullptr;
    text_graphic *disabled_text=nullptr;
    int size;
    public:
    std::string caption;
    Color disabledColor = DarkGrey;
    void paint() override;
    void rename(std::string name)
    {
        caption = name;
        enabled_text = disabled_text = nullptr;
    }
    TextButton(std::string text, float sx, float sy, std::function<void()> pressed = nullptr, int size = 12);
    ~TextButton();
};
#endif
