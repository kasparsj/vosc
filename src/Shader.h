#pragma once

#include "ofMain.h"
#include "ofxAutoReloadedShader.h"
#include "ofxOsc.h"
#include "Texture.h"
#include "Buffer.hpp"
#include "TexturePool.h"
#include "ofxShadertoy.h"

#define DEFAULT_TEX "tex"

class Shader : public VarsHolder {
public:
    static map<string, ofxAutoReloadedShader> cache;
    static string random();
    
    Shader() : VarsHolder() {
    }
    ~Shader() {
        reset();
    }
    bool isLoaded() const {
        return isShaderLoaded() || shadertoy != NULL;
    }
    bool isShaderLoaded() const {
        return shader != NULL && shader->isLoaded();
    }
    bool load(string path);
    void reload();
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes);
    void begin(TexData& data, int delay = 0);
    void end();
    void reset();
    void unload();
    const map<string, shared_ptr<Texture>>& getTextures() const {
        return textures;
    }
    const map<string, shared_ptr<Buffer>>& getBuffers() const {
        return buffers;
    }
    bool hasTexture(string name) {
        return textures.find(name) != textures.end();
    }
    shared_ptr<Texture>& getTexture(string name) {
        return textures[name];
    }
    bool hasDefaultTexture() {
        return hasTexture(DEFAULT_TEX) && getDefaultTexture()->isLoaded();
    }
    shared_ptr<Texture>& getDefaultTexture();
    void setDefaultTexture(shared_ptr<Texture>& tex);
    void setTexture(const ofxOscMessage& m);
    void setTexture(string name, const ofxOscMessage& m, int arg = 1);
    void setBuffer(const ofxOscMessage& m);
    void setBuffer(string name, const ofxOscMessage& m, int arg = 1);
    void setUniform1i(string name, int v1) {
        if (shadertoy == NULL) {
            shader->setUniform1i(name, v1);
        }
        else {
            shadertoy->setUniform1i(name, v1);
        }
    }
    void setUniform2f(string name, float v1, float v2) {
        if (shadertoy == NULL) {
            shader->setUniform2f(name, v1, v2);
        }
        else {
            shadertoy->setUniform2f(name, v1, v2);
        }
    }
    void set(const ofxOscMessage& m);
    
private:
    void setUniformTextures(const map<string, shared_ptr<Texture>>& textures, int delay = 0);
    void setUniforms(const map<string, shared_ptr<BaseVar>>& vars);
    template<typename T>
    void setUniforms(T* shader, const map<string, shared_ptr<BaseVar>>& vars);

    map<string, shared_ptr<Texture>> textures;
    map<string, shared_ptr<Buffer>> buffers;
    ofShader* shader = NULL;
    ofxShadertoy* shadertoy = NULL;
};

