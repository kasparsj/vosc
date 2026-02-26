#pragma once

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#elif __has_include(<json.hpp>)
#include <json.hpp>
#else
#error "nlohmann/json header not found"
#endif

using std::map;
using std::string;
using std::vector;

namespace glm {
struct vec2 {
    float x;
    float y;

    vec2()
        : x(0.f), y(0.f) {}

    explicit vec2(float value)
        : x(value), y(value) {}

    vec2(float x, float y)
        : x(x), y(y) {}
};

struct vec3 {
    float x;
    float y;
    float z;

    vec3()
        : x(0.f), y(0.f), z(0.f) {}

    explicit vec3(float value)
        : x(value), y(value), z(value) {}

    vec3(float x, float y, float z)
        : x(x), y(y), z(z) {}
};

struct mat4 {
    float data[16];

    mat4() {
        for (int i = 0; i < 16; ++i) {
            data[i] = 0.f;
        }
    }
};
} // namespace glm

using ofJson = nlohmann::json;

struct ofColor {
    std::uint32_t value;

    ofColor()
        : value(0) {}

    explicit ofColor(std::uint32_t value)
        : value(value) {}
};

struct ofFloatColor {
    float r;
    float g;
    float b;
    float a;

    ofFloatColor()
        : r(0.f), g(0.f), b(0.f), a(1.f) {}

    explicit ofFloatColor(float gray)
        : r(gray), g(gray), b(gray), a(1.f) {}

    ofFloatColor(float r, float g, float b, float a = 1.f)
        : r(r), g(g), b(b), a(a) {}

    ofFloatColor(const ofColor& color) {
        const std::uint32_t hex = color.value;
        r = static_cast<float>((hex >> 16) & 0xFF) / 255.f;
        g = static_cast<float>((hex >> 8) & 0xFF) / 255.f;
        b = static_cast<float>(hex & 0xFF) / 255.f;
        a = 1.f;
    }

    ofFloatColor(std::uint32_t hex) {
        r = static_cast<float>((hex >> 24) & 0xFF) / 255.f;
        g = static_cast<float>((hex >> 16) & 0xFF) / 255.f;
        b = static_cast<float>((hex >> 8) & 0xFF) / 255.f;
        a = static_cast<float>(hex & 0xFF) / 255.f;
    }

    ofFloatColor& operator=(const ofColor& color) {
        *this = ofFloatColor(color);
        return *this;
    }

    ofFloatColor& operator=(std::uint32_t hex) {
        *this = ofFloatColor(hex);
        return *this;
    }

    static ofFloatColor fromHex(std::uint32_t hex) {
        return ofFloatColor(
            static_cast<float>((hex >> 16) & 0xFF) / 255.f,
            static_cast<float>((hex >> 8) & 0xFF) / 255.f,
            static_cast<float>(hex & 0xFF) / 255.f,
            1.f
        );
    }
};

struct ofBuffer {
    string value;

    ofBuffer() {}
    explicit ofBuffer(const string& value)
        : value(value) {}

    string getText() const {
        return value;
    }
};

inline std::ostream& operator<<(std::ostream& os, const ofBuffer& buffer) {
    os << buffer.value;
    return os;
}

inline int ofToInt(const string& value) {
    try {
        size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed, 10);
        if (consumed != value.size()) {
            return 0;
        }
        return parsed;
    }
    catch (...) {
        return 0;
    }
}

inline float ofToFloat(const string& value) {
    try {
        size_t consumed = 0;
        const float parsed = std::stof(value, &consumed);
        if (consumed != value.size()) {
            return 0.f;
        }
        return parsed;
    }
    catch (...) {
        return 0.f;
    }
}

template<typename T>
inline string ofToString(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

inline string ofToString(const string& value) {
    return value;
}

inline string ofToLower(const string& value) {
    string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

inline string ofTrim(const string& value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(begin, end - begin);
}

inline std::uint32_t ofHexToInt(const string& value) {
    string hex = value;
    if (hex.size() > 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex = hex.substr(2);
    }
    return static_cast<std::uint32_t>(std::strtoul(hex.c_str(), nullptr, 16));
}

inline std::ostream& ofLog() {
    return std::cout;
}

inline std::ostream& ofLog(const string&) {
    return std::cout;
}

inline std::ostream& ofLogWarning() {
    return std::cerr;
}

inline std::ostream& ofLogWarning(const string&) {
    return std::cerr;
}

inline std::ostream& ofLogError() {
    return std::cerr;
}

inline std::ostream& ofLogError(const string&) {
    return std::cerr;
}
