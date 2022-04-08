#pragma once

#include "LayerData.h"
#include "ofMain.h"

class Layer;

class Gen {
public:
    Gen(string path) : path(path) {}
    virtual ~Gen() = default;
    virtual void update(Layer *layer) = 0;
    virtual void draw(int left, int top, int width, int height) = 0;
    virtual void choose() = 0;
    virtual void reload() {};
    virtual void clear() {};
    virtual void reset() {};

    string path = "";
    string prevPath = "";
    bool aspectRatio;
};
