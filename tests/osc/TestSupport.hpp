#pragma once

#include "ofxOscMessage.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

inline ofxOscMessage msg(const string& address) {
    ofxOscMessage m;
    m.setAddress(address);
    return m;
}

inline bool almostEqual(float a, float b, float epsilon = 1e-5f) {
    return std::fabs(a - b) <= epsilon;
}

inline void reportFailure(const char* file, int line, const string& message) {
    std::cerr << file << ":" << line << " - " << message << std::endl;
}

#define TEST_REQUIRE(condition)                                                                   \
    do {                                                                                          \
        if (!(condition)) {                                                                       \
            reportFailure(__FILE__, __LINE__, string("assertion failed: ") + #condition);        \
            return false;                                                                         \
        }                                                                                         \
    } while (0)

#define TEST_REQUIRE_EQ(actual, expected)                                                         \
    do {                                                                                          \
        if (!((actual) == (expected))) {                                                          \
            reportFailure(__FILE__, __LINE__,                                                     \
                string("assertion failed: ") + #actual + " == " + #expected);                    \
            return false;                                                                         \
        }                                                                                         \
    } while (0)

template<typename Callable>
inline bool throwsAny(Callable&& callable) {
    try {
        callable();
    }
    catch (...) {
        return true;
    }
    return false;
}
