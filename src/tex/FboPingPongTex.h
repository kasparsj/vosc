#pragma once

#include "Tex.h"
#include "FboPingPong.h"

class FboPingPongTex : public Tex {
public:
    FboPingPongTex(string path, const vector<float>& args) : Tex(path, args) {}
    virtual void update(TexData& data) override;
    void draw(const glm::vec2 &size) override;
    void clear() override;
    virtual ofTexture & getTexture() override {
        return fbo.source()->getTexture();
    }
    virtual const ofTexture & getTexture() const override {
        // todo: throw error?
    }
    void setUseTexture(bool bUseTex) override {
        /*irrevelant*/
    }
    bool isUsingTexture() const override {
        return true;
    }
    ofPixels& getPixels() override {
        fbo.source()->readToPixels(pixels);
        return pixels;
    }
    
    FboPingPong fbo;
};
