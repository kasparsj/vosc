#include "osc/CommandParser.hpp"
#include <cassert>
#include <iostream>

using namespace osc;

static ofxOscMessage msg(const string& address) {
    ofxOscMessage m;
    m.setAddress(address);
    return m;
}

static void testLayersParsesCountAndLayout() {
    CommandParser parser;
    ofxOscMessage m = msg("/layers");
    m.addIntArg(4);
    m.addStringArg("grid");

    Result<Command> result = parser.parse(m);
    assert(result.isOk());
    assert(result.value().type == CommandType::LAYERS);
    assert(result.value().layers.hasLayerCount);
    assert(result.value().layers.layerCount == 4);
    assert(result.value().layers.hasLayout);
    assert(result.value().layers.layout == Layout::GRID);
}

static void testLayersResetParsesCountAndLayout() {
    CommandParser parser;
    ofxOscMessage m = msg("/layers/reset");
    m.addIntArg(2);
    m.addStringArg("stack");

    Result<Command> result = parser.parse(m);
    assert(result.isOk());
    assert(result.value().type == CommandType::LAYERS_RESET);
    assert(result.value().layers.hasLayerCount);
    assert(result.value().layers.layerCount == 2);
    assert(result.value().layers.hasLayout);
    assert(result.value().layers.layout == Layout::STACK);
}

static void testTargetedResourceRequiresTarget() {
    CommandParser parser;
    ofxOscMessage m = msg("/tex");

    Result<Command> result = parser.parse(m);
    assert(!result.isOk());
    assert(result.error().code == ParseErrorCode::MISSING_ARGUMENT);
}

static void testTargetedResourceNameTarget() {
    CommandParser parser;
    ofxOscMessage m = msg("/shader");
    m.addStringArg("main");

    Result<Command> result = parser.parse(m);
    assert(result.isOk());
    assert(result.value().type == CommandType::TARGETED_RESOURCE);
    assert(result.value().resource.domain == ResourceDomain::SHADER);
    assert(result.value().resource.commandPath == "/shader");
    assert(result.value().resource.target.kind == TargetKind::NAME);
    assert(result.value().resource.target.name == "main");
}

static void testUnknownCommandFails() {
    CommandParser parser;
    ofxOscMessage m = msg("/does/not/exist");

    Result<Command> result = parser.parse(m);
    assert(!result.isOk());
    assert(result.error().code == ParseErrorCode::UNKNOWN_COMMAND);
}

static void testOnsetParsesOptionalBool() {
    CommandParser parser;
    ofxOscMessage m = msg("/onset");
    m.addIntArg(1);

    Result<Command> result = parser.parse(m);
    assert(result.isOk());
    assert(result.value().type == CommandType::ONSET);
    assert(result.value().hasOnsetValue);
    assert(result.value().onsetValue);
}

static void testLayerSoloParsesAction() {
    CommandParser parser;
    ofxOscMessage m = msg("/layer/solo");
    m.addIntArg(2);

    Result<Command> result = parser.parse(m);
    assert(result.isOk());
    assert(result.value().type == CommandType::TARGETED_RESOURCE);
    assert(result.value().resource.domain == ResourceDomain::LAYER);
    assert(result.value().resource.commandPath == "/layer/solo");
    assert(result.value().resource.action == ResourceAction::LAYER_SOLO);
    assert(result.value().resource.target.kind == TargetKind::INDEX);
    assert(result.value().resource.target.index == 2);
}

static void testTargetedResourceKeepsSubPath() {
    CommandParser parser;
    ofxOscMessage m = msg("/tex/shader/var");
    m.addStringArg("main");
    m.addStringArg("u_time");
    m.addFloatArg(1.0f);

    Result<Command> result = parser.parse(m);
    assert(result.isOk());
    assert(result.value().type == CommandType::TARGETED_RESOURCE);
    assert(result.value().resource.domain == ResourceDomain::TEX);
    assert(result.value().resource.commandPath == "/tex/shader/var");
}

static void testPrefixMatchDoesNotAcceptSimilarWords() {
    CommandParser parser;
    ofxOscMessage m = msg("/texture");
    m.addStringArg("main");

    Result<Command> result = parser.parse(m);
    assert(!result.isOk());
    assert(result.error().code == ParseErrorCode::UNKNOWN_COMMAND);
}

static void testInputParsesTypedPathAndAction() {
    CommandParser parser;
    ofxOscMessage m = msg("/input/data");
    m.addStringArg("mic");

    Result<Command> result = parser.parse(m);
    assert(result.isOk());
    assert(result.value().type == CommandType::INPUT);
    assert(result.value().input.commandPath == "/input/data");
    assert(result.value().input.action == InputAction::DATA);
}

static void testInputListParsesTypedAction() {
    CommandParser parser;
    ofxOscMessage m = msg("/input/list");

    Result<Command> result = parser.parse(m);
    assert(result.isOk());
    assert(result.value().type == CommandType::INPUT);
    assert(result.value().input.commandPath == "/input/list");
    assert(result.value().input.action == InputAction::LIST);
}

static void testInputPrefixMatchDoesNotAcceptSimilarWords() {
    CommandParser parser;
    ofxOscMessage m = msg("/inputting");

    Result<Command> result = parser.parse(m);
    assert(!result.isOk());
    assert(result.error().code == ParseErrorCode::UNKNOWN_COMMAND);
}

static void testCameraParsesTypedPath() {
    CommandParser parser;
    ofxOscMessage m = msg("/cam/reset");

    Result<Command> result = parser.parse(m);
    assert(result.isOk());
    assert(result.value().type == CommandType::CAMERA);
    assert(result.value().camera.commandPath == "/cam/reset");
}

static void testCameraPrefixMatchDoesNotAcceptSimilarWords() {
    CommandParser parser;
    ofxOscMessage m = msg("/camera");

    Result<Command> result = parser.parse(m);
    assert(!result.isOk());
    assert(result.error().code == ParseErrorCode::UNKNOWN_COMMAND);
}

static void testMidiListParsesTypedAction() {
    CommandParser parser;
    ofxOscMessage m = msg("/midi/list");

    Result<Command> result = parser.parse(m);
    assert(result.isOk());
    assert(result.value().type == CommandType::MIDI);
    assert(result.value().midiAction == MidiAction::LIST_PORTS);
}

static void testMidiDefaultParsesForwardAction() {
    CommandParser parser;
    ofxOscMessage m = msg("/midi");

    Result<Command> result = parser.parse(m);
    assert(result.isOk());
    assert(result.value().type == CommandType::MIDI);
    assert(result.value().midiAction == MidiAction::FORWARD);
}

int main() {
    testLayersParsesCountAndLayout();
    testLayersResetParsesCountAndLayout();
    testTargetedResourceRequiresTarget();
    testTargetedResourceNameTarget();
    testUnknownCommandFails();
    testOnsetParsesOptionalBool();
    testLayerSoloParsesAction();
    testTargetedResourceKeepsSubPath();
    testPrefixMatchDoesNotAcceptSimilarWords();
    testInputParsesTypedPathAndAction();
    testInputListParsesTypedAction();
    testInputPrefixMatchDoesNotAcceptSimilarWords();
    testCameraParsesTypedPath();
    testCameraPrefixMatchDoesNotAcceptSimilarWords();
    testMidiListParsesTypedAction();
    testMidiDefaultParsesForwardAction();

    std::cout << "CommandParserTests: all tests passed\n";
    return 0;
}
