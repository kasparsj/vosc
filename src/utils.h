#pragma once

#include <glm/glm.hpp>
#include <type_traits>

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
