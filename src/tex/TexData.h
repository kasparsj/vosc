#pragma once

#include "VarsHolder.h"

class TexData {
public:
    TexData() {
        randomSeed = ofRandom(1000);
    }
    
    void setup(VarsHolder* parent) {
        this->parent = parent;
    }
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes);
    
    glm::vec2 getSize() const {
        return size;
    }
    void setSize(float w, float h) {
        size.x = w;
        size.y = h;
        fboSettings.width = size.x;
        fboSettings.height = size.y;
    }
    void setSize(const ofxOscMessage& m) {
        setSize(m.getArgAsFloat(1), m.getArgAsFloat(2));
    }
    void set(const ofxOscMessage& m);
    ofFbo::Settings& getFboSettings() {
        return fboSettings;
    }
    void setFboSettings(const ofxOscMessage& m);
    void allocate(ofFbo& fbo);
    void afterDraw(const map<string, shared_ptr<BaseVar>>& vars);
    const map<string, shared_ptr<BaseVar>>& getVars() {
        return parent->vars;
    }
    bool hasVar(string name) {
        return parent->hasVar(name);
    }
    float getVar(string name, int idx = 0) {
        return parent->getVar(name, idx);
    }
    vector<float> getVarVec(string name) {
        return parent->getVarVec(name);
    }
    ofFloatColor getVarColor(string name) {
        return parent->getVarColor(name);
    }
    
    glm::vec3 size;
    float time = 0;
    float prevTime = 0;
    bool noClear;
    ofBlendMode blendMode = OF_BLENDMODE_ALPHA;
    bool aspectRatio = true;
    float timePct = 0;
    int randomSeed = 0;
    
protected:
    VarsHolder* parent;
    ofFbo::Settings fboSettings;

};
