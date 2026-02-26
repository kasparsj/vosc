#pragma once

#include "ofMain.h"

typedef enum _ofxOscArgType {
    OFXOSC_TYPE_INT32 = 'i',
    OFXOSC_TYPE_INT64 = 'h',
    OFXOSC_TYPE_FLOAT = 'f',
    OFXOSC_TYPE_DOUBLE = 'd',
    OFXOSC_TYPE_STRING = 's',
    OFXOSC_TYPE_SYMBOL = 'S',
    OFXOSC_TYPE_CHAR = 'c',
    OFXOSC_TYPE_MIDI_MESSAGE = 'm',
    OFXOSC_TYPE_TRUE = 'T',
    OFXOSC_TYPE_FALSE = 'F',
    OFXOSC_TYPE_NONE = 'N',
    OFXOSC_TYPE_TRIGGER = 'I',
    OFXOSC_TYPE_TIMETAG = 't',
    OFXOSC_TYPE_BLOB = 'b',
    OFXOSC_TYPE_RGBA_COLOR = 'r',
    OFXOSC_TYPE_INDEXOUTOFBOUNDS = 0
} ofxOscArgType;

class ofxOscMessage {
public:
    ofxOscMessage()
        : _address("")
        , _remoteHost("")
        , _remotePort(0) {}

    void clear() {
        _address.clear();
        _remoteHost.clear();
        _remotePort = 0;
        _args.clear();
    }

    void setAddress(const string& address) {
        _address = address;
    }

    string getAddress() const {
        return _address;
    }

    string getRemoteIp() const {
        return _remoteHost;
    }

    string getRemoteHost() const {
        return _remoteHost;
    }

    int getRemotePort() const {
        return _remotePort;
    }

    void setRemoteEndpoint(const string& host, int port) {
        _remoteHost = host;
        _remotePort = port;
    }

    std::size_t getNumArgs() const {
        return _args.size();
    }

    ofxOscArgType getArgType(std::size_t index) const {
        if (index >= _args.size()) {
            return OFXOSC_TYPE_INDEXOUTOFBOUNDS;
        }
        return _args[index].type;
    }

    string getArgTypeName(std::size_t index) const {
        if (index >= _args.size()) {
            return "INDEX OUT OF BOUNDS";
        }
        return string(1, static_cast<char>(_args[index].type));
    }

    string getTypeString() const {
        string out;
        for (std::size_t i = 0; i < _args.size(); ++i) {
            out.push_back(static_cast<char>(_args[i].type));
        }
        return out;
    }

    std::int32_t getArgAsInt(std::size_t index) const {
        return getArgAsInt32(index);
    }

    std::int32_t getArgAsInt32(std::size_t index) const {
        if (index >= _args.size()) {
            return 0;
        }
        const Arg& arg = _args[index];
        switch (arg.type) {
            case OFXOSC_TYPE_INT32:
            case OFXOSC_TYPE_INT64:
                return static_cast<std::int32_t>(arg.int64Value);
            case OFXOSC_TYPE_FLOAT:
            case OFXOSC_TYPE_DOUBLE:
                return static_cast<std::int32_t>(arg.doubleValue);
            case OFXOSC_TYPE_CHAR:
                return static_cast<std::int32_t>(arg.charValue);
            case OFXOSC_TYPE_TRUE:
            case OFXOSC_TYPE_FALSE:
                return arg.boolValue ? 1 : 0;
            case OFXOSC_TYPE_STRING:
            case OFXOSC_TYPE_SYMBOL:
                return ofToInt(arg.stringValue);
            default:
                return 0;
        }
    }

    std::int64_t getArgAsInt64(std::size_t index) const {
        if (index >= _args.size()) {
            return 0;
        }
        const Arg& arg = _args[index];
        switch (arg.type) {
            case OFXOSC_TYPE_INT32:
            case OFXOSC_TYPE_INT64:
                return arg.int64Value;
            case OFXOSC_TYPE_FLOAT:
            case OFXOSC_TYPE_DOUBLE:
                return static_cast<std::int64_t>(arg.doubleValue);
            case OFXOSC_TYPE_CHAR:
                return static_cast<std::int64_t>(arg.charValue);
            case OFXOSC_TYPE_TRUE:
            case OFXOSC_TYPE_FALSE:
                return arg.boolValue ? 1 : 0;
            case OFXOSC_TYPE_STRING:
            case OFXOSC_TYPE_SYMBOL:
                return static_cast<std::int64_t>(ofToInt(arg.stringValue));
            default:
                return 0;
        }
    }

