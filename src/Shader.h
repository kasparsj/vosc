#pragma once

#include "ofMain.h"
#include "ofxAutoReloadedShader.h"
#include "ofxOsc.h"

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
    void begin(Layer *layer);
    void end();
    ofxAutoReloadedShader* getShader() {
        return shader;
    };
    void setUniform(string name, const ofxOscMessage& m);
    map<string, vector<float>> uniforms;
    
private:
    ofxAutoReloadedShader* shader = NULL;
};

