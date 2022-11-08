#include "FboTex.h"
#include "Layer.h"

void FboTex::update(TexData& data) {
    if (!fbo.isAllocated() || (fbo.getWidth() != data.size.x || fbo.getHeight() != data.size.y)) {
        fbo.clear();
        data.allocate(fbo);
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
