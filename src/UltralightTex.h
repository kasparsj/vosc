#pragma once

#include "Tex.h"
#include "ofxUltralight.h"

class UltralightTex : public Tex {
public:
    UltralightTex(string path, const vector<float>& args) : Tex(path, args) {}
    ~UltralightTex() {}
    void update(TexData& data) override;
    void draw(const glm::vec2 &pos, const glm::vec2 &size) override;
    void choose() override {
        ofLogError() << "UltralightTex::choose not implemented";
    }
    ofTexture & getTexture() override {
        return ul.getTexture();
    }
    const ofTexture & getTexture() const override {
        return ul.getTexture();
    }
    ofPixels& getPixels() override {
        return ul.getPixels();
    }

private:
    ofxUltralight ul;
};
