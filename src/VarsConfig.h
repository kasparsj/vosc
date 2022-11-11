#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "Sound.h"
#include "ofxTidalCycles.h"

struct VarConfig {
    string type;
    float value;
    float scale = 1.f;
};
typedef struct VarConfig VarConfig;

class VarsConfig {
public:
    void setVar(string name, string value, float scale = 1.f);
    void setVar(string name, float value, float scale = 1.f);
    virtual void setVar(const ofxOscMessage& m);

    map<string, VarConfig> varsConfig;
};

class VarsHolder {
public:
    virtual ~VarsHolder() {}
    void updateVars(const vector<Sound> &sounds, const vector<TidalNote> &notes, const map<string, VarConfig>& maps);
    bool hasVar(string name) {
        return vars.find(name) != vars.end();
    }
    float getVar(string name) {
        return vars[name];
    }

    float time = 0;
    map<string, float> vars;
    bool onset;
    vector<float> mfcc;
    
protected:
    float getValue(const vector<Sound> &sounds, string name, const VarConfig& var);
};
