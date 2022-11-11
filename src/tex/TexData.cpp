#include "TexData.h"

void TexDataParent::setVar(string name, string value, float scale) {
    float maxValue = 1.f;
    if (value.find(":") != string::npos) {
        maxValue = std::stof(value.substr(value.find(":") + 1));
        value = value.substr(0, value.find(":"));
    }
    auto it = DataSourceMap.find(value);
    if (it != DataSourceMap.end()) {
        vars[name] = {it->first, maxValue, scale};
    }
    else {
        ofLog() << "invalid var " + name + " type: " + value;
    }
}

void TexDataParent::setVar(string name, float value, float scale) {
    vars[name] = {"const", value, scale};
}

void TexDataParent::setVar(const ofxOscMessage& m) {
    if (m.getArgType(2) == OFXOSC_TYPE_STRING) {
        setVar(m.getArgAsString(1), m.getArgAsString(2), m.getNumArgs() > 3 ? m.getArgAsFloat(3) : 1.f);
    }
    else {
        setVar(m.getArgAsString(1), m.getArgAsFloat(2), m.getArgAsFloat(3));
    }
}

void TexData::update(const vector<Sound> &sounds, const vector<TidalNote> &notes, const map<string, LayerVar>& maps) {
    const float timef = ofGetElapsedTimef();
    time += ((timef - prevTime) * speed);
    prevTime = timef;
    onset = false;
    bool tidal = false;
    for (map<string, LayerVar>::const_iterator it=maps.begin(); it!=maps.end(); ++it) {
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
                    for (map<string, LayerVar>::const_iterator it=maps.begin(); it!=maps.end(); ++it) {
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
        for (map<string, LayerVar>::const_iterator it=maps.begin(); it!=maps.end(); ++it) {
            vars[it->first] = getValue(sounds, it->first, it->second);
            if (it->second.type == "amp" || it->second.type == "lound") {
                int j = it->second.value;
                onset = onset || (sounds[j].onset == 1);
                mfcc = sounds[j].mfcc;
            }
            i++;
        }
    }
}

void TexData::set(const ofxOscMessage& m) {
    string prop = m.getArgAsString(1);
    if (prop == "noClear") {
        noClear = m.getArgAsBool(2);
    }
    else if (prop == "blendMode") {
        blendMode = static_cast<ofBlendMode>(m.getArgAsInt(2));
    }
    else if (prop == "aspectRatio") {
        aspectRatio = m.getArgAsBool(2);
    }
}

void TexData::setFboSettings(const ofxOscMessage &m) {
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

void TexData::allocate(ofFbo& fbo) {
    ofDisableTextureEdgeHack();
    fbo.allocate(fboSettings);
    ofEnableTextureEdgeHack();
}

float TexData::getValue(const vector<Sound> &sounds, string name, const LayerVar& var) {
    float value;
    if (var.type == "amp" || var.type == "loud") {
        int j;
        if (var.type == "amp") {
            j = var.value;
            value = (sounds[j].amplitude / sounds[j].maxAmp);
        }
        else {
            j = var.value;
            value = (sounds[j].loudness / sounds[j].maxLoud);
        }
    }
    else {
        if (var.type == "const") {
            value = var.value * var.scale;
        }
        else if (var.type == "noise") {
            value = ofNoise(std::stoi(name), time) * var.value;
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
