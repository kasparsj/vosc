#pragma once

#include "ofxOsc.h"
#include "Result.hpp"

namespace osc {

class ArgReader {
public:
    explicit ArgReader(const ofxOscMessage& message)
        : _message(message) {}

    int size() const {
        return _message.getNumArgs();
    }

    const ofxOscMessage& raw() const {
        return _message;
    }

    Result<string> requireString(int index) const;
    Result<int> requireInt(int index) const;
    Result<float> requireFloat(int index) const;
    Result<bool> requireBool(int index) const;

    Result<string> optionalString(int index, const string& fallback) const;
    Result<int> optionalInt(int index, int fallback) const;
    Result<float> optionalFloat(int index, float fallback) const;
    Result<bool> optionalBool(int index, bool fallback) const;

    bool has(int index) const {
        return index >= 0 && index < size();
    }

private:
    const ofxOscMessage& _message;

    ParseError missingArg(int index) const;
    ParseError wrongType(int index, ofxOscArgType expected) const;
};

} // namespace osc

