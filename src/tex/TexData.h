#pragma once

#include "VarsConfig.h"

class TexData : public VarsHolder {
public:
    TexData() {
        randomSeed = ofRandom(1000);
    }
    
    void setup(VarsConfig* parent) {
        this->parent = parent;
    }
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes, const map<string, VarConfig>& maps);
    
    ofFloatColor getColor() {
        if (useMFCCColor && mfcc.size() > 0) {
            return getMFCCColor();
        }
        return color;
    }
    void setColor(const ofxOscMessage& m);
    
    ofFloatColor getTint() {
        if (useMFCCTint && mfcc.size() > 0) {
            return getMFCCColor();
        }
        return tint;
    }
    void setTint(const ofxOscMessage& m);
    
    ofFloatColor getMFCCColor() {
        if (mfccColor == ofFloatColor(0)) {
            int k = (mfcc.size() / 3);
            for (int i=0; i<mfcc.size(); i++) {
                int j = MIN(2, i / k);
                int k2 = j < 2 ? k : mfcc.size() - k*2;
                mfccColor[j] += (mfcc[i] * 1.f/k2);
            }
        }
        return mfccColor;
    }
    
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
    void reset() {
        onset = false;
        vars.clear();
        mfcc.clear();
        mfccColor = ofFloatColor(0);
        speed = 1.f;
        color = ofFloatColor(0, 0);
    }
    void afterDraw(const map<string, VarConfig>& maps);
    
    glm::vec3 size;
    float prevTime = 0;
    float speed = 1.f;
    bool noClear;
    ofBlendMode blendMode = OF_BLENDMODE_ALPHA;
    bool aspectRatio = true;
    float timePct = 0;
    int randomSeed = 0;
    ofFloatColor color = ofFloatColor(1.f, 1.f);
    bool useMFCCColor = false;
    bool useRandomColor = false;
    ofFloatColor tint = ofFloatColor(1.f, 1.f);
    bool useMFCCTint = false;
    ofFloatColor mfccColor = ofFloatColor(0);
    
protected:
    VarsConfig* parent;
    ofFbo::Settings fboSettings;

};
