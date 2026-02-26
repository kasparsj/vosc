#include "osc/CommandParser.hpp"
#include "TestSupport.hpp"
#include <iostream>

using namespace osc;

static bool testLayersParsesCountAndLayout() {
    CommandParser parser;
    ofxOscMessage m = msg("/layers");
    m.addIntArg(4);
    m.addStringArg("grid");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(result.isOk());
    TEST_REQUIRE_EQ(result.value().type, CommandType::LAYERS);
    TEST_REQUIRE(result.value().layers.hasLayerCount);
    TEST_REQUIRE_EQ(result.value().layers.layerCount, 4);
    TEST_REQUIRE(result.value().layers.hasLayout);
    TEST_REQUIRE_EQ(result.value().layers.layout, Layout::GRID);
    return true;
}

static bool testLayersResetParsesCountAndLayout() {
    CommandParser parser;
    ofxOscMessage m = msg("/layers/reset");
    m.addIntArg(2);
    m.addStringArg("stack");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(result.isOk());
    TEST_REQUIRE_EQ(result.value().type, CommandType::LAYERS_RESET);
    TEST_REQUIRE(result.value().layers.hasLayerCount);
    TEST_REQUIRE_EQ(result.value().layers.layerCount, 2);
    TEST_REQUIRE(result.value().layers.hasLayout);
    TEST_REQUIRE_EQ(result.value().layers.layout, Layout::STACK);
    return true;
}

static bool testTargetedResourceRequiresTarget() {
    CommandParser parser;
    ofxOscMessage m = msg("/tex");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(!result.isOk());
    TEST_REQUIRE_EQ(result.error().code, ParseErrorCode::MISSING_ARGUMENT);
    return true;
}

static bool testTargetedResourceNameTarget() {
    CommandParser parser;
    ofxOscMessage m = msg("/shader");
    m.addStringArg("main");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(result.isOk());
    TEST_REQUIRE_EQ(result.value().type, CommandType::TARGETED_RESOURCE);
    TEST_REQUIRE_EQ(result.value().resource.domain, ResourceDomain::SHADER);
    TEST_REQUIRE_EQ(result.value().resource.commandPath, "/shader");
    TEST_REQUIRE_EQ(result.value().resource.target.kind, TargetKind::NAME);
    TEST_REQUIRE_EQ(result.value().resource.target.name, "main");
    return true;
}

static bool testUnknownCommandFails() {
    CommandParser parser;
    ofxOscMessage m = msg("/does/not/exist");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(!result.isOk());
    TEST_REQUIRE_EQ(result.error().code, ParseErrorCode::UNKNOWN_COMMAND);
    return true;
}

static bool testOnsetParsesOptionalBool() {
    CommandParser parser;
    ofxOscMessage m = msg("/onset");
    m.addIntArg(1);

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(result.isOk());
    TEST_REQUIRE_EQ(result.value().type, CommandType::ONSET);
    TEST_REQUIRE(result.value().hasOnsetValue);
    TEST_REQUIRE(result.value().onsetValue);
    return true;
}

static bool testLayerSoloParsesAction() {
    CommandParser parser;
    ofxOscMessage m = msg("/layer/solo");
    m.addIntArg(2);

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(result.isOk());
    TEST_REQUIRE_EQ(result.value().type, CommandType::TARGETED_RESOURCE);
    TEST_REQUIRE_EQ(result.value().resource.domain, ResourceDomain::LAYER);
    TEST_REQUIRE_EQ(result.value().resource.commandPath, "/layer/solo");
    TEST_REQUIRE_EQ(result.value().resource.action, ResourceAction::LAYER_SOLO);
    TEST_REQUIRE_EQ(result.value().resource.target.kind, TargetKind::INDEX);
    TEST_REQUIRE_EQ(result.value().resource.target.index, 2);
    return true;
}

