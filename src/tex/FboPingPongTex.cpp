#include "FboPingPongTex.h"
#include "Layer.h"

void FboPingPongTex::update(Layer* layer, Texture* tex) {
    glm::vec2 size = tex->getSize();
    if (!fbo.isAllocated() || (fbo.source()->getWidth() != size.x || fbo.source()->getHeight() != size.y)) {
        //fbo.clear();
        fbo.allocate(tex->fboSettings);
    }
}

void FboPingPongTex::draw(const glm::vec2 &size) {
    fbo.draw(glm::vec2(0, 0), size.x);
}

void FboPingPongTex::clear() {
    fbo.clearBoth();
}
