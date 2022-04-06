#pragma once

#include "LayerData.h"
#include "Config.h"
#include "ofMain.h"

class Gen {
public:
    Gen(string name) : name(name) {}
    virtual void update(LayerData *data, Config &config) = 0;
    virtual void draw(int left, int top, int width, int height) = 0;
    virtual void choose() = 0;
    virtual void reload() {};
    virtual void clear() {};
    virtual void reset() {};

    string name = "";
    string prevName = "";
    int randomSeed = 0;
    float bri = 1.0;
    float alpha = 1.0;
    float timeNorm = 0;
    bool noClear;
};