static bool testTargetedResourceKeepsSubPath() {
    CommandParser parser;
    ofxOscMessage m = msg("/tex/shader/var");
    m.addStringArg("main");
    m.addStringArg("u_time");
    m.addFloatArg(1.0f);

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(result.isOk());
    TEST_REQUIRE_EQ(result.value().type, CommandType::TARGETED_RESOURCE);
    TEST_REQUIRE_EQ(result.value().resource.domain, ResourceDomain::TEX);
    TEST_REQUIRE_EQ(result.value().resource.commandPath, "/tex/shader/var");
    return true;
}

static bool testMaterialParsesAsTargetedResource() {
    CommandParser parser;
    ofxOscMessage m = msg("/mat/diffuse");
    m.addIntArg(1);
    m.addFloatArg(1.0f);
    m.addFloatArg(0.5f);
    m.addFloatArg(0.25f);

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(result.isOk());
    TEST_REQUIRE_EQ(result.value().type, CommandType::TARGETED_RESOURCE);
    TEST_REQUIRE_EQ(result.value().resource.domain, ResourceDomain::LAYER);
    TEST_REQUIRE_EQ(result.value().resource.commandPath, "/mat/diffuse");
    TEST_REQUIRE_EQ(result.value().resource.target.kind, TargetKind::INDEX);
    TEST_REQUIRE_EQ(result.value().resource.target.index, 1);
    return true;
}

static bool testPrefixMatchDoesNotAcceptSimilarWords() {
    CommandParser parser;
    ofxOscMessage m = msg("/texture");
    m.addStringArg("main");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(!result.isOk());
    TEST_REQUIRE_EQ(result.error().code, ParseErrorCode::UNKNOWN_COMMAND);
    return true;
}

static bool testInputParsesTypedPathAndAction() {
    CommandParser parser;
    ofxOscMessage m = msg("/input/data");
    m.addStringArg("mic");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(result.isOk());
    TEST_REQUIRE_EQ(result.value().type, CommandType::INPUT);
    TEST_REQUIRE_EQ(result.value().input.commandPath, "/input/data");
    TEST_REQUIRE_EQ(result.value().input.action, InputAction::DATA);
    return true;
}

static bool testInputListParsesTypedAction() {
    CommandParser parser;
    ofxOscMessage m = msg("/input/list");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(result.isOk());
    TEST_REQUIRE_EQ(result.value().type, CommandType::INPUT);
    TEST_REQUIRE_EQ(result.value().input.commandPath, "/input/list");
    TEST_REQUIRE_EQ(result.value().input.action, InputAction::LIST);
    return true;
}

static bool testInputPrefixMatchDoesNotAcceptSimilarWords() {
    CommandParser parser;
    ofxOscMessage m = msg("/inputting");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(!result.isOk());
    TEST_REQUIRE_EQ(result.error().code, ParseErrorCode::UNKNOWN_COMMAND);
    return true;
}

static bool testCameraParsesTypedPath() {
    CommandParser parser;
    ofxOscMessage m = msg("/cam/reset");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(result.isOk());
    TEST_REQUIRE_EQ(result.value().type, CommandType::CAMERA);
    TEST_REQUIRE_EQ(result.value().camera.commandPath, "/cam/reset");
    return true;
}

static bool testCameraPrefixMatchDoesNotAcceptSimilarWords() {
    CommandParser parser;
    ofxOscMessage m = msg("/camera");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(!result.isOk());
    TEST_REQUIRE_EQ(result.error().code, ParseErrorCode::UNKNOWN_COMMAND);
    return true;
}

static bool testMidiListParsesTypedAction() {
    CommandParser parser;
    ofxOscMessage m = msg("/midi/list");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(result.isOk());
    TEST_REQUIRE_EQ(result.value().type, CommandType::MIDI);
    TEST_REQUIRE_EQ(result.value().midiAction, MidiAction::LIST_PORTS);
    return true;
}

