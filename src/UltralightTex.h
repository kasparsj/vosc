#pragma once

#include "Tex.h"
#include "ofxUltralight.h"

class UltralightTex : public Tex {
public:
    UltralightTex(string path, const vector<float>& args) : Tex(path, args) {}
    ~UltralightTex() {
        _unload();
    }
    void update(TexData& data) override;
    void draw(const glm::vec2 &pos, const glm::vec2 &size) override;
    void choose() override {
        ofLogError() << "UltralightTex::choose not implemented";
    }
    ofTexture & getTexture() override {
        return ofxUltralight::get()->getTexture(ulId);
    }
    const ofTexture & getTexture() const override {
        return ofxUltralight::get()->getTexture(ulId);
    }
    ofPixels& getPixels() override {
        return ofxUltralight::get()->getPixels(ulId);
    }

private:
    void _unload() {
        if (ulId > -1) {
            ofxUltralight::removeView(ulId);
        }
    }
    
    int ulId = -1;
};
