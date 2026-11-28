#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

struct GLFWwindow;

namespace webgpu {

class M1Preview {
public:
    struct Pixel {
        uint32_t x;
        uint32_t y;
        std::array<uint8_t, 4> rgba;
    };

    M1Preview(GLFWwindow* outgoing, const std::string& fragment);
    ~M1Preview();
    void draw();
    bool visible() const;

    std::array<Pixel, 4> pixels{};
    std::string backend;
    std::string adapter;
    uint64_t frames = 0;
    uint32_t width = 0;
    uint32_t height = 0;

private:
    struct State;
    std::unique_ptr<State> state;
};

} // namespace webgpu