    float getArgAsFloat(std::size_t index) const {
        if (index >= _args.size()) {
            return 0.f;
        }
        const Arg& arg = _args[index];
        switch (arg.type) {
            case OFXOSC_TYPE_INT32:
            case OFXOSC_TYPE_INT64:
                return static_cast<float>(arg.int64Value);
            case OFXOSC_TYPE_FLOAT:
            case OFXOSC_TYPE_DOUBLE:
                return static_cast<float>(arg.doubleValue);
            case OFXOSC_TYPE_CHAR:
                return static_cast<float>(arg.charValue);
            case OFXOSC_TYPE_TRUE:
            case OFXOSC_TYPE_FALSE:
                return arg.boolValue ? 1.f : 0.f;
            case OFXOSC_TYPE_STRING:
            case OFXOSC_TYPE_SYMBOL:
                return ofToFloat(arg.stringValue);
            default:
                return 0.f;
        }
    }

    double getArgAsDouble(std::size_t index) const {
        return static_cast<double>(getArgAsFloat(index));
    }

    string getArgAsString(std::size_t index) const {
        if (index >= _args.size()) {
            return "";
        }
        const Arg& arg = _args[index];
        switch (arg.type) {
            case OFXOSC_TYPE_STRING:
            case OFXOSC_TYPE_SYMBOL:
                return arg.stringValue;
            case OFXOSC_TYPE_INT32:
            case OFXOSC_TYPE_INT64:
                return ofToString(arg.int64Value);
            case OFXOSC_TYPE_FLOAT:
            case OFXOSC_TYPE_DOUBLE:
                return ofToString(arg.doubleValue);
            case OFXOSC_TYPE_TRUE:
            case OFXOSC_TYPE_FALSE:
                return arg.boolValue ? "1" : "0";
            case OFXOSC_TYPE_CHAR:
                return string(1, arg.charValue);
            default:
                return "";
        }
    }

    string getArgAsSymbol(std::size_t index) const {
        return getArgAsString(index);
    }

    char getArgAsChar(std::size_t index) const {
        if (index >= _args.size()) {
            return 0;
        }
        const Arg& arg = _args[index];
        if (arg.type == OFXOSC_TYPE_CHAR) {
            return arg.charValue;
        }
        const string value = getArgAsString(index);
        return value.empty() ? 0 : value[0];
    }

    std::uint32_t getArgAsMidiMessage(std::size_t index) const {
        if (index >= _args.size()) {
            return 0;
        }
        return static_cast<std::uint32_t>(getArgAsInt(index));
    }

    bool getArgAsBool(std::size_t index) const {
        if (index >= _args.size()) {
            return false;
        }
        const Arg& arg = _args[index];
        if (arg.type == OFXOSC_TYPE_TRUE) {
            return true;
        }
        if (arg.type == OFXOSC_TYPE_FALSE) {
            return false;
        }
        return getArgAsInt(index) != 0;
    }

    bool getArgAsNone(std::size_t index) const {
        return getArgType(index) == OFXOSC_TYPE_NONE;
    }

    bool getArgAsTrigger(std::size_t index) const {
        return getArgType(index) == OFXOSC_TYPE_TRIGGER;
    }

    bool getArgAsImpulse(std::size_t index) const {
        return getArgAsTrigger(index);
    }

    bool getArgAsInfinitum(std::size_t index) const {
        return getArgAsTrigger(index);
    }

    std::uint64_t getArgAsTimetag(std::size_t index) const {
        if (index >= _args.size()) {
            return 0;
        }
        return static_cast<std::uint64_t>(_args[index].int64Value);
    }

    ofBuffer getArgAsBlob(std::size_t index) const {
        if (index >= _args.size()) {
            return ofBuffer();
        }
        return _args[index].blobValue;
    }

