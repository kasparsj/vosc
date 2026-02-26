#include "ArgReader.hpp"

namespace osc {

ParseError ArgReader::missingArg(int index) const {
    return ParseError(
        ParseErrorCode::MISSING_ARGUMENT,
        _message.getAddress(),
        "missing required argument at index " + ofToString(index),
        index
    );
}

ParseError ArgReader::wrongType(int index, ofxOscArgType expected) const {
    ParseError err(
        ParseErrorCode::TYPE_MISMATCH,
        _message.getAddress(),
        "invalid argument type at index " + ofToString(index),
        index
    );
    err.expectedType = expected;
    err.actualType = has(index) ? _message.getArgType(index) : OFXOSC_TYPE_NONE;
    return err;
}

Result<string> ArgReader::requireString(int index) const {
    if (!has(index)) {
        return Result<string>::fail(missingArg(index));
    }
    if (_message.getArgType(index) != OFXOSC_TYPE_STRING) {
        return Result<string>::fail(wrongType(index, OFXOSC_TYPE_STRING));
    }
    return Result<string>::ok(_message.getArgAsString(index));
}

Result<int> ArgReader::requireInt(int index) const {
    if (!has(index)) {
        return Result<int>::fail(missingArg(index));
    }
    const auto type = _message.getArgType(index);
    switch (type) {
        case OFXOSC_TYPE_INT32:
        case OFXOSC_TYPE_INT64:
        case OFXOSC_TYPE_CHAR:
            return Result<int>::ok(_message.getArgAsInt(index));
        case OFXOSC_TYPE_FLOAT:
        case OFXOSC_TYPE_DOUBLE:
            return Result<int>::ok(static_cast<int>(_message.getArgAsFloat(index)));
        case OFXOSC_TYPE_STRING: {
            const string str = ofTrim(_message.getArgAsString(index));
            if (str.empty()) {
                return Result<int>::fail(wrongType(index, OFXOSC_TYPE_INT32));
            }
            return Result<int>::ok(ofToInt(str));
        }
        default:
            return Result<int>::fail(wrongType(index, OFXOSC_TYPE_INT32));
    }
}

Result<float> ArgReader::requireFloat(int index) const {
    if (!has(index)) {
        return Result<float>::fail(missingArg(index));
    }
    const auto type = _message.getArgType(index);
    switch (type) {
        case OFXOSC_TYPE_INT32:
        case OFXOSC_TYPE_INT64:
        case OFXOSC_TYPE_CHAR:
            return Result<float>::ok(static_cast<float>(_message.getArgAsInt(index)));
        case OFXOSC_TYPE_FLOAT:
        case OFXOSC_TYPE_DOUBLE:
            return Result<float>::ok(_message.getArgAsFloat(index));
        case OFXOSC_TYPE_STRING: {
            const string str = ofTrim(_message.getArgAsString(index));
            if (str.empty()) {
                return Result<float>::fail(wrongType(index, OFXOSC_TYPE_FLOAT));
            }
            return Result<float>::ok(ofToFloat(str));
        }
        default:
            return Result<float>::fail(wrongType(index, OFXOSC_TYPE_FLOAT));
    }
}

Result<bool> ArgReader::requireBool(int index) const {
    if (!has(index)) {
        return Result<bool>::fail(missingArg(index));
    }
    const auto type = _message.getArgType(index);
    switch (type) {
        case OFXOSC_TYPE_TRUE:
            return Result<bool>::ok(true);
        case OFXOSC_TYPE_FALSE:
            return Result<bool>::ok(false);
        case OFXOSC_TYPE_INT32:
        case OFXOSC_TYPE_INT64:
        case OFXOSC_TYPE_CHAR:
            return Result<bool>::ok(_message.getArgAsInt(index) != 0);
        case OFXOSC_TYPE_FLOAT:
        case OFXOSC_TYPE_DOUBLE:
            return Result<bool>::ok(_message.getArgAsFloat(index) != 0.f);
        case OFXOSC_TYPE_STRING: {
            const string val = ofToLower(ofTrim(_message.getArgAsString(index)));
            if (val == "1" || val == "true" || val == "yes" || val == "on") {
                return Result<bool>::ok(true);
            }
            if (val == "0" || val == "false" || val == "no" || val == "off") {
                return Result<bool>::ok(false);
            }
            return Result<bool>::fail(wrongType(index, OFXOSC_TYPE_TRUE));
        }
        default:
            return Result<bool>::fail(wrongType(index, OFXOSC_TYPE_TRUE));
    }
}

Result<string> ArgReader::optionalString(int index, const string& fallback) const {
    if (!has(index)) {
        return Result<string>::ok(fallback);
    }
    return requireString(index);
}

Result<int> ArgReader::optionalInt(int index, int fallback) const {
    if (!has(index)) {
        return Result<int>::ok(fallback);
    }
    return requireInt(index);
}

Result<float> ArgReader::optionalFloat(int index, float fallback) const {
    if (!has(index)) {
        return Result<float>::ok(fallback);
    }
    return requireFloat(index);
}

Result<bool> ArgReader::optionalBool(int index, bool fallback) const {
    if (!has(index)) {
        return Result<bool>::ok(fallback);
    }
    return requireBool(index);
}

} // namespace osc

