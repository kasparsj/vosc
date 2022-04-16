#pragma once

#include "LayerData.h"
#include "ofMain.h"

class Layer;

class Gen {
public:
    Gen(string path) : path(path) {}
    virtual ~Gen() = default;
    virtual void update(Layer *layer) = 0;
    virtual void draw(const glm::vec3 &pos, const glm::vec3 &size) = 0;
    virtual void choose() = 0;
    virtual void reload() {};
    virtual void clear() {};
    virtual void reset() {};
    virtual ofFloatColor getTint(Layer *layer) {
        return ofFloatColor(1);
    }

    string path = "";
    string prevPath = "";
    bool aspectRatio;
    ofFloatColor color;
};
