#pragma once

#include "ofMain.h"
#include "ofxAutoReloadedShader.h"
#include "ofxOsc.h"
#include "Texture.h"
#include "TexturePool.h"

#define DEFAULT_TEX "tex"

class Layer;

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
        return hasTexture(DEFAULT_TEX) && getDefaultTexture()->isLoaded();
    }
    Texture* getDefaultTexture();
    void setDefaultTexture(Texture* tex);
    void setTexture(const ofxOscMessage& m);
    void setTexture(string name, const ofxOscMessage& m, int arg = 1);
    void set(const ofxOscMessage& m);
    
private:
    map<string, Texture*> textures;
    ofxAutoReloadedShader shader;
};

