#pragma once

#include "Tex.h"

class FboTex : public Tex {
public:
    FboTex(string path, const vector<float>& args) : Tex(path, args) {}
    virtual void update(TexData& data) override;
    void draw(const glm::vec2 &pos, const glm::vec2 &size) override;
    void clear() override;
    virtual ofTexture & getTexture() override {
        return fbo.getTexture();
    }
    virtual const ofTexture & getTexture() const override {
        return fbo.getTexture();
    }
    void setUseTexture(bool bUseTex) override {
        fbo.setUseTexture(bUseTex);
    }
    bool isUsingTexture() const override {
        return fbo.isUsingTexture();
    }
    ofPixels& getPixels() override {
        fbo.readToPixels(pixels);
        return pixels;
    }
    
    ofFbo fbo;
};
