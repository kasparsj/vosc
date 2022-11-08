#pragma once

#include "TexData.h"
#include "Tex.h"
#include "ofFbo.h"
#include "ofxLooper.h"

class Texture {
public:
    ~Texture() {
        unload();
    }
    void load(string source, const vector<float>& args);
    void load(const ofxOscMessage& m, int arg = 1);
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
    void draw(Layer* layer);
    void reset();
    bool isFrameNew() const {
        return tex->isFrameNew();
    }
    void setNumFrames(int value);
    const ofTexture& getTexture(int delay = 0) const;
    ofTexture& getTexture(int delay = 0);
    bool hasTexture(int delay = 0) const;
    ofPixels& getPixels() const;
    bool isLoaded() const {
        return tex != NULL;
    }
    void setLooper(const ofxOscMessage& m);

    Tex* tex = NULL;
    TexData data;
    int numFrames = 1;
    vector<ofFbo> frames;
    int curFbo = -1;
    
private:
    void drawFrame();
    const ofFbo& getFrame(int delay = 0) const;
    ofFbo& getFrame(int delay = 0);
    
    ofxLooper* looper = NULL;
};
