#pragma once

#include "ofMain.h"
#include "ofxAutoReloadedShader.h"
#include "ofxOsc.h"
#include "Texture.h"
#include "TexturePool.h"
#include "VarsConfig.h"

#define DEFAULT_TEX "tex"

class Layer;

class Shader {
public:
    static map<string, ofxAutoReloadedShader> cache;
    static string random();
    
    Shader() {
        static int nextId = 0;
        _id = nextId++;
    }
    ~Shader() {
        reset();
    }
    bool isLoaded() const {
        return shader.isLoaded();
    }
    bool load(string path);
    void reload();
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes);
    void begin(TexData& data, int delay = 0);
    void end();
    void reset();
    ofxAutoReloadedShader& getShader() {
        return shader;
    };
    bool hasTexture(string name) {
        return textures.find(name) != textures.end();
    }
    Texture* getTexture(string name) {
        if (!hasTexture(name)) {
            return NULL;
        }
        return textures[name];
    }
    bool hasDefaultTexture() {
        return hasTexture(DEFAULT_TEX);
    }
    Texture* getDefaultTexture();
    void setDefaultTexture(Texture* tex);
    void setTexture(const ofxOscMessage& m);
    void setTexture(string name, const ofxOscMessage& m, int arg = 1);
    void setUniform(const ofxOscMessage& m);
    vector<float>& getUniform(string name) {
        return uniforms[name];
    }
    void setUniform(string name, const vector<float>& value);
    void setUniform(string name, float v1) {
        vector<float> vec = {v1};
        setUniform(name, vec);
    }
    void setUniform(string name, float v1, float v2) {
        vector<float> vec = {v1, v2};
        setUniform(name, vec);
    }
    void setUniform(string name, float v1, float v2, float v3) {
        vector<float> vec = {v1, v2, v3};
        setUniform(name, vec);
    }
    void setUniform(string name, float v1, float v2, float v3, float v4) {
        vector<float> vec = {v1, v2, v3, v4};
        setUniform(name, vec);
    }
    void setUniform(string name, ofFloatColor& color) {
        vector<float> vec = {color.r, color.g, color.b};
        setUniform(name, vec);
    }
    void set(const ofxOscMessage& m);
    int getId() {
        return _id;
    }
    
private:
    int _id;
    map<string, Texture*> textures;
    map<string, vector<float>> uniforms;
    ofxAutoReloadedShader shader;
};

