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
    void update(const vector<OSCInput> &sounds, const vector<TidalNote> &notes);
    void oscCommand(const string& command, const ofxOscMessage &m);
    
    glm::vec2 getSize() const {
        return getVarVec3("size");
    }
    void setSize(float w, float h) {
        parent->setVar("size", glm::vec3(w, h, 0));
        fboSettings.width = w;
        fboSettings.height = h;
    }
    void setSize(glm::vec2 size) {
        setSize(size.x, size.y);
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
    const map<string, shared_ptr<BaseVar>>& getVars() const {
        return parent->vars;
    }
    bool hasVar(const string& name) const {
        return parent->hasVar(name);
    }
    float getVar(const string& name, int idx = 0) const {
        return parent->getVar(name, idx);
    }
    float getVarPercent(const string& name, int idx = 0) const {
        return parent->getVarPercent(name, idx);
    }
    const vector<float>& getVarVec(const string& name) const {
        return parent->getVarVec(name);
    }
    glm::vec3 getVarVec3(const string& name) const {
        return parent->getVarVec3(name);
    }
    ofFloatColor getVarColor(const string& name) const {
        return parent->getVarColor(name);
    }
    
    float time = 0;
    float prevTime = 0;
    bool noClear = false;
    ofBlendMode blendMode = OF_BLENDMODE_ALPHA;
    bool aspectRatio = true;
    int randomSeed = 0;
    
protected:
    VarsHolder* parent;
    ofFbo::Settings fboSettings;

};
