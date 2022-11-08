#include "FboPingPongTex.h"
#include "Layer.h"

void FboPingPongTex::update(TexData& data) {
    if (!fbo.isAllocated() || (fbo.source()->getWidth() != data.size.x || fbo.source()->getHeight() != data.size.y)) {
        //fbo.clear();
        fbo.allocate(data.getFboSettings());
    }
}

void FboPingPongTex::draw(const glm::vec2 &size) {
    fbo.draw(glm::vec2(0, 0), size.x);
}

void FboPingPongTex::clear() {
    fbo.clearBoth();
}
