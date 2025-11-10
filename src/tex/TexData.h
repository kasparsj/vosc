#pragma once

#include "VarsHolder.h"
#include "Config.h"

class TexData {
public:
    TexData() {
        randomSeed = ofRandom(1000);
    }
    
    void setup(VarsHolder* parent) {
        this->parent = parent;
    }
    void update(const vector<TidalNote> &notes);
    void oscCommand(const string& command, const ofxOscMessage &m);
    
#if ALLOW_TEX_2D_ARRAY
    glm::vec3 getSize() const {
#else
    glm::vec2 getSize() const {
#endif
        return getVarVec3("size");
    }
    void setSize(float w, float h, float d = 0) {
        parent->setVar("size", glm::vec3(w, h, d));
        texData.width = w;
        texData.height = h;
#if ALLOW_TEX_2D_ARRAY
        texData.depth = d;
        // todo: set tex_d?
        if (d > 0) {
            texData.textureTarget = GL_TEXTURE_2D_ARRAY;
        }
        else {
            ofTextureData tmp;
            texData.textureTarget = tmp.textureTarget;
        }
#endif
        fboSettings.width = w;
        fboSettings.height = h;
    }
    void setSize(glm::vec2 size) {
        setSize(size.x, size.y);
    }
    void setSize(glm::vec3 size) {
        setSize(size.x, size.y, size.z);
    }
    void setSize(const ofxOscMessage& m) {
        switch (m.getNumArgs()) {
            case 4:
                setSize(m.getArgAsFloat(1), m.getArgAsFloat(2), m.getArgAsFloat(3));
                break;
            case 3:
                setSize(m.getArgAsFloat(1), m.getArgAsFloat(2));
                break;
            case 2:
                if (m.getArgType(1) == 's') {
                    ofLogWarning() << "/tex/size expects 1 to 3 numeric parameters";
                }
                else {
                    setSize(m.getArgAsFloat(1), m.getArgAsFloat(1));
                }
                break;
        }
    }
    void setTextureWrap(GLint wrapModeHorizontal, GLint wrapModeVertical);
    void set(const ofxOscMessage& m);

    ofFbo::Settings& getFboSettings() {
        fboSettings.width = texData.width;
        fboSettings.height = texData.height;
        fboSettings.internalformat = texData.glInternalFormat;
        fboSettings.textureTarget = texData.textureTarget;
        fboSettings.wrapModeHorizontal = texData.wrapModeHorizontal;
        fboSettings.wrapModeVertical = texData.wrapModeVertical;
        fboSettings.minFilter = texData.minFilter;
        fboSettings.maxFilter = texData.magFilter;
        return fboSettings;
    }
    void allocate(ofFbo& fbo);
    void allocate(ofTexture& tex);
    void afterDraw(const map<string, shared_ptr<BaseVar>>& vars);
    const map<string, shared_ptr<BaseVar>>& getVars() const {
        return parent->vars;
    }
    bool hasVar(const string& name) const {
        return parent->hasVar(name);
    }
    float getVar(const string& name, int idx = 0) const {
        return parent->getVar(name, idx);
    }
    float getVarPercent(const string& name, int idx = 0) const {
        return parent->getVarPercent(name, idx);
    }
    const vector<float>& getVarVec(const string& name) const {
        return parent->getVarVec(name);
    }
    glm::vec3 getVarVec3(const string& name) const {
        return parent->getVarVec3(name);
    }
    ofFloatColor getVarColor(const string& name) const {
        return parent->getVarColor(name);
    }
    
    float time = 0;
    float prevTime = 0;
    bool noClear = false;
    ofBlendMode blendMode = OF_BLENDMODE_ALPHA;
    bool aspectRatio = true;
    int randomSeed = 0;
    
protected:
    VarsHolder* parent;
    ofTextureData texData;
    ofFbo::Settings fboSettings;

};
