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
    // FBO/Texture settings (previously handled by /tex/fbo)
    else if (prop == "internalformat") {
        texData.glInternalFormat = m.getArgAsInt(2);
    }
    else if (prop == "textureTarget") {
        texData.textureTarget = m.getArgAsInt(2);
    }
    else if (prop == "wrapMode") {
        texData.wrapModeHorizontal = m.getArgAsInt(2);
        texData.wrapModeVertical = m.getArgAsInt(2);
    }
    else if (prop == "wrapModeHorizontal") {
        texData.wrapModeHorizontal = m.getArgAsInt(2);
    }
    else if (prop == "wrapModeVertical") {
        texData.wrapModeVertical = m.getArgAsInt(2);
    }
    else if (prop == "filter") {
        texData.minFilter = m.getArgAsInt(2);
        texData.magFilter = m.getArgAsInt(2);
    }
    else if (prop == "minFilter") {
        texData.minFilter = m.getArgAsInt(2);
    }
    else if (prop == "maxFilter" || prop == "magFilter") {
        texData.magFilter = m.getArgAsInt(2);
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
