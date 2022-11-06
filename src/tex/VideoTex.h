#pragma once

#include "Tex.h"

class VideoTex : public Tex {
public:
    static vector<string> cache;
    static string random();
    
    VideoTex(string name) : Tex(name) {}
    ~VideoTex() {}
    void update(Layer *layer) override;
    void seek(float pct);
    void draw(const glm::vec3 &pos, const glm::vec3 &size) override;
    void choose() override;
    ofPixels& getPixels() override {
        return videoPlayer.getPixels();
    }
    
private:
    ofVideoPlayer videoPlayer;
};
