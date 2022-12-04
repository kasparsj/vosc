#pragma once

#include "Tex.h"
#include "ofxHPVPlayer.h"

class HPVideoTex : public Tex {
public:
    static vector<string> cache;
    static string random();
    static bool engineInitialized;
    
    HPVideoTex(string name, const vector<float>& args) : Tex(name, args) {
        if (!engineInitialized) {
            HPV::InitHPVEngine();
        }
        hpvPlayer.init(HPV::NewPlayer());
    }
    ~HPVideoTex() {}
    void update(TexData& data) override;
    void seek(float pct);
    void draw(const glm::vec2 &pos, const glm::vec2 &size) override;
    void choose() override;
    ofTexture & getTexture() override {
        return *hpvPlayer.getTexturePtr();
    }
    const ofTexture & getTexture() const override {
        return ofTexture();
    }
    bool isUsingTexture() const override {
        return true;
    }
    ofPixels& getPixels() override {
        getTexture().readToPixels(pixels);
        return pixels;
    }
    
private:
    ofxHPVPlayer hpvPlayer;
};
