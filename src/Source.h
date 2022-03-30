#pragma once

#include "Config.h"
#include "ofMain.h"

class Source {
public:
    bool isEnabled() const {
        return name != "";
    }
    virtual void update(const Config &config);
    virtual void draw(int left, int top, int width, int height) = 0;

    string name = "";
    float time = 0;
};
