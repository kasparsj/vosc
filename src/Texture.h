#pragma once

#include "TexData.h"
#include "tex/BaseTex.h"
#include "ofFbo.h"
#include "ofxLooper.h"

class Texture : public VarsHolder {
public:
    Texture(shared_ptr<BaseTex> tex) : VarsHolder(), tex(tex) {
        data.setup(this);
        reset();
    }
    Texture() : Texture(NULL) {}
    ~Texture() {
        unload();
    }
    void load(string source, const vector<float>& args);
    void load(const ofxOscMessage& m, int arg = 1);
    void loadData(const ofxOscMessage& m, int arg = 1);
    void choose(const ofxOscMessage& m);
    void reload();
    void unload();
    void clear();
    void update(const vector<TidalNote> &notes);
    void oscCommand(const string& command, const ofxOscMessage &m);
    void draw(Layer* layer);
    void texDraw(const glm::vec2& pos, const glm::vec2 size);
    void reset();
    bool isFrameNew() const {
        return tex->isFrameNew();
    }
    void setNumFrames(int value);
    int getNumTextures(int delay = 0) const;
    const ofTexture& getTexture(int delay = 0, int att = 0) const;
    ofTexture& getTexture(int delay = 0, int att = 0);
    bool hasTexture(int delay = 0) const;
    ofPixels& getPixels() const;
    bool isLoaded() const {
        return tex != NULL;
    }
    void setLooper(const ofxOscMessage& m);

    shared_ptr<BaseTex> tex = NULL;
    TexData data;
    int numFrames = 1;
    vector<ofFbo> frames;
    int curFbo = -1;
    
private:
    static shared_ptr<BaseTex> chooseTex(const ofxOscMessage& m);
    static shared_ptr<BaseTex> chooseTex(string type, const vector<float>& args);
    static shared_ptr<BaseTex> chooseTex() {
        vector<float> args;
        return chooseTex("", args);
    }

    void _unload();
    void drawFrame();
    const ofFbo& getFrame(int delay = 0) const;
    ofFbo& getFrame(int delay = 0);
    
    ofxLooper* looper = NULL;
};
