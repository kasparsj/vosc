#pragma once

#include "BaseTex.h"
#include "FboPingPong.h"

class FboPingPongTex : public BaseTex {
public:
    FboPingPongTex(string path, const vector<float>& args) : BaseTex(path, args) {}
    virtual void update(TexData& data) override;
    void draw(const glm::vec2 &pos, const glm::vec2 &size) override;
    void clear() override;
    virtual ofTexture & getTexture() override {
        return fbo.source()->getTexture();
    }
    virtual const ofTexture & getTexture() const override {
        return fbo.source()->getTexture();
    }
    void setUseTexture(bool bUseTex) override {
        /*irrevelant*/
    }
    bool isUsingTexture() const override {
        return true;
    }
    virtual int getNumTextures() override {
        return fbo.source()->getNumTextures();
    }
    virtual ofTexture & getTexture(int att) override {
        return fbo.source()->getTexture(att);
    }
    virtual const ofTexture & getTexture(int att) const override {
        return fbo.source()->getTexture(att);
    }
    ofPixels& getPixels() override {
        fbo.source()->readToPixels(pixels);
        return pixels;
    }
    
    FboPingPong fbo;
};
