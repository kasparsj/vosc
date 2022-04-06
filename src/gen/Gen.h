#pragma once

#include "LayerData.h"
#include "Config.h"
#include "ofMain.h"

class Layer;

class Gen {
public:
    Gen(string name) : name(name) {}
    virtual void update(Layer *layer, const Config &config) = 0;
    virtual void draw(int left, int top, int width, int height) = 0;
    virtual void choose() = 0;
    virtual void reload() {};
    virtual void clear() {};
    virtual void reset() {};

    string name = "";
    string prevName = "";
};
