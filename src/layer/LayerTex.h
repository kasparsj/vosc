#pragma once

#include "Tex.h"
#include "ofxOsc.h"

class LayerTex {
public:
    static Tex* factory(string type, string name, const vector<float>& args);
    static Tex* factory(string source, const vector<float>& args);
    
    ~LayerTex() {
        unload();
    }
    void load(string source, const vector<float>& args);
    void load(const ofxOscMessage& m);
    void choose(string type, const vector<float>& args);
    void choose() {
        vector<float> args;
        choose("", args);
    }
    void choose(const ofxOscMessage& m);
    void reload();
    void unload();
    void clear();
    void update();
    void drawToFbo();
    void draw();
    void reset();
    bool isFrameNew() const {
        return tex->isFrameNew();
    }
    const ofFbo& getFbo() const;
    bool hasTexture() const {
        const ofFbo& fbo = getFbo();
        return fbo.isAllocated() && fbo.getTexture().isAllocated();
    }
    ofPixels& getPixels() const;
    bool isLoaded() const {
        return tex != NULL;
    }
    ofFloatColor getTint() const {
        return tex->getTint(layer);
    }

    Layer* layer = NULL;
    Tex* tex = NULL;
    vector<ofFbo> frames;
    int curFbo = -1;
};
