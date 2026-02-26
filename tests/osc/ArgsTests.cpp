#include "Args.h"
#include "TestSupport.hpp"
#include <iostream>

static bool testParseIntFromString() {
    ofxOscMessage m = msg("/var");
    m.addStringArg("42");

    const int value = Args::parse<int>(m, 0);
    TEST_REQUIRE_EQ(value, 42);
    return true;
}

static bool testParseIntRejectsInvalidString() {
    ofxOscMessage m = msg("/var");
    m.addStringArg("12x");

    TEST_REQUIRE(throwsAny([&]() {
        (void) Args::parse<int>(m, 0);
    }));
    return true;
}

static bool testParseFloatFromString() {
    ofxOscMessage m = msg("/var");
    m.addStringArg("3.5");

    const float value = Args::parse<float>(m, 0);
    TEST_REQUIRE(almostEqual(value, 3.5f));
    return true;
}

static bool testParseFloatRejectsEmptyString() {
    ofxOscMessage m = msg("/var");
    m.addStringArg("   ");

    TEST_REQUIRE(throwsAny([&]() {
        (void) Args::parse<float>(m, 0);
    }));
    return true;
}

static bool testParsePercentHandlesIntAndFractionInputs() {
    ofxOscMessage m = msg("/var");
    m.addIntArg(50);
    m.addFloatArg(0.25f);
    m.addFloatArg(2.5f);

    TEST_REQUIRE(almostEqual(Args::parsePercent(m, 0), 0.5f));
    TEST_REQUIRE(almostEqual(Args::parsePercent(m, 1), 0.25f));
    TEST_REQUIRE(almostEqual(Args::parsePercent(m, 2), 0.025f));
    return true;
}

static bool testParseColorFromHexString() {
    ofxOscMessage m = msg("/color");
    m.addStringArg("#FF0000");

    ofFloatColor color = Args::parse<ofFloatColor>(m, 0);
    TEST_REQUIRE(almostEqual(color.r, 1.f));
    TEST_REQUIRE(almostEqual(color.g, 0.f));
    TEST_REQUIRE(almostEqual(color.b, 0.f));
    TEST_REQUIRE(almostEqual(color.a, 1.f));
    return true;
}

static bool testParseColorFromJsonObjectNormalizes255Range() {
    ofxOscMessage m = msg("/color");
    m.addStringArg("{\"r\":255,\"g\":128,\"b\":0,\"a\":64}");

    ofFloatColor color = Args::parse<ofFloatColor>(m, 0);
    TEST_REQUIRE(almostEqual(color.r, 1.f));
    TEST_REQUIRE(almostEqual(color.g, 128.f / 255.f));
    TEST_REQUIRE(almostEqual(color.b, 0.f));
    TEST_REQUIRE(almostEqual(color.a, 64.f / 255.f));
    return true;
}

static bool testParseVec2FromJsonObject() {
    ofxOscMessage m = msg("/vec2");
    m.addStringArg("{\"x\":1.5,\"y\":2}");

    glm::vec2 vec = Args::parse<glm::vec2>(m, 0);
    TEST_REQUIRE(almostEqual(vec.x, 1.5f));
    TEST_REQUIRE(almostEqual(vec.y, 2.f));
    return true;
}

static bool testParseVec3FromJsonArray() {
    ofxOscMessage m = msg("/vec3");
    m.addStringArg("[1,2,3]");

    glm::vec3 vec = Args::parse<glm::vec3>(m, 0);
    TEST_REQUIRE(almostEqual(vec.x, 1.f));
    TEST_REQUIRE(almostEqual(vec.y, 2.f));
    TEST_REQUIRE(almostEqual(vec.z, 3.f));
    return true;
}

static bool testParseConstReturnsEmptyWhenNoValuesProvided() {
    ofxOscMessage m = msg("/empty");

    const vector<int> values = Args::parseConst<int>(m, 0);
    TEST_REQUIRE(values.empty());
    return true;
}

static bool testParseConstReadsValuesFromStartIndex() {
    ofxOscMessage m = msg("/values");
    m.addStringArg("ignored");
    m.addIntArg(7);
    m.addIntArg(9);

    const vector<int> values = Args::parseConst<int>(m, 1);
    TEST_REQUIRE_EQ(values.size(), static_cast<size_t>(2));
    TEST_REQUIRE_EQ(values[0], 7);
    TEST_REQUIRE_EQ(values[1], 9);
    return true;
}

int main() {
    int failed = 0;
    int passed = 0;

#define RUN_TEST(fn)                                            \
    do {                                                        \
        if (fn()) {                                             \
            ++passed;                                           \
        }                                                       \
        else {                                                  \
            ++failed;                                           \
            std::cerr << "FAILED: " << #fn << std::endl;       \
        }                                                       \
    } while (0)

    RUN_TEST(testParseIntFromString);
    RUN_TEST(testParseIntRejectsInvalidString);
    RUN_TEST(testParseFloatFromString);
    RUN_TEST(testParseFloatRejectsEmptyString);
    RUN_TEST(testParsePercentHandlesIntAndFractionInputs);
    RUN_TEST(testParseColorFromHexString);
    RUN_TEST(testParseColorFromJsonObjectNormalizes255Range);
    RUN_TEST(testParseVec2FromJsonObject);
    RUN_TEST(testParseVec3FromJsonArray);
    RUN_TEST(testParseConstReturnsEmptyWhenNoValuesProvided);
    RUN_TEST(testParseConstReadsValuesFromStartIndex);

#undef RUN_TEST

    std::cout << "ArgsTests: " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
