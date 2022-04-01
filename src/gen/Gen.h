#pragma once

#include "VisualData.h"
#include "Config.h"
#include "ofMain.h"

class Gen {
public:
    bool isEnabled() const {
        return name != "";
    }
    virtual void update(VisualData *data, Config &config) = 0;
    virtual void draw(int left, int top, int width, int height) = 0;
    virtual void choose() = 0;

    string name = "";
    string prevName = "";
    int random = 0;
};
