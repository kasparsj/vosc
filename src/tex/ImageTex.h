#pragma once

#include "Tex.h"

class ImageTex : public Tex {
public:
    static vector<string> cache;
    static string random();
    
    ImageTex(string path, const vector<float>& args) : Tex(path, args) {}
    ~ImageTex() {}
    void update(TexData& data) override;
    void draw(const glm::vec2 &size) override;
    void choose() override;
    ofTexture & getTexture() override {
        return image.getTexture();
    }
    const ofTexture & getTexture() const override {
        return image.getTexture();
    }
    void setUseTexture(bool bUseTex) override {
        image.setUseTexture(bUseTex);
    }
    bool isUsingTexture() const override {
        return image.isUsingTexture();
    }
    ofPixels& getPixels() override {
        return image.getPixels();
    }
    
private:
    ofImage image;
};
