#include "GenericTex.h"

void GenericTex::update(TexData& data) {
    glm::vec2 size = data.getSize();
    if (!tex.isAllocated() || (tex.getWidth() != size.x || tex.getHeight() != size.y)) {
        tex.clear();
        data.allocate(tex);
    }
}

void GenericTex::draw(const glm::vec2 &pos, const glm::vec2 &size) {
    tex.draw(pos, size.x, size.y);
}

void GenericTex::clear() {
    tex.clear();
}
