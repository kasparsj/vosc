#pragma once

#include "Command.hpp"
#include "ArgReader.hpp"
#include "Result.hpp"

namespace osc {

class CommandParser {
public:
    Result<Command> parse(const ofxOscMessage& message) const;

private:
    Result<Command> parseLayersLike(const ofxOscMessage& message, CommandType type) const;
    Result<Command> parseLayers(const ofxOscMessage& message) const;
    Result<Command> parseLayersReset(const ofxOscMessage& message) const;
    Result<Command> parseLayersLayout(const ofxOscMessage& message) const;
    Result<Command> parseInput(const ofxOscMessage& message) const;
    Result<Command> parseCamera(const ofxOscMessage& message) const;
    Result<Command> parseMidi(const ofxOscMessage& message) const;
    Result<Command> parseShading(const ofxOscMessage& message) const;
    Result<Command> parseShadingPasses(const ofxOscMessage& message) const;
    Result<Command> parseTargeted(const ofxOscMessage& message, ResourceDomain domain, const string& prefix) const;

    Result<Layout> parseLayoutArg(const ofxOscMessage& message, int index) const;
    Result<TargetSelector> parseTarget(const ofxOscMessage& message, int index) const;
    Result<string> normalizePrefixedPath(const ofxOscMessage& message, const string& prefix) const;
    bool isPrefixedCommand(const string& value, const string& prefix) const;

    bool startsWith(const string& value, const string& prefix) const;
};

} // namespace osc
