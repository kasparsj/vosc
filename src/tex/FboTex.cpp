#include "FboTex.h"
#include "Layer.h"

void FboTex::update(Layer* layer, Texture* tex) {
    glm::vec2 size = tex->getSize();
    if (!fbo.isAllocated() || (fbo.getWidth() != size.x || fbo.getHeight() != size.y)) {
        fbo.clear();
        tex->allocate(fbo);
    }
}

void FboTex::draw(const glm::vec2 &size) {
    fbo.draw(0, 0, size.x, size.y);
}

void FboTex::clear() {
    fbo.begin();
    ofClear(0, 0, 0, 0);
    fbo.end();
}
