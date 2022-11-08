#pragma once

#include "ofMain.h"
#include "ofxOsc.h"

class TexData {
public:
    TexData() {
        randomSeed = ofRandom(1000);
    }
    
    void update();
    
    ofFloatColor getColor() {
        if (useMFCCColor && mfcc.size() > 0) {
            return getMFCCColor();
        }
        return color;
    }
    
    ofFloatColor getTint() {
        const ofFloatColor& tint = getColor();
        if (tint == ofFloatColor(0, 0)) {
            return ofFloatColor(1);
        }
        return tint;
    }
    
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
    ofFbo::Settings& getFboSettings() {
        return fboSettings;
    }
    void setFboSettings(const ofxOscMessage& m);
    void allocate(ofFbo& fbo);
    
    void reset() {
        onset = false;
        values.clear();
        mfcc.clear();
        mfccColor = ofFloatColor(0);
        speed = 1.f;
        color = ofFloatColor(0, 0);
    }
    
    glm::vec2 size;
    float time = 0;
    float prevTime = 0;
    float speed = 1.f;
    bool noClear;
    ofBlendMode blendMode = OF_BLENDMODE_ALPHA;
    bool aspectRatio = true;
    float timePct = 0;
    int randomSeed = 0;
    ofFloatColor color = ofFloatColor(0, 0);
    bool useMFCCColor = false;
    bool useRandomColor = false;
    bool onset;
    vector<float> mfcc;
    ofFloatColor mfccColor = ofFloatColor(0);
    vector<float> values;
    
private:
    ofFbo::Settings fboSettings;

};
