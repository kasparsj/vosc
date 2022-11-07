#include "FBOTex1.h"
#include "Layer.h"

void FBOTex1::update(Layer* layer, Texture* tex) {
    glm::vec2 size = tex->getSize();
    if (!fbo.isAllocated() || (fbo.getWidth() != size.x || fbo.getHeight() != size.y)) {
        fbo.clear();
        tex->allocate(fbo);
    }
}

void FBOTex::draw(const glm::vec2 &size) {
    fbo.draw(0, 0, size.x, size.y);
}

void FBOTex::clear() {
    fbo.begin();
    ofClear(0, 0, 0, 0);
    fbo.end();
}
