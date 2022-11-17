#include "TexData.h"
#include "Args.h"
#include "ColorUtil.h"

void TexData::update(const vector<Sound> &sounds, const vector<TidalNote> &notes, const map<string, VarConfig>& maps) {
    const float timef = ofGetElapsedTimef();
    time += ((timef - prevTime) * speed);
    prevTime = timef;
    onset = false;
    updateVars(sounds, notes, maps);
}

void TexData::setColor(const ofxOscMessage &m) {
    if (m.getArgType(1) == OFXOSC_TYPE_STRING) {
        string value = m.getArgAsString(1);
        if (value == "rand") {
            useRandomColor = m.getNumArgs() > 2 ? m.getArgAsBool(2) : true;
            if (useRandomColor) {
                useMFCCColor = false;
            }
        }
        else if (value == "mfcc") {
            useMFCCColor = m.getNumArgs() > 2 ? m.getArgAsBool(2) : true;
            if (useMFCCColor) {
                useRandomColor = false;
            }
        }
        else if (value == "lerp") {
            ofFloatColor fromColor = Args::getInstance().parseColor(m, 3);
            ofFloatColor toColor = Args::getInstance().parseColor(m, 6);
            float perc = m.getArgAsFloat(2);
            color = ofxColorTheory::ColorUtil::lerpLch(fromColor, toColor, perc);
            useMFCCColor = false;
            useRandomColor = false;
        }
    }
    else {
        Args::getInstance().handleColor(&color, m);
        useMFCCColor = false;
        useRandomColor = false;
    }
}

void TexData::setTint(const ofxOscMessage &m) {
    if (m.getArgType(1) == OFXOSC_TYPE_STRING) {
        string value = m.getArgAsString(1);
        if (value == "rand") {
            tint = ofFloatColor(ofRandom(1.f), ofRandom(1.f), ofRandom(1.f));
            useMFCCTint = false;
        }
        else if (value == "mfcc") {
            useMFCCTint = m.getNumArgs() > 2 ? m.getArgAsBool(2) : true;
        }
        else if (value == "lerp") {
            ofFloatColor fromColor = Args::getInstance().parseColor(m, 3);
            ofFloatColor toColor = Args::getInstance().parseColor(m, 6);
            float perc = m.getArgAsFloat(2);
            tint = ofxColorTheory::ColorUtil::lerpLch(fromColor, toColor, perc);
            useMFCCTint = false;
        }
    }
    else {
        Args::getInstance().handleColor(&tint, m);
        useMFCCTint = false;
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

void TexData::afterDraw(const map<string, VarConfig>& maps) {
    for (map<string, VarConfig>::const_iterator it=maps.begin(); it!=maps.end(); ++it) {
        if (it->second.type == "tidal") {
            vars[it->first] -= 1.f/8.f;
            if (vars[it->first] < 0) {
                vars[it->first] = 0;
            }
        }
    }
}
