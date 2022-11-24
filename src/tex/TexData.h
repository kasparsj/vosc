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
    
//    ofFloatColor getMFCCColor() {
//        if (mfccColor == ofFloatColor(0)) {
//            vector<float> mfcc = getVarOfTypeVec("mfcc");
//            int k = (mfcc.size() / 3);
//            for (int i=0; i<mfcc.size(); i++) {
//                int j = MIN(2, i / k);
//                int k2 = j < 2 ? k : mfcc.size() - k*2;
//                mfccColor[j] += (mfcc[i] * 1.f/k2);
//            }
//        }
//        return mfccColor;
//    }
    
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
    void afterDraw(const map<string, Variable*>& vars);
    const map<string, Variable*>& getVars() {
        return parent->vars;
    }
    bool hasVar(string name) {
        return parent->hasVar(name);
    }
    float getVar(string name, int idx = 0) {
        return parent->getVar(name, idx);
    }
    vector<float> getVarVec(string name) {
        Variable* var = parent->getVariable(name);
        if (var != NULL) {
            return var->values;
        }
        return vector<float>();
    }
    ofFloatColor getVarColor(string name) {
        return parent->getVarColor(name);
    }
    float getVarOfType(string type, int idx = 0) {
        Variable* var = parent->getVariableOfType(type);
        if (var != NULL) {
            return var->values[idx];
        }
        return 0;
    }
    vector<float> getVarOfTypeVec(string type) {
        Variable* var = parent->getVariableOfType(type);
        if (var != NULL) {
            return var->values;
        }
        return vector<float>();
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
