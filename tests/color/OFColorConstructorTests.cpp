#include "ofColor.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {
int failures = 0;

template<typename Actual, typename Expected>
void expectEqual(const Actual & actual, const Expected & expected, const std::string & label) {
    if (actual != static_cast<Actual>(expected)) {
        std::cerr << "FAIL " << label << "\n";
        ++failures;
    }
}

template<typename PixelType>
void testInteger(const std::string & name) {
    static_assert(std::numeric_limits<PixelType>::is_integer);
    const auto hi = std::numeric_limits<PixelType>::max();
    const auto lo = std::numeric_limits<PixelType>::lowest();
    ofColor_<PixelType> white;
    expectEqual(white.r, hi, name + " default r");
    expectEqual(white.g, hi, name + " default g");
    expectEqual(white.b, hi, name + " default b");
    expectEqual(white.a, hi, name + " default a");
    expectEqual(ofColor_<PixelType>::white.r, hi, name + " named white r");
    expectEqual(ofColor_<PixelType>::white.a, hi, name + " named white a");
    ofColor_<PixelType> bounds(std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity(),
                               std::numeric_limits<float>::quiet_NaN(),
                               static_cast<float>(hi));
    expectEqual(bounds.r, hi, name + " positive infinity");
    expectEqual(bounds.g, lo, name + " negative infinity");
    expectEqual(bounds.b, 0, name + " NaN");
    expectEqual(bounds.a, hi, name + " rounded maximum");
    ofColor_<PixelType> finiteOverflow(std::numeric_limits<float>::max(),
                                       -std::numeric_limits<float>::max(), 0, 0);
    expectEqual(finiteOverflow.r, hi, name + " finite upper saturation");
    expectEqual(finiteOverflow.g, lo, name + " finite lower saturation");
    ofColor_<PixelType> exactLower(static_cast<float>(lo), 0, 0, 0);
    expectEqual(exactLower.r, lo, name + " exact lower bound");
    const float roundedHigh = static_cast<float>(hi);
    const float inwardHigh = std::nextafter(roundedHigh,
                                            -std::numeric_limits<float>::infinity());
    ofColor_<PixelType> adjacentHigh(inwardHigh, 0, 0, 0);
    expectEqual(adjacentHigh.r, static_cast<PixelType>(inwardHigh),
                name + " inward value below rounded maximum");
    const float roundedLow = static_cast<float>(lo);
    const float inwardLow = std::nextafter(roundedLow,
                                           std::numeric_limits<float>::infinity());
    ofColor_<PixelType> adjacentLow(inwardLow, 0, 0, 0);
    expectEqual(adjacentLow.r, static_cast<PixelType>(inwardLow),
                name + " inward value above lower bound");
    ofColor_<PixelType> gray(12.75f, -3.75f);
    expectEqual(gray.r, 12, name + " fractional gray truncation");
    if constexpr (std::numeric_limits<PixelType>::is_signed) {
        expectEqual(gray.a, -3, name + " negative fractional alpha truncation");
        ofColor_<PixelType> interior(-12.75f, 2.0f, -1.25f, 1.0f);
        expectEqual(interior.r, -12, name + " signed interior r");
        expectEqual(interior.b, -1, name + " signed interior b");
    } else {
        expectEqual(gray.a, lo, name + " unsigned lower saturation");
    }
    ofColor_<PixelType> base(1, 2, 3, 4);
    ofColor_<PixelType> defaultAlpha(1, 2, 3);
    expectEqual(defaultAlpha.a, hi, name + " RGBA default alpha");
    ofColor_<PixelType> defaultGrayAlpha(5);
    expectEqual(defaultGrayAlpha.a, hi, name + " grayscale default alpha");
    ofColor_<PixelType> copyAlpha(base, std::numeric_limits<float>::infinity());
    expectEqual(copyAlpha.r, 1, name + " copy r");
    expectEqual(copyAlpha.a, hi, name + " copy alpha saturation");
}

template<typename PixelType>
void testFloating(const std::string & name) {
    ofColor_<PixelType> rgba(-2.5f, 0.25f, 4.5f, 8.0f);
    expectEqual(rgba.r, -2.5f, name + " negative HDR");
    expectEqual(rgba.g, 0.25f, name + " fractional");
    expectEqual(rgba.b, 4.5f, name + " positive HDR");
    ofColor_<PixelType> gray(3.25f, -4.5f);
    expectEqual(gray.r, 3.25f, name + " gray HDR");
    expectEqual(gray.a, -4.5f, name + " alpha HDR");
    ofColor_<PixelType> copyAlpha(rgba, 12.5f);
    expectEqual(copyAlpha.a, 12.5f, name + " copy alpha HDR");
}
}

int main() {
    testInteger<char>("char");
    testInteger<unsigned char>("unsigned char");
    testInteger<short>("short");
    testInteger<unsigned short>("unsigned short");
    testInteger<int>("int");
    testInteger<unsigned int>("unsigned int");
    testInteger<long>("long");
    testInteger<unsigned long>("unsigned long");
    testFloating<float>("float");
    testFloating<double>("double");
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "PASS actual overlaid ofColor constructors for 10 instantiated channel types\n";
    return EXIT_SUCCESS;
}
