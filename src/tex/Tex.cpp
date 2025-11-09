#include "Tex.h"

void Tex::update(TexData& data) {
    glm::vec2 size = data.getSize();
    if (!tex.isAllocated() || (tex.getWidth() != size.x || tex.getHeight() != size.y)) {
        tex.clear();
        data.allocate(tex);
    }
}

void Tex::draw(const glm::vec2 &pos, const glm::vec2 &size) {
    tex.draw(pos, size.x, size.y);
}

void Tex::clear() {
    tex.clear();
}

