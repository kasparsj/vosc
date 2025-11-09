#pragma once

#include "BaseTex.h"
#include "Config.h"
#if USE_OFX_HPVPLAYER
#include "ofxHPVPlayer.h"
#endif

class HPVideoTex : public BaseTex {
public:
    static vector<string> cache;
    static string random();
    static bool engineInitialized;
    
    HPVideoTex(string name, const vector<float>& args) : BaseTex(name, args) {
#if USE_OFX_HPVPLAYER
        if (!engineInitialized) {
            HPV::InitHPVEngine();
        }
        hpvPlayer.init(HPV::NewPlayer());
#endif
    }
    ~HPVideoTex() {}
    void update(TexData& data) override;
    void seek(float pct);
    void draw(const glm::vec2 &pos, const glm::vec2 &size) override;
    void choose() override;
    ofTexture & getTexture() override {
#if USE_OFX_HPVPLAYER
        return *hpvPlayer.getTexturePtr();
#else
        throw "HPVideoTex::getTexture called but USE_OFX_HPVPLAYER is disabled";
#endif
    }
    bool isUsingTexture() const override {
        return true;
    }
    ofPixels& getPixels() override {
        getTexture().readToPixels(pixels);
        return pixels;
    }
    
private:
#if USE_OFX_HPVPLAYER
    ofxHPVPlayer hpvPlayer;
#endif
};
