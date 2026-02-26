#include "CommandParser.hpp"

namespace osc {

bool CommandParser::startsWith(const string& value, const string& prefix) const {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool CommandParser::isPrefixedCommand(const string& value, const string& prefix) const {
    return value == prefix || startsWith(value, prefix + "/");
}

Result<string> CommandParser::normalizePrefixedPath(const ofxOscMessage& message, const string& prefix) const {
    const string address = message.getAddress();
    if (!isPrefixedCommand(address, prefix)) {
        return Result<string>::fail(ParseError(
            ParseErrorCode::UNKNOWN_COMMAND,
            address,
            "invalid targeted command for prefix " + prefix + ": " + address
        ));
    }
    if (address == prefix) {
        return Result<string>::ok(prefix);
    }
    return Result<string>::ok(prefix + address.substr(prefix.size()));
}

Result<Layout> CommandParser::parseLayoutArg(const ofxOscMessage& message, int index) const {
    ArgReader reader(message);
    if (!reader.has(index)) {
        return Result<Layout>::fail(ParseError(
            ParseErrorCode::MISSING_ARGUMENT,
            message.getAddress(),
            "missing layout argument",
            index
        ));
    }
    if (message.getArgType(index) == OFXOSC_TYPE_STRING) {
        string name = message.getArgAsString(index);
        auto it = LayoutMap.find(name);
        if (it == LayoutMap.end()) {
            return Result<Layout>::fail(ParseError(
                ParseErrorCode::INVALID_VALUE,
                message.getAddress(),
                "invalid layout value: " + name,
                index
            ));
        }
        return Result<Layout>::ok(it->second);
    }

    auto intVal = reader.requireInt(index);
    if (!intVal.isOk()) {
        return Result<Layout>::fail(intVal.error());
    }
    return Result<Layout>::ok(static_cast<Layout>(intVal.value()));
}

Result<TargetSelector> CommandParser::parseTarget(const ofxOscMessage& message, int index) const {
    ArgReader reader(message);
    if (!reader.has(index)) {
        return Result<TargetSelector>::fail(ParseError(
            ParseErrorCode::MISSING_ARGUMENT,
            message.getAddress(),
            "missing target selector argument",
            index
        ));
    }

    TargetSelector target;
    const auto type = message.getArgType(index);
    if (type == OFXOSC_TYPE_STRING) {
        const string value = message.getArgAsString(index);
        if (value == "*" || value == "x" || value == "a") {
            target.kind = TargetKind::ALL;
            return Result<TargetSelector>::ok(target);
        }

        try {
            target.kind = TargetKind::INDEX;
            target.index = std::stoi(value);
            return Result<TargetSelector>::ok(target);
        }
        catch (...) {
            target.kind = TargetKind::NAME;
            target.name = value;
            return Result<TargetSelector>::ok(target);
        }
    }

    auto intVal = reader.requireInt(index);
    if (!intVal.isOk()) {
        return Result<TargetSelector>::fail(intVal.error());
    }
    target.kind = TargetKind::INDEX;
    target.index = intVal.value();
    return Result<TargetSelector>::ok(target);
}

Result<Command> CommandParser::parseLayersLike(const ofxOscMessage& message, CommandType type) const {
    Command command;
    command.type = type;
    command.address = message.getAddress();
    command.raw = message;

    ArgReader reader(message);
    if (reader.size() > 0) {
        auto layerCount = reader.requireInt(0);
        if (!layerCount.isOk()) {
            return Result<Command>::fail(layerCount.error());
        }
        command.layers.hasLayerCount = true;
        command.layers.layerCount = layerCount.value();
    }

    if (reader.size() > 1) {
        auto layout = parseLayoutArg(message, 1);
        if (!layout.isOk()) {
            return Result<Command>::fail(layout.error());
        }
        command.layers.hasLayout = true;
        command.layers.layout = layout.value();
    }

    return Result<Command>::ok(command);
}

Result<Command> CommandParser::parseLayers(const ofxOscMessage& message) const {
    return parseLayersLike(message, CommandType::LAYERS);
}

Result<Command> CommandParser::parseLayersReset(const ofxOscMessage& message) const {
    return parseLayersLike(message, CommandType::LAYERS_RESET);
}

Result<Command> CommandParser::parseLayersLayout(const ofxOscMessage& message) const {
    auto layout = parseLayoutArg(message, 0);
    if (!layout.isOk()) {
        return Result<Command>::fail(layout.error());
    }

    Command command;
    command.type = CommandType::LAYERS_LAYOUT;
    command.address = message.getAddress();
    command.raw = message;
    command.layers.hasLayout = true;
    command.layers.layout = layout.value();
    return Result<Command>::ok(command);
}

Result<Command> CommandParser::parseShading(const ofxOscMessage& message) const {
    ArgReader reader(message);
    auto mode = reader.requireString(0);
    if (!mode.isOk()) {
        return Result<Command>::fail(mode.error());
    }

    Command command;
    command.type = CommandType::SHADING_MODE;
    command.address = message.getAddress();
    command.raw = message;
    command.shadingMode = mode.value();
    return Result<Command>::ok(command);
}

Result<Command> CommandParser::parseInput(const ofxOscMessage& message) const {
    auto normalizedPath = normalizePrefixedPath(message, "/input");
    if (!normalizedPath.isOk()) {
        return Result<Command>::fail(normalizedPath.error());
    }

    Command command;
    command.type = CommandType::INPUT;
    command.address = message.getAddress();
    command.raw = message;
    command.input.commandPath = normalizedPath.value();
    if (command.input.commandPath == "/input/data") {
        command.input.action = InputAction::DATA;
    }
    else if (command.input.commandPath == "/input/list") {
        command.input.action = InputAction::LIST;
    }
    return Result<Command>::ok(command);
}

Result<Command> CommandParser::parseCamera(const ofxOscMessage& message) const {
    auto normalizedPath = normalizePrefixedPath(message, "/cam");
    if (!normalizedPath.isOk()) {
        return Result<Command>::fail(normalizedPath.error());
    }

    Command command;
    command.type = CommandType::CAMERA;
    command.address = message.getAddress();
    command.raw = message;
    command.camera.commandPath = normalizedPath.value();
    return Result<Command>::ok(command);
}

Result<Command> CommandParser::parseMidi(const ofxOscMessage& message) const {
    Command command;
    command.type = CommandType::MIDI;
    command.address = message.getAddress();
    command.raw = message;
    if (message.getAddress() == "/midi/list") {
        command.midiAction = MidiAction::LIST_PORTS;
    }
    return Result<Command>::ok(command);
}

Result<Command> CommandParser::parseShadingPasses(const ofxOscMessage& message) const {
    Command command;
    command.type = CommandType::SHADING_PASSES;
    command.address = message.getAddress();
    command.raw = message;

    for (int i = 0; i < message.getNumArgs(); ++i) {
        ShadingPassSpec pass;
        if (message.getArgType(i) == OFXOSC_TYPE_STRING) {
            pass.byName = true;
            pass.name = message.getArgAsString(i);
        }
        else if (message.getArgType(i) == OFXOSC_TYPE_INT32 || message.getArgType(i) == OFXOSC_TYPE_INT64 || message.getArgType(i) == OFXOSC_TYPE_CHAR) {
            pass.byName = false;
            pass.id = message.getArgAsInt(i);
        }
        else {
            return Result<Command>::fail(ParseError(
                ParseErrorCode::TYPE_MISMATCH,
                message.getAddress(),
                "shading pass argument must be string or int",
                i
            ));
        }
        command.shadingPasses.push_back(pass);
    }
    return Result<Command>::ok(command);
}

Result<Command> CommandParser::parseTargeted(const ofxOscMessage& message, ResourceDomain domain, const string& prefix) const {
    auto target = parseTarget(message, 0);
    if (!target.isOk()) {
        return Result<Command>::fail(target.error());
    }
    auto normalizedPath = normalizePrefixedPath(message, prefix);
    if (!normalizedPath.isOk()) {
        return Result<Command>::fail(normalizedPath.error());
    }

    Command command;
    command.type = CommandType::TARGETED_RESOURCE;
    command.address = message.getAddress();
    command.raw = message;
    command.resource.domain = domain;
    command.resource.commandPath = normalizedPath.value();
    if (domain == ResourceDomain::LAYER && command.resource.commandPath == "/layer/solo") {
        command.resource.action = ResourceAction::LAYER_SOLO;
    }
    command.resource.target = target.value();
    return Result<Command>::ok(command);
}

Result<Command> CommandParser::parse(const ofxOscMessage& message) const {
    const string address = message.getAddress();

    if (address == "/layers") {
        return parseLayers(message);
    }
    if (address == "/layers/reset") {
        return parseLayersReset(message);
    }
    if (address == "/layers/layout") {
        return parseLayersLayout(message);
    }
    if (address == "/shading") {
        return parseShading(message);
    }
    if (address == "/shading/passes") {
        return parseShadingPasses(message);
    }
    if (address == "/onset") {
        Command command;
        command.type = CommandType::ONSET;
        command.address = address;
        command.raw = message;
        ArgReader reader(message);
        if (reader.size() > 0) {
            auto value = reader.requireBool(0);
            if (!value.isOk()) {
                return Result<Command>::fail(value.error());
            }
            command.hasOnsetValue = true;
            command.onsetValue = value.value();
        }
        return Result<Command>::ok(command);
    }
    if (address == "/onset/force") {
        Command command;
        command.type = CommandType::ONSET_FORCE;
        command.address = address;
        command.raw = message;
        return Result<Command>::ok(command);
    }
    if (isPrefixedCommand(address, "/input")) {
        return parseInput(message);
    }
    if (startsWith(address, "/midi")) {
        return parseMidi(message);
    }
    if (address == "/dirt/play") {
        Command command;
        command.type = CommandType::DIRT_PLAY;
        command.address = address;
        command.raw = message;
        return Result<Command>::ok(command);
    }
    if (isPrefixedCommand(address, "/cam")) {
        return parseCamera(message);
    }
    if (address == "/light") {
        Command command;
        command.type = CommandType::LIGHT;
        command.address = address;
        command.raw = message;
        return Result<Command>::ok(command);
    }
    if (address == "/light/remove") {
        Command command;
        command.type = CommandType::LIGHT_REMOVE;
        command.address = address;
        command.raw = message;
        return Result<Command>::ok(command);
    }
    if (isPrefixedCommand(address, "/tex")) {
        return parseTargeted(message, ResourceDomain::TEX, "/tex");
    }
    if (isPrefixedCommand(address, "/var")) {
        return parseTargeted(message, ResourceDomain::VAR, "/var");
    }
    if (isPrefixedCommand(address, "/geom")) {
        return parseTargeted(message, ResourceDomain::GEOM, "/geom");
    }
    if (isPrefixedCommand(address, "/shader")) {
        return parseTargeted(message, ResourceDomain::SHADER, "/shader");
    }
    if (isPrefixedCommand(address, "/layer")) {
        return parseTargeted(message, ResourceDomain::LAYER, "/layer");
    }

    return Result<Command>::fail(ParseError(
        ParseErrorCode::UNKNOWN_COMMAND,
        address,
        "unknown OSC command: " + address
    ));
}

} // namespace osc
