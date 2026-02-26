#pragma once

#include "ofxOsc.h"

namespace osc {

enum class ParseErrorCode {
    NONE = 0,
    UNKNOWN_COMMAND,
    MISSING_ARGUMENT,
    TYPE_MISMATCH,
    INVALID_VALUE,
};

struct ParseError {
    ParseErrorCode code;
    string address;
    string message;
    int argIndex;
    ofxOscArgType expectedType;
    ofxOscArgType actualType;

    ParseError()
        : code(ParseErrorCode::NONE)
        , address("")
        , message("")
        , argIndex(-1)
        , expectedType(OFXOSC_TYPE_NONE)
        , actualType(OFXOSC_TYPE_NONE) {}

    ParseError(ParseErrorCode code, const string& address, const string& message, int argIndex = -1)
        : code(code)
        , address(address)
        , message(message)
        , argIndex(argIndex)
        , expectedType(OFXOSC_TYPE_NONE)
        , actualType(OFXOSC_TYPE_NONE) {}
};

} // namespace osc

