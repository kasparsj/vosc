#include "FboPingPongTex.h"
#include "Layer.h"

void FboPingPongTex::update(TexData& data) {
    glm::vec2 size = data.getSize();
    if (!fbo.isAllocated() || (fbo.source()->getWidth() != size.x || fbo.source()->getHeight() != size.y)) {
        //fbo.clear();
        fbo.allocate(data.getFboSettings());
    }
}

void FboPingPongTex::draw(const glm::vec2 &pos, const glm::vec2 &size) {
    fbo.draw(pos, size.x);
}

void FboPingPongTex::clear() {
    fbo.clearBoth();
}
