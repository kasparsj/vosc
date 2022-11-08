#pragma once

#include "Tex.h"

class VideoTex : public Tex {
public:
    static vector<string> cache;
    static string random();
    
    VideoTex(string name, const vector<float>& args) : Tex(name, args) {}
    ~VideoTex() {}
    void update(TexData& data) override;
    void seek(float pct);
    void draw(const glm::vec2 &size) override;
    void choose() override;
    ofTexture & getTexture() override {
        return videoPlayer.getTexture();
    }
    const ofTexture & getTexture() const override {
        return videoPlayer.getTexture();
    }
    void setUseTexture(bool bUseTex) override {
        videoPlayer.setUseTexture(bUseTex);
    }
    bool isUsingTexture() const override {
        return videoPlayer.isUsingTexture();
    }
    ofPixels& getPixels() override {
        return videoPlayer.getPixels();
    }
    
private:
    ofVideoPlayer videoPlayer;
};
