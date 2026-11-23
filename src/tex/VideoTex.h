#pragma once

#include "BaseTex.h"

class VideoTex : public BaseTex {
public:
    static string random();
    
    VideoTex(string name, const vector<float>& args);
    ~VideoTex() override;
    static bool supportsInWindowLoad();
    void useInWindowLoad();
    void verifyInWindowInput() const;
    void update(TexData& data) override;
    bool startupFrameReady() const override;
    void seek(float pct);
    void draw(const glm::vec2 &pos, const glm::vec2 &size) override;
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
    struct InWindowVideo;
    void updateInWindow(TexData& data);
    void initializePlayback(TexData& data);
    void advancePlayback(TexData& data);

    std::unique_ptr<InWindowVideo> inWindow;
    bool observedFrame = false;
    ofVideoPlayer videoPlayer;
};
