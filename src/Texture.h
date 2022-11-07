#pragma once

#include "Tex.h"
#include "ofFbo.h"
#include "ofxOsc.h"
#include "ofxLooper.h"

class Texture {
public:
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
    }
    void setNumFrames(int value) {
        numFrames = value;
        frames.resize(numFrames);
    };
    void setFboSettings(const ofxOscMessage& m) {
        std::unordered_map<string, function<void(int, ofFbo::Settings&)>> mapping;
        mapping["numColorbuffers"] = [](int v, ofFbo::Settings& a) { a.numColorbuffers = v; };
        mapping["useDepth"] = [](int v, ofFbo::Settings& a) { a.useDepth = v; };
        mapping["internalformat"] = [](int v, ofFbo::Settings& a) { a.internalformat = v; };
        mapping["textureTarget"] = [](int v, ofFbo::Settings& a) { a.textureTarget = v; };
        mapping["wrapModeHorizontal"] = [](int v, ofFbo::Settings& a) { a.wrapModeHorizontal = v; };
        mapping["wrapModeVertical"] = [](int v, ofFbo::Settings& a) { a.wrapModeVertical = v; };
        mapping["minFilter"] = [](int v, ofFbo::Settings& a) { a.minFilter = v; };
        mapping["maxFilter"] = [](int v, ofFbo::Settings& a) { a.maxFilter = v; };
        string key = m.getArgAsString(1);
        int value = m.getArgAsInt(2);
        if (mapping.find(key) == mapping.end()) {
            mapping[key](value, fboSettings);
        }
    }
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
    
private:
    void drawFrame();
    const ofFbo& getFrame(int delay = 0) const;
    
    ofxLooper* looper = NULL;
};
