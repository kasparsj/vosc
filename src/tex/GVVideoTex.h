#pragma once

#include "Tex.h"
#include "Config.h"
#if USE_OFX_EXTREME_GPU_VIDEO
#include "ofxExtremeGpuVideo.hpp"
#include "ofxGvTexture.hpp"
#endif

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
#if USE_OFX_EXTREME_GPU_VIDEO
        return _gv.getTexture();
#else
        throw "GVVideoTex::getTexture called but USE_OFX_EXTREME_GPU_VIDEO is disabled";
#endif
    }
    const ofTexture & getTexture() const override {
        throw "GVVideoTex::getTexture const not implemented";
    }
    bool isUsingTexture() const override {
        return true;
    }
    ofPixels& getPixels() override {
        getTexture().readToPixels(pixels);
        return pixels;
    }
    
private:
#if USE_OFX_EXTREME_GPU_VIDEO
    ofxExtremeGpuVideo _gpuVideo;
    ofxGvTexture _gv;
#endif
    bool isPlaying;
    float startTime;
};
