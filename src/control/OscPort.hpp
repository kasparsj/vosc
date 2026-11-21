#pragma once

#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

namespace control {

inline unsigned int parseOscPort(const char* value, unsigned int defaultPort = 33333) {
    if (value == nullptr) return defaultPort;
    std::string text(value);
    if (text.empty()) throw std::invalid_argument("VOSC_OSC_PORT must not be empty");
    unsigned int port = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9')
            throw std::invalid_argument("VOSC_OSC_PORT must be decimal digits in range 0..65535");
        const unsigned digit = static_cast<unsigned>(ch - '0');
        if (port > (65535u - digit) / 10u)
            throw std::invalid_argument("VOSC_OSC_PORT must be in range 0..65535");
        port = port * 10u + digit;
    }
    return port;
}

inline unsigned int oscPortFromEnvironment() {
    return parseOscPort(std::getenv("VOSC_OSC_PORT"));
}

} // namespace control
