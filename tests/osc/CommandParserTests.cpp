#include "osc/CommandParser.hpp"
#include "TestSupport.hpp"
#include <iostream>
#include <limits>

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
    m.addFloatArg(0.5f);

    Result<Command> result = parser.parse(m);
    TEST_REQUIRE(result.isOk());
    TEST_REQUIRE_EQ(result.value().type, CommandType::INPUT);
    TEST_REQUIRE_EQ(result.value().input.commandPath, "/input/data");
    TEST_REQUIRE_EQ(result.value().input.action, InputAction::DATA);
    return true;
}

static bool testLayoutShorthand() {
    CommandParser parser;
    for (const auto* address : {"/layers", "/layers/reset"}) {
        const auto empty = parser.parse(msg(address));
        TEST_REQUIRE(empty.isOk());
        TEST_REQUIRE(!empty.value().layers.hasLayerCount && !empty.value().layers.hasLayout);
        auto countOnly = msg(address);
        countOnly.addStringArg("12");
        const auto count = parser.parse(countOnly);
        TEST_REQUIRE(count.isOk());
        TEST_REQUIRE_EQ(count.value().layers.layerCount, 12);
        TEST_REQUIRE(!count.value().layers.hasLayout);
        for (const auto& [name, layout] : LayoutMap) {
            auto shorthand = msg(address);
            shorthand.addStringArg(name);
            const auto result = parser.parse(shorthand);
            TEST_REQUIRE(result.isOk() && result.value().layers.hasLayout);
            TEST_REQUIRE(!result.value().layers.hasLayerCount);
            TEST_REQUIRE_EQ(result.value().layers.layout, layout);
            auto explicitForm = msg(address);
            explicitForm.addIntArg(9);
            explicitForm.addIntArg(static_cast<int>(layout));
            const auto full = parser.parse(explicitForm);
            TEST_REQUIRE(full.isOk() && full.value().layers.hasLayerCount);
            TEST_REQUIRE_EQ(full.value().layers.layout, layout);
        }
    }

    auto malformed = msg("/layers");
    malformed.addStringArg("2grid");
    TEST_REQUIRE(!parser.parse(malformed).isOk());
    malformed = msg("/layers/reset");
    malformed.addStringArg("column");
    TEST_REQUIRE(!parser.parse(malformed).isOk());
    malformed = msg("/layers/reset");
    malformed.addIntArg(-1);
    TEST_REQUIRE(!parser.parse(malformed).isOk());
    auto layout = msg("/layers/layout");
    layout.addIntArg(5);
    TEST_REQUIRE(!parser.parse(layout).isOk());
    layout = msg("/layers/layout");
    layout.addStringArg("row");
    TEST_REQUIRE(parser.parse(layout).isOk());
    layout.addIntArg(1);
    TEST_REQUIRE(!parser.parse(layout).isOk());
    return true;
}

static bool testInputSetupDefaults() {
    CommandParser parser;
    auto named = msg("/input");
    named.addStringArg("mic");
    const auto external = parser.parse(named);
    TEST_REQUIRE(external.isOk());
    TEST_REQUIRE_EQ(external.value().raw.getTypeString(), "ssii");
    TEST_REQUIRE_EQ(external.value().raw.getArgAsString(1), "");
    TEST_REQUIRE_EQ(external.value().raw.getArgAsInt(2), 0);
    TEST_REQUIRE_EQ(external.value().raw.getArgAsInt(3), 256);

    auto numeric = msg("/input");
    numeric.addInt64Arg(std::numeric_limits<int32_t>::max());
    const auto integer = parser.parse(numeric);
    TEST_REQUIRE(integer.isOk());
    TEST_REQUIRE_EQ(integer.value().raw.getTypeString(), "hsii");
    TEST_REQUIRE_EQ(integer.value().raw.getArgAsInt64(0), std::numeric_limits<int32_t>::max());
    numeric.addStringArg("Microphone");
    const auto physical = parser.parse(numeric);
    TEST_REQUIRE(physical.isOk());
    TEST_REQUIRE_EQ(physical.value().raw.getArgAsInt(2), 1);
    TEST_REQUIRE_EQ(physical.value().raw.getArgAsInt(3), 256);
    numeric.addIntArg(2);
    const auto channels = parser.parse(numeric);
    TEST_REQUIRE(channels.isOk());
    TEST_REQUIRE_EQ(channels.value().raw.getArgAsInt(2), 2);
    TEST_REQUIRE_EQ(channels.value().raw.getArgAsInt(3), 256);

    auto wildcard = msg("/input");
    wildcard.addStringArg("*");
    TEST_REQUIRE(!parser.parse(wildcard).isOk());
    wildcard.addStringArg("");
    wildcard.addIntArg(0);
    TEST_REQUIRE(!parser.parse(wildcard).isOk());
    wildcard.addIntArg(256);
    TEST_REQUIRE(parser.parse(wildcard).isOk());
    wildcard.addIntArg(1);
    TEST_REQUIRE(!parser.parse(wildcard).isOk());
    return true;
}

