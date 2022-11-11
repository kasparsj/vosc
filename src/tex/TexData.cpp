#include "TexData.h"

void TexData::update(const vector<Sound> &sounds, const vector<TidalNote> &notes, const map<string, VarConfig>& maps) {
    const float timef = ofGetElapsedTimef();
    time += ((timef - prevTime) * speed);
    prevTime = timef;
    onset = false;
    updateVars(sounds, notes, maps);
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