static bool testMidiDefaultParsesForwardAction() {
    CommandParser parser;
    ofxOscMessage m = msg("/midi");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(result.isOk());
    TEST_REQUIRE_EQ(result.value().type, CommandType::MIDI);
    TEST_REQUIRE_EQ(result.value().midiAction, MidiAction::FORWARD);
    return true;
}

static bool testLayersLayoutRejectsMissingArg() {
    CommandParser parser;
    ofxOscMessage m = msg("/layers/layout");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(!result.isOk());
    TEST_REQUIRE_EQ(result.error().code, ParseErrorCode::MISSING_ARGUMENT);
    return true;
}

static bool testLayersLayoutRejectsInvalidString() {
    CommandParser parser;
    ofxOscMessage m = msg("/layers/layout");
    m.addStringArg("diagonal");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(!result.isOk());
    TEST_REQUIRE_EQ(result.error().code, ParseErrorCode::INVALID_VALUE);
    return true;
}

static bool testOnsetRejectsInvalidBoolString() {
    CommandParser parser;
    ofxOscMessage m = msg("/onset");
    m.addStringArg("enable-ish");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(!result.isOk());
    TEST_REQUIRE_EQ(result.error().code, ParseErrorCode::TYPE_MISMATCH);
    return true;
}

static bool testTargetedResourceParsesWildcardTarget() {
    CommandParser parser;
    ofxOscMessage m = msg("/layer");
    m.addStringArg("*");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(result.isOk());
    TEST_REQUIRE_EQ(result.value().resource.target.kind, TargetKind::ALL);
    return true;
}

static bool testTargetedResourceParsesIndexFromString() {
    CommandParser parser;
    ofxOscMessage m = msg("/layer");
    m.addStringArg("12");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(result.isOk());
    TEST_REQUIRE_EQ(result.value().resource.target.kind, TargetKind::INDEX);
    TEST_REQUIRE_EQ(result.value().resource.target.index, 12);
    return true;
}

static bool testMidiPrefixMatchDoesNotAcceptSimilarWords() {
    CommandParser parser;
    ofxOscMessage m = msg("/midiing");

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(!result.isOk());
    TEST_REQUIRE_EQ(result.error().code, ParseErrorCode::UNKNOWN_COMMAND);
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

    RUN_TEST(testLayersParsesCountAndLayout);
    RUN_TEST(testLayersResetParsesCountAndLayout);
    RUN_TEST(testTargetedResourceRequiresTarget);
    RUN_TEST(testTargetedResourceNameTarget);
    RUN_TEST(testUnknownCommandFails);
    RUN_TEST(testOnsetParsesOptionalBool);
    RUN_TEST(testLayerSoloParsesAction);
    RUN_TEST(testTargetedResourceKeepsSubPath);
    RUN_TEST(testMaterialParsesAsTargetedResource);
    RUN_TEST(testPrefixMatchDoesNotAcceptSimilarWords);
    RUN_TEST(testInputParsesTypedPathAndAction);
    RUN_TEST(testInputListParsesTypedAction);
    RUN_TEST(testInputPrefixMatchDoesNotAcceptSimilarWords);
    RUN_TEST(testCameraParsesTypedPath);
    RUN_TEST(testCameraPrefixMatchDoesNotAcceptSimilarWords);
    RUN_TEST(testMidiListParsesTypedAction);
    RUN_TEST(testMidiDefaultParsesForwardAction);
    RUN_TEST(testLayersLayoutRejectsMissingArg);
    RUN_TEST(testLayersLayoutRejectsInvalidString);
    RUN_TEST(testOnsetRejectsInvalidBoolString);
    RUN_TEST(testTargetedResourceParsesWildcardTarget);
    RUN_TEST(testTargetedResourceParsesIndexFromString);
    RUN_TEST(testMidiPrefixMatchDoesNotAcceptSimilarWords);

#undef RUN_TEST

    std::cout << "CommandParserTests: " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
