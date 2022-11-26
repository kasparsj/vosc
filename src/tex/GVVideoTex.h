#pragma once

#include "Tex.h"
#include "ofxExtremeGpuVideo.hpp"
#include "ofxGvTexture.hpp"

class GVVideoTex : public Tex {
public:
    static vector<string> cache;
    static string random();
    static bool engineInitialized;
    
    GVVideoTex(string name, const vector<float>& args) : Tex(name, args) {
    }
    ~GVVideoTex() {}
    void update(TexData& data) override;
    void seek(float pct);
    void draw(const glm::vec2 &pos, const glm::vec2 &size) override;
    void choose() override;
    ofTexture & getTexture() override {
        return _gv.getTexture();
    }
    const ofTexture & getTexture() const override {
        // todo: throw error
    }
    bool isUsingTexture() const override {
        return true;
    }
    ofPixels& getPixels() override {
        getTexture().readToPixels(pixels);
        return pixels;
    }
    
private:
    ofxExtremeGpuVideo _gpuVideo;
    ofxGvTexture _gv;
    bool isPlaying;
    float startTime;
};
