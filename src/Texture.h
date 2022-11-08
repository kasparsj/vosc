#pragma once

#include "Tex.h"
#include "ofFbo.h"
#include "ofxOsc.h"
#include "ofxLooper.h"

class Texture {
public:
    static map<string, Texture> pool;
    static Texture& getFromPool(string which, bool create = false);
    static Tex* factory(string type, string name, const vector<float>& args);
    static Tex* factory(string source, const vector<float>& args);
    
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
    void update(Layer* layer);
    void draw(Layer* layer);
    void reset();
    bool isFrameNew() const {
        return tex->isFrameNew();
    }
    glm::vec2 getSize() const {
        return glm::vec2(fboSettings.width, fboSettings.height);
    }
    void setSize(const ofxOscMessage& m) {
        fboSettings.width = m.getArgAsFloat(1);
        fboSettings.height = m.getArgAsFloat(2);
        // todo: reallocate?
    }
    void setNumFrames(int value);
    void setFboSettings(const ofxOscMessage& m);
    void allocate(ofFbo& fbo);
    const ofTexture& getTexture(int delay = 0) const;
    bool hasTexture(int delay = 0) const;
    ofPixels& getPixels() const;
    bool isLoaded() const {
        return tex != NULL;
    }
    void setLooper(const ofxOscMessage& m);

    Tex* tex = NULL;
    bool noClear;
    int numFrames = 1;
    ofFbo::Settings fboSettings;
    vector<ofFbo> frames;
    int curFbo = -1;
    ofBlendMode blendMode = OF_BLENDMODE_ALPHA;
    bool aspectRatio = true;
    
private:
    void drawFrame();
    const ofFbo& getFrame(int delay = 0) const;
    
    ofxLooper* looper = NULL;
};
