#pragma once

#include "LayerData.h"
#include "ofMain.h"

class Layer;

class Tex {
public:
    Tex(string path, const vector<float>& args) : path(path), args(args) {}
    virtual ~Tex() = default;
    virtual void update(Layer *layer) = 0;
    virtual void draw(const glm::vec3 &pos, const glm::vec3 &size) = 0;
    virtual void choose() = 0;
    virtual void reload() {};
    virtual void clear() {};
    virtual void reset() {};
    virtual ofFloatColor getTint(Layer *layer) const;
    virtual bool isFrameNew() {
        return true;
    }
    virtual ofPixels& getPixels() = 0;

    string path = "";
    string prevPath = "";
    vector<float> args;
    bool aspectRatio;
};
