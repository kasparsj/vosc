#pragma once

#include "ofMain.h"
#include "ofxAutoReloadedShader.h"
#include "ofxOsc.h"
#include "Texture.h"

#define DEFAULT_TEX "tex"

class Layer;

class Shader {
public:
    static map<string, ofxAutoReloadedShader> cache;
    static string random();
    
    bool isLoaded() const {
        return shader != NULL && shader->isLoaded();
    }
    bool load(string path);
    void reload();
    void update(Layer* layer);
    void begin(Layer *layer);
    void end();
    void reset();
    ofxAutoReloadedShader* getShader() {
        return shader;
    };
    bool hasTexture(string name) {
        return textures.find(name) != textures.end();
    }
    Texture& getTexture(string name) {
        return textures[name];
    }
    bool hasDefaultTexture() {
        return hasTexture(DEFAULT_TEX);
    }
    Texture& getDefaultTexture(bool create = false);
    void setTexture(const ofxOscMessage& m);
    void setTexture(string name, const ofxOscMessage& m, int arg = 1);
    void setUniform(const ofxOscMessage& m);
    vector<float>& getUniform(string name) {
        return uniforms[name];
    }
    void setUniform(string name, const vector<float>& value);
    void setUniform(string name, ofFloatColor& color) {
        vector<float> vec = {color.r, color.g, color.b};
        setUniform(name, vec);
    }
    
private:
    map<string, Texture> textures;
    map<string, vector<float>> uniforms;
    ofxAutoReloadedShader* shader = NULL;
};

