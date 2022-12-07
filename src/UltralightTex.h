#pragma once

#include "Tex.h"
#include "ofxUltralight.h"

class UltralightTex : public Tex {
public:
    static ofxUL::ofxUltralight ul;
    
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
        return ul.getTexture(ulIndex);
    }
    const ofTexture & getTexture() const override {
        return ul.getTexture(ulIndex);
    }
    ofPixels& getPixels() override {
        return ul.getPixels(ulIndex);
    }

private:
    void _unload() {
        if (ulIndex > -1) {
            ul.unload(ulIndex);
        }
    }
    
    int ulIndex = -1;
};
