#include "Tex.h"
#include "TexData.h"

void Tex::update(TexData& data) {
#if ALLOW_TEX_2D_ARRAY
    glm::vec3 size = data.getSize();
    if (!tex.isAllocated() || (tex.getWidth() != size.x || tex.getHeight() != size.y || tex.getDepth() != size.z)) {
#else
    glm::vec2 size = data.getSize();
    if (!tex.isAllocated() || (tex.getWidth() != size.x || tex.getHeight() != size.y)) {
#endif
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

