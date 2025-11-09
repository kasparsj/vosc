#pragma once

#include "Tex.h"
#include "Config.h"

#if USE_OFX_ULTRALIGHT
#include "ofxUltralight.h"
#endif

class UltralightTex : public Tex {
public:
    UltralightTex(string path, const vector<float>& args) : Tex(path, args) {}
    ~UltralightTex() {
        _unload();
    }
    void update(TexData& data) override;
    void draw(const glm::vec2 &pos, const glm::vec2 &size) override;
    void choose() override {
        ofLogError() << "UltralightTex::choose not implemented";
    }
    ofTexture & getTexture() override {
#if USE_OFX_ULTRALIGHT
        return ofxUltralight::get()->getTexture(ulId);
#else
        throw "UltralightTex::getTexture called but USE_ULTRALIGHT is disabled";
#endif
    }
    const ofTexture & getTexture() const override {
#if USE_OFX_ULTRALIGHT
        return ofxUltralight::get()->getTexture(ulId);
#else
        throw "UltralightTex::getTexture called but USE_ULTRALIGHT is disabled";
#endif
    }
    ofPixels& getPixels() override {
#if USE_OFX_ULTRALIGHT
        return ofxUltralight::get()->getPixels(ulId);
#else
        throw "UltralightTex::getPixels called but USE_ULTRALIGHT is disabled";
#endif
    }

private:
    void _unload() {
#if USE_OFX_ULTRALIGHT
        if (ulId > -1) {
            ofxUltralight::removeView(ulId);
        }
#endif
    }
    
    int ulId = -1;
};
