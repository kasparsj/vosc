#include "UltralightTex.h"

#if USE_OFX_ULTRALIGHT
void UltralightTex::update(TexData& data) {
    ofxUltralight::setup();
    if (path != prevPath) {
        _unload();
        ulId = ofxUltralight::createView(args.size() > 0 ? args[0] : ofGetWidth(),
                                         args.size() > 1 ? args[1] : ofGetHeight(), path)->getId();
        prevPath = path;
    }
}

void UltralightTex::draw(const glm::vec2 &pos, const glm::vec2 &size) {
    if (ulId > -1) {
        ofxUltralight::draw(ulId);
    }
}
#endif
