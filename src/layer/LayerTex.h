#pragma once

#include "Tex.h"

class LayerTex {
public:
    static Tex* factory(string type, string name);
    static Tex* factory(string source);
    
    ~LayerTex() {
        unload();
    }
    void load(string source);
    void choose(string type = "");
    void reload();
    void unload();
    void clear();
    void update();
    void drawToFbo();
    void draw();
    void reset();
    bool isFrameNew() const {
        return gen->isFrameNew();
    }
    const ofFbo& getFbo() const;
    bool hasTexture() const {
        const ofFbo& fbo = getFbo();
        return fbo.isAllocated() && fbo.getTexture().isAllocated();
    }
    ofPixels& getPixels() const;
    bool isLoaded() const {
        return gen != NULL;
    }
    ofFloatColor getTint() const {
        return gen->getTint(layer);
    }

    Layer* layer = NULL;
    Tex* gen = NULL;
    vector<ofFbo> frames;
    int curFbo = -1;
};
