#include "CommandParser.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>

namespace osc {
namespace {

bool fitsInt32(double value) {
    return value >= std::numeric_limits<std::int32_t>::min() &&
        value <= std::numeric_limits<std::int32_t>::max() && std::trunc(value) == value;
}

bool isInt32Value(const ofxOscMessage& message, int index) {
    switch (message.getArgType(index)) {
        case OFXOSC_TYPE_INT32:
            return true;
        case OFXOSC_TYPE_INT64: {
            const auto value = message.getArgAsInt64(index);
            return value >= std::numeric_limits<std::int32_t>::min() &&
                value <= std::numeric_limits<std::int32_t>::max();
        }
        case OFXOSC_TYPE_FLOAT:
        case OFXOSC_TYPE_DOUBLE:
            return fitsInt32(message.getArgAsDouble(index));
        default:
            return false;
    }
}

bool isOscBool(ofxOscArgType type) {
    return type == OFXOSC_TYPE_TRUE || type == OFXOSC_TYPE_FALSE;
}

bool isSampleValue(const ofxOscMessage& message, int index) {
    switch (message.getArgType(index)) {
        case OFXOSC_TYPE_INT32:
        case OFXOSC_TYPE_INT64:
        case OFXOSC_TYPE_TRUE:
        case OFXOSC_TYPE_FALSE:
            return true;
        case OFXOSC_TYPE_FLOAT:
        case OFXOSC_TYPE_DOUBLE:
            return std::abs(message.getArgAsDouble(index)) <= std::numeric_limits<float>::max();
        default:
            return false;
    }
}

ParseError invalidArgument(const ofxOscMessage& message, int index, const char* detail) {
    return {ParseErrorCode::INVALID_VALUE, message.getAddress(), detail, index};
}

Result<int> readLayerCount(const ofxOscMessage& message) {
    const auto type = message.getArgType(0);
    int count = -1;
    if (type == OFXOSC_TYPE_STRING) {
        const auto text = ofTrim(message.getArgAsString(0));
        const auto* end = text.data() + text.size();
        const auto parsed = std::from_chars(text.data(), end, count);
        if (parsed.ec != std::errc{} || parsed.ptr != end) count = -1;
    } else if (isInt32Value(message, 0)) {
        count = message.getArgAsInt(0);
    } else if (type == OFXOSC_TYPE_CHAR) {
        count = message.getArgAsChar(0);
    }
    if (count >= 0) return Result<int>::ok(count);
    return Result<int>::fail(invalidArgument(message, 0, "expected a nonnegative integral layer count"));
}

} // namespace


bool CommandParser::startsWith(const std::string& value, const std::string& prefix) const {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool CommandParser::isPrefixedCommand(const std::string& value, const std::string& prefix) const {
    return value == prefix || startsWith(value, prefix + "/");
}

Result<std::string> CommandParser::normalizePrefixedPath(const ofxOscMessage& message, const std::string& prefix) const {
    const std::string address = message.getAddress();
    if (!isPrefixedCommand(address, prefix)) {
        return Result<std::string>::fail(ParseError(
            ParseErrorCode::UNKNOWN_COMMAND,
            address,
            "invalid targeted command for prefix " + prefix + ": " + address
        ));
    }
    if (address == prefix) {
        return Result<std::string>::ok(prefix);
    }
    return Result<std::string>::ok(prefix + address.substr(prefix.size()));
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
        std::string name = message.getArgAsString(index);
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

    if (isInt32Value(message, index)) {
        const auto value = message.getArgAsInt(index);
        if (value >= static_cast<int>(Layout::STACK) && value <= static_cast<int>(Layout::ROW)) {
            return Result<Layout>::ok(static_cast<Layout>(value));
        }
    }
    return Result<Layout>::fail(invalidArgument(message, index, "expected a layout name or enum 1–4"));
}

Result<TargetSelector> CommandParser::parseTarget(const ofxOscMessage& message, int index, ResourceDomain domain) const {
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
        const std::string value = message.getArgAsString(index);
        if (value == "*" || value == "x" || value == "a") {
            target.kind = TargetKind::ALL;
            return Result<TargetSelector>::ok(target);
        }

        if (domain == ResourceDomain::LAYER) {
            std::int32_t layerIndex = 0;
            const auto* end = value.data() + value.size();
            const auto parsed = std::from_chars(value.data(), end, layerIndex);
            if (value.empty() || value.front() < '0' || value.front() > '9' ||
                parsed.ec != std::errc{} || parsed.ptr != end) {
                return Result<TargetSelector>::fail(ParseError(
                    ParseErrorCode::INVALID_VALUE, message.getAddress(),
                    "expected a nonnegative decimal layer index or wildcard", index
                ));
            }
            target.kind = TargetKind::INDEX;
            target.index = layerIndex;
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

    if (domain == ResourceDomain::LAYER) {
        if (type != OFXOSC_TYPE_INT32) {
            return Result<TargetSelector>::fail(ParseError(
                ParseErrorCode::TYPE_MISMATCH, message.getAddress(),
                "expected an OSC int32 or string layer selector", index
            ));
        }
        target.index = message.getArgAsInt32(index);
        if (target.index < 0) {
            return Result<TargetSelector>::fail(ParseError(
                ParseErrorCode::INVALID_VALUE, message.getAddress(),
                "expected a nonnegative layer index", index
            ));
        }
        target.kind = TargetKind::INDEX;
        return Result<TargetSelector>::ok(target);
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

    const auto count = message.getNumArgs();
    if (count > 2) {
        return Result<Command>::fail(invalidArgument(message, 2, "unexpected layer argument"));
    }
    const bool layoutOnly = count == 1 && message.getArgType(0) == OFXOSC_TYPE_STRING &&
        LayoutMap.count(message.getArgAsString(0));
    if (count > 0 && !layoutOnly) {
        auto layerCount = readLayerCount(message);
        if (!layerCount.isOk()) {
            return Result<Command>::fail(layerCount.error());
        }
        command.layers.hasLayerCount = true;
        command.layers.layerCount = layerCount.value();
    }

    if (count == 2 || layoutOnly) {
        auto layout = parseLayoutArg(message, layoutOnly ? 0 : 1);
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
    if (message.getNumArgs() > 1) {
        return Result<Command>::fail(invalidArgument(message, 1, "unexpected layout argument"));
    }
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
    const auto address = message.getAddress();
    const int count = message.getNumArgs();
    const auto invalid = [&](int index, const char* detail) {
        return Result<Command>::fail(invalidArgument(message, index, detail));
    };
    const auto missing = [&](int index, const char* detail) {
        return Result<Command>::fail({ParseErrorCode::MISSING_ARGUMENT, address, detail, index});
    };

    Command command;
    command.type = CommandType::INPUT;
    command.address = address;
    command.raw = message;
    command.input.commandPath = address;

    if (address == "/input/list") {
        if (count != 0) return invalid(0, "input list takes no arguments");
        command.input.action = InputAction::LIST;
        return Result<Command>::ok(command);
    }
    if (address != "/input" && address != "/input/data" && address != "/input/set") {
        return Result<Command>::fail({ParseErrorCode::UNKNOWN_COMMAND, address, "unknown input command"});
    }

    if (count == 0) return missing(0, "missing input target");
    const auto targetType = message.getArgType(0);
    const bool stringTarget = targetType == OFXOSC_TYPE_STRING;
    if (stringTarget ? message.getArgAsString(0).empty() :
            ((targetType != OFXOSC_TYPE_INT32 && targetType != OFXOSC_TYPE_INT64) || !isInt32Value(message, 0))) {
        return invalid(0, "expected a nonempty input name or an int32-range integer ID");
    }

    if (address == "/input") {
        if (count > 4) return invalid(4, "unexpected input setup argument");
        if (count < 4 && stringTarget && message.getArgAsString(0) == "*") {
            return missing(count, "wildcard setup requires device, channels and buffer");
        }
        if (count == 1) command.raw.addStringArg("");
        else if (message.getArgType(1) != OFXOSC_TYPE_STRING) return invalid(1, "expected an input device string");
        if (count < 3) command.raw.addIntArg(count == 1 ? 0 : 1);
        else if (!isInt32Value(message, 2)) return invalid(2, "expected an integral input channel count");
        if (count < 4) command.raw.addIntArg(256);
        else if (!isInt32Value(message, 3) || message.getArgAsInt(3) <= 0) {
            return invalid(3, "expected a positive integral input buffer size");
        }
    } else if (address == "/input/set") {
        if (count < 3) return missing(count, "expected an input property and value");
        if (count > 3) return invalid(3, "unexpected input property argument");
        if (message.getArgType(1) != OFXOSC_TYPE_STRING) return invalid(1, "expected an input property name");
        const auto property = message.getArgAsString(1);
        if (property != "maxAmp" && property != "maxLoud") return invalid(1, "unknown input property");
        if (message.getArgType(2) != OFXOSC_TYPE_FLOAT || !std::isfinite(message.getArgAsFloat(2))) {
            return invalid(2, "expected a finite OSC float");
        }
    } else {
        if (count < 2) return missing(1, "missing input amplitude");
        if (!isSampleValue(message, 1)) return invalid(1, "expected finite input amplitude");
        if (count < 3) command.raw.addFloatArg(0.f);
        else if (!isSampleValue(message, 2)) return invalid(2, "expected finite input loudness");
        if (count < 4) command.raw.addIntArg(0);
        else if (!isOscBool(message.getArgType(3)) && !isInt32Value(message, 3)) {
            return invalid(3, "expected an integral or boolean onset");
        }
        for (int index = 4; index < count; ++index) {
            if (!isSampleValue(message, index)) return invalid(index, "expected a finite MFCC coefficient");
        }
        command.input.action = InputAction::DATA;
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

Result<Command> CommandParser::parseTargeted(const ofxOscMessage& message, ResourceDomain domain, const std::string& prefix) const {
    auto target = parseTarget(message, 0, domain);
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
    const std::string address = message.getAddress();

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
    if (isPrefixedCommand(address, "/midi")) {
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
    if (isPrefixedCommand(address, "/mat")) {
        return parseTargeted(message, ResourceDomain::LAYER, "/mat");
    }

    return Result<Command>::fail(ParseError(
        ParseErrorCode::UNKNOWN_COMMAND,
        address,
        "unknown OSC command: " + address
    ));
}

} // namespace osc
