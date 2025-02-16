/*
 * European Train Control System
 * Copyright (C) 2019-2023  César Benito <cesarbema2009@hotmail.com>
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef _DATA_VIEW_H
#define _DATA_VIEW_H
#include "subwindow.h"
#include "../monitor.h"
#include <vector>
class data_view_window : public subwindow
{
    protected:
    std::vector<std::pair<std::string, std::string>> data;
    std::vector<Component*> components;
    void setLayout() override
    {
        clearLayout();
        for (int i=(current_page-1)*10; i<components.size()/2 && i<current_page*10; i++)
        {
            if (components[2*i] == nullptr) continue;
            addToLayout(components[2*i], new RelativeAlignment(nullptr, 320+104, 15+62+(i%10)*16, 0));
            addToLayout(components[2*i+1], new RelativeAlignment(nullptr, 320+204, 15+62+(i%10)*16, 0));
        }
        subwindow::setLayout();
    }
    public:
    data_view_window(std::string title, std::vector<std::pair<std::string, std::string>> data) : subwindow(title, false, (data.size()-1)/10 + 1), data(data)
    {
        for (int i=0; i<data.size(); i++)
        {
            int rows = data[i].first.find('\n') != std::string::npos ? 2 : 1;
            Component *c1 = new Component(100,16*rows);
            Component *c2 = new Component(100,16);
            c1->addText(data[i].first, 5, 0, 12, White, RIGHT);
            c2->addText(data[i].second, 4, 0, 12, White, LEFT);
            components.push_back(c1);
            components.push_back(c2);
            for (int j=0; j<(rows-2)*2; j++) components.push_back(nullptr);
        }
        page_count = (components.size()/2-1)/10 + 1;
        next_button.enabled = page_count > 1;
        updateTitle();
        setLayout();
    }
    ~data_view_window()
    {
        for (auto c : components)
        {
            if (c != nullptr) delete c;
        }
    }
};
#endif