static bool testInputSampleDefaultsAndValidation() {
    CommandParser parser;
    auto sample = msg("/input/data");
    sample.addStringArg("*");
    TEST_REQUIRE_EQ(parser.parse(sample).error().code, ParseErrorCode::MISSING_ARGUMENT);
    sample.addFloatArg(.5f);
    const auto shortSample = parser.parse(sample);
    TEST_REQUIRE(shortSample.isOk());
    TEST_REQUIRE_EQ(shortSample.value().raw.getTypeString(), "sffi");
    TEST_REQUIRE_EQ(shortSample.value().raw.getArgAsFloat(1), .5f);
    TEST_REQUIRE_EQ(shortSample.value().raw.getArgAsFloat(2), 0.f);
    TEST_REQUIRE_EQ(shortSample.value().raw.getArgAsInt(3), 0);
    sample.addDoubleArg(.125);
    const auto loudness = parser.parse(sample);
    TEST_REQUIRE(loudness.isOk());
    TEST_REQUIRE_EQ(loudness.value().raw.getTypeString(), "sfdi");
    TEST_REQUIRE_EQ(loudness.value().raw.getArgAsDouble(2), .125);
    TEST_REQUIRE_EQ(loudness.value().raw.getArgAsInt(3), 0);
    sample.addBoolArg(true);
    const auto onset = parser.parse(sample);
    TEST_REQUIRE(onset.isOk());
    TEST_REQUIRE_EQ(onset.value().raw.getArgAsInt(3), 1);
    sample.addFloatArg(.25f);
    TEST_REQUIRE(parser.parse(sample).isOk());
    sample.addIntArg(2);
    const auto tail = parser.parse(sample);
    TEST_REQUIRE(tail.isOk());
    TEST_REQUIRE_EQ(tail.value().raw.getTypeString(), "sfdTfi");
    sample.addDoubleArg(std::numeric_limits<double>::max());
    const auto overflow = parser.parse(sample);
    TEST_REQUIRE(!overflow.isOk());
    TEST_REQUIRE_EQ(overflow.error().argIndex, 6);

    auto badOnset = msg("/input/data");
    badOnset.addIntArg(0);
    badOnset.addFloatArg(.5f);
    badOnset.addFloatArg(0.f);
    badOnset.addDoubleArg(2147483647.5);
    TEST_REQUIRE(!parser.parse(badOnset).isOk());
    auto target = msg("/input");
    target.addInt64Arg(4294967296LL);
    TEST_REQUIRE(!parser.parse(target).isOk());
    return true;
}

