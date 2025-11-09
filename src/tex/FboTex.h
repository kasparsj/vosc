#pragma once

#include "BaseTex.h"

class FboTex : public BaseTex {
public:
    FboTex(string path, const vector<float>& args) : BaseTex(path, args) {}
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
    virtual int getNumTextures() override {
        return fbo.getNumTextures();
    }
    ofPixels& getPixels() override {
        fbo.readToPixels(pixels);
        return pixels;
    }
    
    ofFbo fbo;
};
