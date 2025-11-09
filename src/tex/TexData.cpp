#include "TexData.h"
#include "Args.h"

void TexData::update(const vector<TidalNote> &notes) {
    const float timef = ofGetElapsedTimef();
    time += ((timef - prevTime) * parent->getVar("speed"));
    prevTime = timef;
}

void TexData::oscCommand(const string& command, const ofxOscMessage &m) {
    if (command == "/tex/size") {
        setSize(m);
    }
    else if (command == "/tex/set") {
        set(m);
    }
    else if (command == "/tex/fbo") {
        setSettings(m);
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

void TexData::setSettings(const ofxOscMessage &m) {
    std::unordered_map<string, std::function<void(int, ofTextureData&)>> mapping;
    mapping["internalformat"] = [](int v, ofTextureData& a) { a.glInternalFormat = v; };
    mapping["textureTarget"] = [](int v, ofTextureData& a) { a.textureTarget = v; };
    mapping["wrapModeHorizontal"] = [](int v, ofTextureData& a) { a.wrapModeHorizontal = v; };
    mapping["wrapModeVertical"] = [](int v, ofTextureData& a) { a.wrapModeVertical = v; };
    mapping["minFilter"] = [](int v, ofTextureData& a) { a.minFilter = v; };
    mapping["maxFilter"] = [](int v, ofTextureData& a) { a.magFilter = v; };
    string key = m.getArgAsString(1);
    int value = m.getArgAsInt(2);
    if (mapping.find(key) != mapping.end()) {
        mapping[key](value, texData);
    }
}

void TexData::allocate(ofFbo& fbo) {
    ofDisableTextureEdgeHack();
    fbo.allocate(getFboSettings());
    ofEnableTextureEdgeHack();
}

void TexData::allocate(ofTexture& tex) {
    ofDisableTextureEdgeHack();
    tex.allocate(texData);
    ofEnableTextureEdgeHack();
}

void TexData::afterDraw(const map<string, shared_ptr<BaseVar>>& vars) {
    for (map<string, shared_ptr<BaseVar>>::const_iterator it=vars.begin(); it!=vars.end(); ++it) {
        it->second->afterDraw();
    }
}
