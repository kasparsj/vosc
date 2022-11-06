#pragma once

#include "Tex.h"
#include "ofxHPVPlayer.h"

class HPVideoTex : public Tex {
public:
    static vector<string> cache;
    static string random();
    static bool engineInitialized;
    
    HPVideoTex(string name) : Tex(name) {
        if (!engineInitialized) {
            HPV::InitHPVEngine();
        }
        hpvPlayer.init(HPV::NewPlayer());
    }
    ~HPVideoTex() {}
    void update(Layer *layer) override;
    void seek(float pct);
    void draw(const glm::vec3 &pos, const glm::vec3 &size) override;
    void choose() override;
    ofPixels& getPixels() override {
        ofTexture* tex = hpvPlayer.getTexturePtr();
        tex->readToPixels(pixels);
        return pixels;
    }
    
private:
    ofxHPVPlayer hpvPlayer;
    ofPixels pixels;
};