static bool testInputFixedFormsRejectInvalidPayloads() {
    CommandParser parser;
    TEST_REQUIRE_EQ(parser.parse(msg("/input/unknown")).error().code, ParseErrorCode::UNKNOWN_COMMAND);
    auto list = msg("/input/list");
    list.addIntArg(0);
    TEST_REQUIRE(!parser.parse(list).isOk());
    auto setup = msg("/input");
    setup.addIntArg(0);
    setup.addStringArg("");
    setup.addDoubleArg(.5);
    TEST_REQUIRE(!parser.parse(setup).isOk());
    for (const auto* property : {"maxAmp", "maxLoud"}) {
        auto set = msg("/input/set");
        set.addIntArg(0);
        set.addStringArg(property);
        TEST_REQUIRE(!parser.parse(set).isOk());
        auto good = set;
        good.addFloatArg(1.f);
        TEST_REQUIRE(parser.parse(good).isOk());
        good.addFloatArg(1.f);
        TEST_REQUIRE(!parser.parse(good).isOk());
        auto nonfinite = set;
        nonfinite.addFloatArg(std::numeric_limits<float>::infinity());
        TEST_REQUIRE(!parser.parse(nonfinite).isOk());
        set.addIntArg(1);
        TEST_REQUIRE(!parser.parse(set).isOk());
    }
    auto unknown = msg("/input/set");
    unknown.addStringArg("mic");
    unknown.addStringArg("maxUnknown");
    unknown.addFloatArg(1.f);
    TEST_REQUIRE(!parser.parse(unknown).isOk());
    auto nan = msg("/input/data");
    nan.addStringArg("mic");
    nan.addFloatArg(std::numeric_limits<float>::quiet_NaN());
    TEST_REQUIRE(!parser.parse(nan).isOk());
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

static bool testStrictLayerAndMaterialSelectors() {
    CommandParser parser;
    for (const char* address : {"/layer/visible", "/mat/roughness"}) {
        for (const char* alias : {"*", "x", "a"}) {
            auto m = msg(address);
            m.addStringArg(alias);
            const auto result = parser.parse(m);
            TEST_REQUIRE(result.isOk());
            TEST_REQUIRE_EQ(result.value().resource.target.kind, TargetKind::ALL);
        }
        auto integer = msg(address);
        integer.addIntArg(0);
        const auto zero = parser.parse(integer);
        TEST_REQUIRE(zero.isOk());
        TEST_REQUIRE_EQ(zero.value().resource.target.kind, TargetKind::INDEX);
        TEST_REQUIRE_EQ(zero.value().resource.target.index, 0);

        auto decimal = msg(address);
        decimal.addStringArg("002147483647");
        const auto max = parser.parse(decimal);
        TEST_REQUIRE(max.isOk());
        TEST_REQUIRE_EQ(max.value().resource.target.kind, TargetKind::INDEX);
        TEST_REQUIRE_EQ(max.value().resource.target.index, 2147483647);

        for (const char* invalid : {"1junk", "2147483648", "-1", "-0", "+1", " 1", "1 ",
                                    "1.0", "0x1", "", "named"}) {
            auto m = msg(address);
            m.addStringArg(invalid);
            const auto result = parser.parse(m);
            TEST_REQUIRE(!result.isOk());
            TEST_REQUIRE_EQ(result.error().code, ParseErrorCode::INVALID_VALUE);
        }
        auto negative = msg(address);
        negative.addIntArg(-1);
        const auto result = parser.parse(negative);
        TEST_REQUIRE(!result.isOk());
        TEST_REQUIRE_EQ(result.error().code, ParseErrorCode::INVALID_VALUE);

        auto floating = msg(address);
        floating.addFloatArg(1.f);
        auto doublePrecision = msg(address);
        doublePrecision.addDoubleArg(1.0);
        auto wide = msg(address);
        wide.addInt64Arg(1);
        for (const auto& m : {floating, doublePrecision, wide}) {
            const auto wrongType = parser.parse(m);
            TEST_REQUIRE(!wrongType.isOk());
            TEST_REQUIRE_EQ(wrongType.error().code, ParseErrorCode::TYPE_MISMATCH);
        }
    }
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
    RUN_TEST(testLayoutShorthand);
    RUN_TEST(testInputSetupDefaults);
    RUN_TEST(testInputSampleDefaultsAndValidation);
    RUN_TEST(testInputFixedFormsRejectInvalidPayloads);
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
    RUN_TEST(testStrictLayerAndMaterialSelectors);
    RUN_TEST(testMidiPrefixMatchDoesNotAcceptSimilarWords);

#undef RUN_TEST

    std::cout << "CommandParserTests: " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
