#pragma once

#include "ofMain.h"

class Layer;
class Texture;
class TexData;

class Tex : public ofBaseHasTexture, public ofBaseHasPixels {
public:
    static Tex* factory(string type, string name, const vector<float>& args);
    static Tex* factory(string source, const vector<float>& args);

    Tex(string path, const vector<float>& args) : path(path), args(args) {}
    virtual ~Tex() = default;
    virtual void update(TexData& data) = 0;
    virtual void draw(const glm::vec2 &pos, const glm::vec2 &size) = 0;
    virtual void choose() = 0;
    virtual void reload() {};
    virtual void clear() {};
    virtual void reset() {};
    virtual bool isFrameNew() {
        return true;
    }
    void setUseTexture(bool bUseTex) override {
        
    }
    bool isUsingTexture() const override {
        return false;
    }
    virtual ofPixels& getPixels() override {
        return pixels;
    }
    virtual const ofPixels& getPixels() const override {
        return pixels;
    }
    string path = "";
    string prevPath = "";
    vector<float> args;
    bool aspectRatio;
    ofPixels pixels;
};
