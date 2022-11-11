#include "VarsConfig.h"
#include "Config.h"
#include "TexData.h"

void VarsConfig::setVar(string name, string value, float scale) {
    float maxValue = 1.f;
    if (value.find(":") != string::npos) {
        maxValue = std::stof(value.substr(value.find(":") + 1));
        value = value.substr(0, value.find(":"));
    }
    auto it = DataSourceMap.find(value);
    if (it != DataSourceMap.end()) {
        varsConfig[name] = {it->first, maxValue, scale};
    }
    else {
        ofLog() << "invalid var " + name + " type: " + value;
    }
}

void VarsConfig::setVar(string name, float value, float scale) {
    varsConfig[name] = {"const", value, scale};
}

void VarsConfig::setVar(const ofxOscMessage& m) {
    if (m.getArgType(2) == OFXOSC_TYPE_STRING) {
        setVar(m.getArgAsString(1), m.getArgAsString(2), m.getNumArgs() > 3 ? m.getArgAsFloat(3) : 1.f);
    }
    else {
        setVar(m.getArgAsString(1), m.getArgAsFloat(2), m.getArgAsFloat(3));
    }
}

void VarsHolder::updateVars(const vector<Sound> &sounds, const vector<TidalNote> &notes, const map<string, VarConfig>& maps) {
    bool tidal = false;
    for (map<string, VarConfig>::const_iterator it=maps.begin(); it!=maps.end(); ++it) {
        if (it->second.type == "tidal") {
            tidal = true;
        }
        else {
            vars.erase(it->first);
        }
    }
    if (tidal) {
        for (int i = 0; i < notes.size(); i++) {
            if (ofGetElapsedTimef() - notes[i].timeStamp < 32) {
                //float diff = ofGetElapsedTimef() - notes[i].timeStamp - notes[i].latency;
                float diff = ofGetElapsedTimef() - notes[i].timeStamp;
                if (diff > 0 && abs(diff) < 1.0 / ofGetFrameRate() && notes[i].s != "midi") {
                    int instNum = notes[i].instNum;
                    for (map<string, VarConfig>::const_iterator it=maps.begin(); it!=maps.end(); ++it) {
                        if (it->second.type == "tidal" && it->second.value == instNum) {
                            vars[it->first] += notes[i].gain;
                            onset = true;
                        }
                    }
                }
            }
        }
    }
    else {
        int i=0;
        for (map<string, VarConfig>::const_iterator it=maps.begin(); it!=maps.end(); ++it) {
            vars[it->first] = getValue(sounds, it->first, it->second);
            if (it->second.type == "amp" || it->second.type == "loud") {
                int j = it->second.value;
                if (sounds.size() > j) {
                    onset = onset || (sounds[j].onset == 1);
                    mfcc = sounds[j].mfcc;
                }
            }
            i++;
        }
    }
}

float VarsHolder::getValue(const vector<Sound> &sounds, string name, const VarConfig& var) {
    float value = 0;
    if (var.type == "amp" || var.type == "loud") {
        int j = var.value;
        if (sounds.size() > j) {
            if (var.type == "amp") {
                value = (sounds[j].amplitude / sounds[j].maxAmp);
            }
            else {
                value = (sounds[j].loudness / sounds[j].maxLoud);
            }
        }
        else {
            ofLog() << "sound data not available " + var.type + ":" + ofToString(var.value);
        }
    }
    else {
        if (var.type == "const") {
            value = var.value * var.scale;
        }
        else if (var.type == "noise") {
            int sum = 0;
            for (int i=0; i<name.size(); i++) {
                sum += name[i];
            }
            if (dynamic_cast<TexData*>(this) != NULL) {
                sum += dynamic_cast<TexData*>(this)->randomSeed;
            }
            value = ofNoise(sum, time) * var.value;
        }
        else if (var.type == "rand") {
            value = ofRandom(var.value);
        }
        else if (var.type == "sin") {
            value = abs(sin(time));
        }
    }
    return value * var.scale;
}