    std::uint32_t getArgAsRgbaColor(std::size_t index) const {
        if (index >= _args.size()) {
            return 0;
        }
        const Arg& arg = _args[index];
        switch (arg.type) {
            case OFXOSC_TYPE_RGBA_COLOR:
                return arg.rgbaValue;
            case OFXOSC_TYPE_INT32:
            case OFXOSC_TYPE_INT64:
                return static_cast<std::uint32_t>(arg.int64Value);
            case OFXOSC_TYPE_FLOAT:
            case OFXOSC_TYPE_DOUBLE:
                return static_cast<std::uint32_t>(arg.doubleValue);
            default:
                return 0;
        }
    }

    void addIntArg(std::int32_t argument) {
        addInt32Arg(argument);
    }

    void addInt32Arg(std::int32_t argument) {
        Arg arg;
        arg.type = OFXOSC_TYPE_INT32;
        arg.int64Value = argument;
        _args.push_back(arg);
    }

    void addInt64Arg(std::int64_t argument) {
        Arg arg;
        arg.type = OFXOSC_TYPE_INT64;
        arg.int64Value = argument;
        _args.push_back(arg);
    }

    void addFloatArg(float argument) {
        Arg arg;
        arg.type = OFXOSC_TYPE_FLOAT;
        arg.doubleValue = argument;
        _args.push_back(arg);
    }

    void addDoubleArg(double argument) {
        Arg arg;
        arg.type = OFXOSC_TYPE_DOUBLE;
        arg.doubleValue = argument;
        _args.push_back(arg);
    }

    void addStringArg(const string& argument) {
        Arg arg;
        arg.type = OFXOSC_TYPE_STRING;
        arg.stringValue = argument;
        _args.push_back(arg);
    }

    void addSymbolArg(const string& argument) {
        Arg arg;
        arg.type = OFXOSC_TYPE_SYMBOL;
        arg.stringValue = argument;
        _args.push_back(arg);
    }

    void addCharArg(char argument) {
        Arg arg;
        arg.type = OFXOSC_TYPE_CHAR;
        arg.charValue = argument;
        _args.push_back(arg);
    }

    void addMidiMessageArg(std::uint32_t argument) {
        Arg arg;
        arg.type = OFXOSC_TYPE_MIDI_MESSAGE;
        arg.int64Value = static_cast<std::int64_t>(argument);
        _args.push_back(arg);
    }

    void addBoolArg(bool argument) {
        Arg arg;
        arg.type = argument ? OFXOSC_TYPE_TRUE : OFXOSC_TYPE_FALSE;
        arg.boolValue = argument;
        _args.push_back(arg);
    }

    void addNoneArg() {
        Arg arg;
        arg.type = OFXOSC_TYPE_NONE;
        _args.push_back(arg);
    }

    void addTriggerArg() {
        Arg arg;
        arg.type = OFXOSC_TYPE_TRIGGER;
        _args.push_back(arg);
    }

    void addImpulseArg() {
        addTriggerArg();
    }

    void addInfinitumArg() {
        addTriggerArg();
    }

    void addTimetagArg(std::uint64_t argument) {
        Arg arg;
        arg.type = OFXOSC_TYPE_TIMETAG;
        arg.int64Value = static_cast<std::int64_t>(argument);
        _args.push_back(arg);
    }

    void addBlobArg(const ofBuffer& argument) {
        Arg arg;
        arg.type = OFXOSC_TYPE_BLOB;
        arg.blobValue = argument;
        _args.push_back(arg);
    }

    void addRgbaColorArg(std::uint32_t argument) {
        Arg arg;
        arg.type = OFXOSC_TYPE_RGBA_COLOR;
        arg.rgbaValue = argument;
        _args.push_back(arg);
    }

    friend std::ostream& operator<<(std::ostream& os, const ofxOscMessage& message) {
        os << message._address << " " << message.getTypeString();
        return os;
    }

private:
    struct Arg {
        ofxOscArgType type;
        std::int64_t int64Value;
        double doubleValue;
        string stringValue;
        char charValue;
        bool boolValue;
        ofBuffer blobValue;
        std::uint32_t rgbaValue;

        Arg()
            : type(OFXOSC_TYPE_NONE)
            , int64Value(0)
            , doubleValue(0.0)
            , stringValue("")
            , charValue(0)
            , boolValue(false)
            , blobValue()
            , rgbaValue(0) {}
    };

    string _address;
    vector<Arg> _args;
    string _remoteHost;
    int _remotePort;
};
