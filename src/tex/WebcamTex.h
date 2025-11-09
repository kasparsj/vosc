#pragma once

#include "BaseTex.h"

class WebcamTex : public BaseTex {
public:
    static bool exists(int device);
    static int random();
    
    WebcamTex(string path, const vector<float>& args) : BaseTex(path, args) {}
    ~WebcamTex() {
        vidGrabber.close();
    }
    void update(TexData& data) override;
    void draw(const glm::vec2 &pos, const glm::vec2 &size) override;
    void choose() override;
    bool isFrameNew() override {
        return vidGrabber.isFrameNew();
    }
    ofTexture & getTexture() override {
        return vidGrabber.getTexture();
    }
    const ofTexture & getTexture() const override {
        return vidGrabber.getTexture();
    }
    void setUseTexture(bool bUseTex) override {
        vidGrabber.setUseTexture(bUseTex);
    }
    bool isUsingTexture() const override {
        return vidGrabber.isUsingTexture();
    }
    ofPixels& getPixels() override {
        return vidGrabber.getPixels();
    }

private:
    ofVideoGrabber vidGrabber;
};
