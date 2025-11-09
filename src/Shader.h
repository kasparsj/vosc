#pragma once

#include "ofMain.h"
#include "ofxAutoReloadedShader.h"
#include "ofxOsc.h"
#include "Texture.h"
#include "Buffer.hpp"
#include "TexturePool.h"
#include "ofxShadertoy.h"

#define DEFAULT_TEX "tex"
#define MAX_LIGHTS 8

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
    bool loadFromSource(const string& source);
    bool loadShadertoy(const string& shadertoyId);
    bool loadFromFile(const string& path);
    void reload();
    void update(const vector<TidalNote> &notes);
    void oscCommand(const string& command, const ofxOscMessage& m);
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
    void setTexture(const string& name, const ofxOscMessage& m, int arg = 1);
    void setBuffer(const ofxOscMessage& m);
    void setBuffer(const string& name, const ofxOscMessage& m, int arg = 1);
    void setUniform1i(const string& name, int v1);
    void setUniform2f(const string& name, float v1, float v2);
    void setUniformTexture(const string& name, ofTexture& tex, int loc = 0);
    void setUniformMaterial(ofMaterial& mat, const string& prefix = "mat");
    void set(const ofxOscMessage& m);
    ofShader& getShader() {
        return *shader;
    }
    ofxShadertoy& getShadertoy() {
        return *shadertoy;
    }
    string getShaderPath() const {
        return shaderPath;
    }
    const map<string, GLenum>& getUniformTypes() const;
    map<string, string> getUniformValues() const;
    void checkUniforms();
    
private:
    void setUniformTextures(const map<string, shared_ptr<Texture>>& textures, int delay = 0);
    void setUniforms(const map<string, shared_ptr<BaseVar>>& vars);
    template<typename T>
    void setUniforms(shared_ptr<T>& shader, const map<string, shared_ptr<BaseVar>>& vars);
    template<typename T>
    void setLights(shared_ptr<T>& shader);
    template<typename T>
    void setUniformCameraMatrices(shared_ptr<T>& shader, ofCamera& cam, const string& prefix = "cam");
    template<typename T>
    void setUniformMaterial(shared_ptr<T>& shader, ofMaterial& mat, const string& prefix = "mat");
    
    template<typename T>
    void setUniformFloatArray(shared_ptr<T>& shader, const string& name, const vector<float>& values);
    template<typename T>
    void setUniformIntArray(shared_ptr<T>& shader, const string& name, const vector<int>& values);
    template<typename T>
    void setUniformFromFloatVar(shared_ptr<T>& shader, const string& name, const Variable<float>* var, GLenum uniformType);
    template<typename T>
    void setUniformFromIntVar(shared_ptr<T>& shader, const string& name, const Variable<int>* var, GLenum uniformType);

    map<string, shared_ptr<Texture>> textures;
    map<string, shared_ptr<Buffer>> buffers;
    shared_ptr<ofShader> shader;
    shared_ptr<ofxShadertoy> shadertoy;
    string shaderPath;
    mutable map<string, GLenum> uniformTypes;
};

