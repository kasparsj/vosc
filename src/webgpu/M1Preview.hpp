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

    explicit M1Preview(GLFWwindow* outgoing);
    M1Preview(GLFWwindow* outgoing, const std::string& fragment);
    bool prepare(const std::string& fragment);
    bool pollPreparation();
    void cancelPreparation();
    void presentPrepared(bool rejectFrame = false);
    ~M1Preview();
    void draw();
    bool visible() const;
    bool shouldClose() const;
    bool hasDevice() const;
    bool deviceLost() const;

    std::array<Pixel, 4> pixels{};
    std::string backend;
    std::string adapter;
    uint64_t frames = 0;
    uint64_t contextId = 0;
    uint32_t width = 0;
    uint32_t height = 0;

private:
    struct State;
    std::unique_ptr<State> state;
};

} // namespace webgpu
