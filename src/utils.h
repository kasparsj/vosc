#pragma once

#include <glm/glm.hpp>
#include <type_traits>
#include "ofVboMesh.h"

// Detect if T is a glm::vec<L, T, Q> type
template<typename T>
struct is_glm_vec : std::false_type {};

template<glm::length_t L, typename T_, glm::qualifier Q>
struct is_glm_vec<glm::vec<L, T_, Q>> : std::true_type {};

// Main util function
template<typename T>
constexpr int glmVecTypeDimension() {
    if constexpr (is_glm_vec<T>::value) {
        return T::length(); // Works for vec2, vec3, vec4, etc.
    } else {
        return 0; // Not a glm::vec
    }
}

template<typename T>
constexpr int glmVecTypeDimensionOf(const T&) {
    return glmVecTypeDimension<std::decay_t<T>>();
}

inline ofMesh getQuad(ofTexture& tex, float x, float y, float w, float h) {
    glm::vec3 p1(x, y, 0);
    glm::vec3 p2(x + w, y, 0);
    glm::vec3 p3(x + w, y + h, 0);
    glm::vec3 p4(x, y + h, 0);
    ofMesh quad = tex.getQuad(p1, p2, p3, p4);
    return quad;
}
