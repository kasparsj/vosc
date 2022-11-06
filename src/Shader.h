#pragma once

#include "ofMain.h"
#include "ofxAutoReloadedShader.h"

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
    
private:
    ofxAutoReloadedShader* shader = NULL;
};

