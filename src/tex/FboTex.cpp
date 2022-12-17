#include "FboTex.h"
#include "Layer.h"

void FboTex::update(TexData& data) {
    glm::vec2 size = data.getSize();
    if (!fbo.isAllocated() || (fbo.getWidth() != size.x || fbo.getHeight() != size.y)) {
        fbo.clear();
        data.allocate(fbo);
    }
}

void FboTex::draw(const glm::vec2 &pos, const glm::vec2 &size) {
    fbo.draw(pos, size.x, size.y);
}

void FboTex::clear() {
    fbo.begin();
    ofClear(0, 0, 0, 0);
    fbo.end();
}
