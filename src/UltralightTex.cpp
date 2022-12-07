#include "UltralightTex.h"

ofxUL::ofxUltralight UltralightTex::ul;

void UltralightTex::update(TexData& data) {
    if (!ul.renderer) {
        ul.setup();
    }
    if (path != prevPath) {
        _unload();
        ulIndex = ul.load(args.size() > 0 ? args[0] : ofGetWidth(),
                          args.size() > 1 ? args[1] : ofGetHeight(), path);
        prevPath = path;
    }
}

void UltralightTex::draw(const glm::vec2 &pos, const glm::vec2 &size) {
    if (ulIndex > -1) {
        ul.draw(ulIndex);
    }
}
