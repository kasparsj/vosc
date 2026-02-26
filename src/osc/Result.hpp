#pragma once

#include "ParseError.hpp"

namespace osc {

template<typename T>
class Result {
public:
    static Result ok(const T& value) {
        return Result(true, value, ParseError());
    }

    static Result fail(const ParseError& error) {
        return Result(false, T(), error);
    }

    bool isOk() const {
        return _ok;
    }

    const T& value() const {
        return _value;
    }

    const ParseError& error() const {
        return _error;
    }

private:
    Result(bool ok, const T& value, const ParseError& error)
        : _ok(ok), _value(value), _error(error) {}

    bool _ok;
    T _value;
    ParseError _error;
};

} // namespace osc

