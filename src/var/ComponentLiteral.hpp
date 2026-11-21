#pragma once

#include <cmath>
#include <locale>
#include <optional>
#include <sstream>
#include <string>

// Mixed vector bindings store numeric components as decimal text. Parse via
// double so float32 subnormals survive until the caller applies its bounds.
inline std::optional<double> parseComponentLiteral(const std::string& text) {
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    double value;
    if (!(input >> value) || !input.eof() || !std::isfinite(value)) return std::nullopt;
    return value;
}
