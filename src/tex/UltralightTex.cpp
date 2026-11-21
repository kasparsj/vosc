#include "UltralightTex.h"

#if USE_OFX_ULTRALIGHT
void UltralightTex::update(TexData& data) {
    ofxUltralight::setup();
    if (path != prevPath) {
        _unload();
        view = ofxUltralight::createView(args.size() > 0 ? args[0] : ofGetWidth(),
                                         args.size() > 1 ? args[1] : ofGetHeight(), path);
        ulId = view->getId();
        prevPath = path;
    }
}

bool UltralightTex::startupFrameReady() const {
    // The addon allocates this texture by uploading its dirty bitmap in draw().
    if (!view || !view->DOMready) return false;
    const auto& frame = view->getTexture();
    return frame.isAllocated() && frame.getWidth() > 0 && frame.getHeight() > 0;
}

void UltralightTex::draw(const glm::vec2 &pos, const glm::vec2 &size) {
    if (ulId > -1) {
        ofxUltralight::draw(ulId);
    }
}
#endif
