#include "UltralightTex.h"

void UltralightTex::update(TexData& data) {
    if (!ul.view || (!ul.view->is_loading() && !ul.DOMready)) {
        ul.setup(args.size() > 0 ? args[0] : ofGetWidth(),
                 args.size() > 1 ? args[1] : ofGetHeight(), path);
    }
    else {
        ul.update();
    }
}

void UltralightTex::draw(const glm::vec2 &pos, const glm::vec2 &size) {
    ul.draw();
}
