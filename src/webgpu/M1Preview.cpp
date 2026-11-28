#ifdef VOSC_WEBGPU_M1

#include "M1Preview.hpp"
#include <webgpu/webgpu_cpp.h>

#define GLFW_INCLUDE_NONE
#ifdef __APPLE__
#define GLFW_EXPOSE_NATIVE_COCOA
#else
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#if defined(__linux__)
#undef Status
#undef Success
#endif

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string_view>
#include <utility>

#ifdef __APPLE__
void* voscM1MetalLayer(void* cocoaWindow);
#endif

namespace webgpu {
namespace {

constexpr auto waitOnly = wgpu::CallbackMode::WaitAnyOnly;
using Clock = std::chrono::steady_clock;
constexpr auto frameTimeout = std::chrono::seconds(1);
constexpr uint64_t rendererUniformBytes = 16;
constexpr uint32_t bytesPerRow = 256;
uint64_t nextContextId = 0;

constexpr auto vertexSource = R"(
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex fn vs_main(@builtin(vertex_index) vertex: u32) -> VertexOutput {
    let uv = vec2f(f32((vertex << 1u) & 2u), f32(vertex & 2u));
    return VertexOutput(vec4f(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y, 0.0, 1.0), uv);
}
)";

std::string message(wgpu::StringView value) {
    return value.data ? std::string(std::string_view(value)) : std::string();
}

void wait(wgpu::Instance instance, wgpu::Future future, Clock::time_point deadline, const char* operation) {
    const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - Clock::now()).count();
    if (instance.WaitAny(future, std::max<int64_t>(0, remaining)) != wgpu::WaitStatus::Success)
        throw std::runtime_error(std::string(operation) + " did not complete");
}

template <typename Handle>
struct Creation {
    Handle handle;
    std::string detail;
};

using Window = std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)>;

Window createWindow(GLFWwindow* outgoing) {
    int x, y, width, height;
    glfwGetWindowPos(outgoing, &x, &y);
    glfwGetWindowSize(outgoing, &width, &height);
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    Window window(glfwCreateWindow(width, height, "VOSC Draft Preview (WebGPU)", nullptr, nullptr),
                  glfwDestroyWindow);
    glfwDefaultWindowHints();
    if (!window) throw std::runtime_error("Could not create the WebGPU preview window");
    glfwSetWindowPos(window.get(), x, y);
    return window;
}

wgpu::Surface createSurface(wgpu::Instance instance, GLFWwindow* window) {
    wgpu::SurfaceDescriptor descriptor;
#ifdef __APPLE__
    wgpu::SurfaceSourceMetalLayer source;
    source.layer = voscM1MetalLayer(glfwGetCocoaWindow(window));
    if (!source.layer) throw std::runtime_error("Could not create the Cocoa Metal layer");
#elif defined(__linux__)
    wgpu::SurfaceSourceXlibWindow source;
    source.display = glfwGetX11Display();
    source.window = glfwGetX11Window(window);
    if (!source.display || !source.window) throw std::runtime_error("The Linux WebGPU preview requires X11");
#endif
    descriptor.nextInChain = &source;
    return instance.CreateSurface(&descriptor);
}

wgpu::ShaderModule shaderModule(wgpu::Device device, std::string_view code) {
    wgpu::ShaderSourceWGSL source;
    source.code = code;
    wgpu::ShaderModuleDescriptor descriptor;
    descriptor.nextInChain = &source;
    return device.CreateShaderModule(&descriptor);
}

} // namespace

struct M1Preview::State {
    explicit State(GLFWwindow* outgoing) : window(createWindow(outgoing)) {}
    enum class Preparation { idle, adapter, device, pipeline, ready };
    Window window;
    Clock::time_point started = Clock::now();
    wgpu::Instance instance;
    wgpu::Surface surface;
    wgpu::BackendType nativeBackend;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
    wgpu::RenderPipeline pipeline;
    wgpu::Texture image;
    wgpu::Queue queue;
    wgpu::Buffer uniformBuffer;
    struct Bindings {
        wgpu::BindGroup group;
        wgpu::PipelineLayout layout;
        std::optional<float> fixedTime;
    };
    Bindings frameBindings;
    Bindings activeBindings;
    Bindings candidateBindings;
    std::vector<ControlLane> candidateControls;
    std::string entryPoint;
    std::optional<float> candidateTime;
    wgpu::ShaderModule vertex;
    Preparation phase = Preparation::idle;
    bool abandoned = false;
    std::string source;
    wgpu::Future future;
    std::shared_ptr<Creation<wgpu::Adapter>> selected;
    std::shared_ptr<Creation<wgpu::Device>> opened;
    std::shared_ptr<Creation<wgpu::RenderPipeline>> compiled;

    void configure(uint32_t& width, uint32_t& height);
    Bindings makeBindings(const std::vector<ControlLane>& controls);
    void compile();
    void render(wgpu::CommandEncoder encoder, const wgpu::RenderPipeline& program,
                const Bindings& bindings, const wgpu::Texture& target);
    void gpuDone(Clock::time_point deadline);
    void checkValidation(Clock::time_point deadline);
    void copyToSurface(const wgpu::Texture& target, const Clock::time_point* deadline);
};

M1Preview::M1Preview(GLFWwindow* outgoing) : state(std::make_unique<State>(outgoing)) {
    auto& gpu = *state;
    const auto timedWait = wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor instanceDescriptor;
    instanceDescriptor.requiredFeatureCount = 1;
    instanceDescriptor.requiredFeatures = &timedWait;
    gpu.instance = wgpu::CreateInstance(&instanceDescriptor);
    if (!gpu.instance) throw std::runtime_error("Could not create the Dawn instance");
    gpu.surface = createSurface(gpu.instance, gpu.window.get());
    if (!gpu.surface) throw std::runtime_error("Could not create the WebGPU preview surface");
#ifdef __APPLE__
    gpu.nativeBackend = wgpu::BackendType::Metal;
#else
    gpu.nativeBackend = wgpu::BackendType::Vulkan;
#endif
}

bool M1Preview::prepare(const std::string& fragment, std::string entryPoint,
                        const std::vector<ControlLane>& controls, std::optional<float> fixedTime) {
    auto& gpu = *state;
    if (gpu.phase != State::Preparation::idle) return false;
    gpu.source = fragment;
    gpu.entryPoint = std::move(entryPoint);
    gpu.candidateControls = controls;
    gpu.candidateTime = fixedTime;
    if (gpu.device) {
        gpu.compile();
    } else {
        wgpu::RequestAdapterOptions options;
        options.backendType = gpu.nativeBackend;
        options.compatibleSurface = gpu.surface;
        gpu.selected = std::make_shared<Creation<wgpu::Adapter>>();
        gpu.future = gpu.instance.RequestAdapter(&options, waitOnly,
            [result = gpu.selected](wgpu::RequestAdapterStatus status, wgpu::Adapter handle, wgpu::StringView detail) {
                if (status == wgpu::RequestAdapterStatus::Success) result->handle = std::move(handle);
                else result->detail = message(detail);
            });
        gpu.phase = State::Preparation::adapter;
    }
    return true;
}

bool M1Preview::pollPreparation() {
    auto& gpu = *state;
    using Phase = State::Preparation;
    if (gpu.phase == Phase::ready) return true;
    if (gpu.phase == Phase::idle || gpu.instance.WaitAny(gpu.future, 0) != wgpu::WaitStatus::Success)
        return false;
    const auto phase = std::exchange(gpu.phase, Phase::idle);
    if (std::exchange(gpu.abandoned, false)) {
        gpu.selected.reset();
        gpu.opened.reset();
        gpu.compiled.reset();
        gpu.candidateBindings = {};
        return false;
    }
    if (phase == Phase::adapter) {
        auto result = std::move(gpu.selected);
        if (!result->handle) throw std::runtime_error("Dawn adapter request failed: " + result->detail);
        gpu.adapter = std::move(result->handle);
        wgpu::AdapterInfo info;
        if (gpu.adapter.GetInfo(&info) != wgpu::Status::Success || info.backendType != gpu.nativeBackend)
            throw std::runtime_error("Dawn returned an unexpected backend");
        backend = info.backendType == wgpu::BackendType::Metal ? "Metal" : "Vulkan";
        adapter = message(info.device);
        const auto driver = message(info.description);
        if (!driver.empty()) adapter += (adapter.empty() ? "" : " / ") + driver;
        gpu.opened = std::make_shared<Creation<wgpu::Device>>();
        gpu.future = gpu.adapter.RequestDevice(nullptr, waitOnly,
            [result = gpu.opened](wgpu::RequestDeviceStatus status, wgpu::Device handle, wgpu::StringView detail) {
                if (status == wgpu::RequestDeviceStatus::Success) result->handle = std::move(handle);
                else result->detail = message(detail);
            });
        gpu.phase = Phase::device;
    } else if (phase == Phase::device) {
        auto result = std::move(gpu.opened);
        if (!result->handle) throw std::runtime_error("Dawn device request failed: " + result->detail);
        gpu.device = std::move(result->handle);
        try {
            gpu.configure(width, height);
            contextId = ++nextContextId;
        } catch (...) {
            gpu.device = nullptr;
            throw;
        }
        gpu.compile();
    } else {
        if (!gpu.compiled->handle) {
            auto result = std::move(gpu.compiled);
            gpu.candidateBindings = {};
            throw std::runtime_error("WGSL pipeline creation failed" +
                (result->detail.empty() ? std::string() : ": " + result->detail));
        }
        gpu.phase = Phase::ready;
    }
    return gpu.phase == Phase::ready;
}

void M1Preview::cancelPreparation() {
    auto& gpu = *state;
    gpu.source.clear();
    gpu.candidateControls.clear();
    if (gpu.phase == State::Preparation::ready) {
        gpu.compiled.reset();
        gpu.candidateBindings = {};
        gpu.phase = State::Preparation::idle;
    } else if (gpu.phase != State::Preparation::idle) {
        // Keep one pending Dawn operation until its callback can be drained.
        gpu.abandoned = true;
    }
}

void M1Preview::State::configure(uint32_t& width, uint32_t& height) {
    queue = device.GetQueue();
    wgpu::SurfaceCapabilities capabilities;
    if (surface.GetCapabilities(adapter, &capabilities) != wgpu::Status::Success ||
        !(capabilities.usages & wgpu::TextureUsage::CopyDst))
        throw std::runtime_error("WebGPU preview surface cannot receive the staged image");
    format = wgpu::TextureFormat::Undefined;
    for (size_t i = 0; i < capabilities.formatCount; ++i) {
        const auto supported = capabilities.formats[i];
        if (supported == wgpu::TextureFormat::BGRA8Unorm || supported == wgpu::TextureFormat::RGBA8Unorm) {
            format = supported;
            break;
        }
    }
    if (format == wgpu::TextureFormat::Undefined)
        throw std::runtime_error("WebGPU preview surface has no linear 8-bit RGBA format");

    int backingWidth, backingHeight;
    glfwGetFramebufferSize(window.get(), &backingWidth, &backingHeight);
    if (backingWidth < 2 || backingHeight < 2)
        throw std::runtime_error("WebGPU preview window has no usable framebuffer");
    width = static_cast<uint32_t>(backingWidth);
    height = static_cast<uint32_t>(backingHeight);

    wgpu::BufferDescriptor bufferDescriptor;
    bufferDescriptor.size = rendererUniformBytes;
    bufferDescriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
    uniformBuffer = device.CreateBuffer(&bufferDescriptor);

    frameBindings = makeBindings({});
    vertex = shaderModule(device, vertexSource);

    wgpu::SurfaceConfiguration configuration;
    configuration.device = device;
    configuration.format = format;
    configuration.width = width;
    configuration.height = height;
    configuration.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopyDst;
    configuration.presentMode = wgpu::PresentMode::Fifo;
    surface.Configure(&configuration);
}

M1Preview::State::Bindings M1Preview::State::makeBindings(const std::vector<ControlLane>& controls) {
    std::array<wgpu::BindGroupLayoutEntry, 2> entries{};
    std::array<wgpu::BindGroupEntry, 2> resources{};
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Fragment;
    entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[0].buffer.minBindingSize = rendererUniformBytes;
    resources[0].binding = 0;
    resources[0].buffer = uniformBuffer;
    resources[0].size = rendererUniformBytes;
    if (!controls.empty()) {
        static_assert(sizeof(ControlLane) == 16);
        wgpu::BufferDescriptor descriptor;
        descriptor.size = controls.size() * sizeof(ControlLane);
        descriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
        auto controlBuffer = device.CreateBuffer(&descriptor);
        queue.WriteBuffer(controlBuffer, 0, controls.data(), descriptor.size);
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[1].buffer.minBindingSize = descriptor.size;
        resources[1].binding = 1;
        resources[1].buffer = std::move(controlBuffer);
        resources[1].size = descriptor.size;
    }
    const size_t count = controls.empty() ? 1 : 2;
    wgpu::BindGroupLayoutDescriptor groupLayoutDescriptor;
    groupLayoutDescriptor.entryCount = count;
    groupLayoutDescriptor.entries = entries.data();
    const auto groupLayout = device.CreateBindGroupLayout(&groupLayoutDescriptor);
    wgpu::BindGroupDescriptor bindDescriptor;
    bindDescriptor.layout = groupLayout;
    bindDescriptor.entryCount = count;
    bindDescriptor.entries = resources.data();
    wgpu::PipelineLayoutDescriptor layoutDescriptor;
    layoutDescriptor.bindGroupLayoutCount = 1;
    layoutDescriptor.bindGroupLayouts = &groupLayout;
    return {device.CreateBindGroup(&bindDescriptor), device.CreatePipelineLayout(&layoutDescriptor), {}};
}

void M1Preview::State::compile() {
    candidateBindings = candidateControls.empty() ? frameBindings : makeBindings(candidateControls);
    candidateBindings.fixedTime = candidateTime;
    candidateControls.clear();
    wgpu::ColorTargetState target;
    target.format = format;
    wgpu::FragmentState fragment;
    fragment.module = shaderModule(device, source);
    fragment.entryPoint = std::string_view(entryPoint);
    fragment.targetCount = 1;
    fragment.targets = &target;
    wgpu::RenderPipelineDescriptor descriptor;
    descriptor.layout = candidateBindings.layout;
    descriptor.vertex.module = vertex;
    descriptor.vertex.entryPoint = "vs_main";
    descriptor.fragment = &fragment;
    compiled = std::make_shared<Creation<wgpu::RenderPipeline>>();
    future = device.CreateRenderPipelineAsync(&descriptor, waitOnly,
        [result = compiled](wgpu::CreatePipelineAsyncStatus status, wgpu::RenderPipeline handle, wgpu::StringView detail) {
            if (status == wgpu::CreatePipelineAsyncStatus::Success) result->handle = std::move(handle);
            else result->detail = message(detail);
        });
    source.clear();
    phase = Preparation::pipeline;
}

M1Preview::M1Preview(GLFWwindow* outgoing, const std::string& fragment) : M1Preview(outgoing) {
    const auto deadline = Clock::now() + frameTimeout;
    prepare(fragment);
    while (state->phase != State::Preparation::ready) {
        wait(state->instance, state->future, deadline, "Dawn preparation");
        pollPreparation();
    }
    presentPrepared();
}

void M1Preview::presentPrepared(bool rejectFrame) {
    auto& gpu = *state;
    if (gpu.phase != State::Preparation::ready)
        throw std::runtime_error("There is no prepared WebGPU pipeline");
    if (shouldClose()) throw std::runtime_error("WebGPU preview window closed");
    if (deviceLost()) throw std::runtime_error("WebGPU device was lost");
    const auto deadline = Clock::now() + frameTimeout;
    auto stage = [&](auto work) {
        gpu.device.PushErrorScope(wgpu::ErrorFilter::Validation);
        try {
            work();
        } catch (...) {
            gpu.checkValidation(deadline);
            throw;
        }
        gpu.checkValidation(deadline);
    };
    wgpu::Texture candidateImage;
    std::array<Pixel, 4> candidatePixels{};
    stage([&] {
        wgpu::TextureDescriptor imageDescriptor;
        imageDescriptor.format = gpu.format;
        imageDescriptor.size = {width, height, 1};
        imageDescriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
        candidateImage = gpu.device.CreateTexture(&imageDescriptor);
        // Read four pixels from the same GPU image that the surface will present.
        candidatePixels = {{{width / 4, height / 4, {}}, {3 * width / 4, height / 4, {}},
                   {width / 4, 3 * height / 4, {}}, {3 * width / 4, 3 * height / 4, {}}}};
        wgpu::BufferDescriptor readbackDescriptor;
        readbackDescriptor.size = bytesPerRow * candidatePixels.size();
        readbackDescriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
        auto readback = gpu.device.CreateBuffer(&readbackDescriptor);
        auto encoder = gpu.device.CreateCommandEncoder();
        gpu.render(encoder, gpu.compiled->handle, gpu.candidateBindings, candidateImage);
        const wgpu::Extent3D onePixel{1, 1, 1};
        for (size_t i = 0; i < candidatePixels.size(); ++i) {
            wgpu::TexelCopyTextureInfo from;
            from.texture = candidateImage;
            from.origin = {candidatePixels[i].x, candidatePixels[i].y, 0};
            wgpu::TexelCopyBufferInfo to;
            to.buffer = readback;
            to.layout.offset = i * bytesPerRow;
            to.layout.bytesPerRow = bytesPerRow;
            encoder.CopyTextureToBuffer(&from, &to, &onePixel);
        }
        auto commands = encoder.Finish();
        gpu.queue.Submit(1, &commands);
        auto mapped = std::make_shared<bool>(false);
        auto map = readback.MapAsync(wgpu::MapMode::Read, 0, readbackDescriptor.size, waitOnly,
            [mapped](wgpu::MapAsyncStatus status, wgpu::StringView) { *mapped = status == wgpu::MapAsyncStatus::Success; });
        wait(gpu.instance, map, deadline, "WebGPU readback");
        if (!*mapped) throw std::runtime_error("Could not map the staged WebGPU image");
        for (size_t i = 0; i < candidatePixels.size(); ++i) {
            auto& rgba = candidatePixels[i].rgba;
            if (readback.ReadMappedRange(i * bytesPerRow, rgba.data(), rgba.size()) != wgpu::Status::Success)
                throw std::runtime_error("Could not read the staged WebGPU pixel");
            if (gpu.format == wgpu::TextureFormat::BGRA8Unorm) std::swap(rgba[0], rgba[2]);
        }
        readback.Unmap();
    });
    if (rejectFrame) throw std::runtime_error("Injected WebGPU first-frame failure");
#ifdef __APPLE__
    // Cocoa needs a visible window for its first drawable. Keep the old
    // window visible until presentation succeeds.
    if (!frames) glfwShowWindow(gpu.window.get());
#endif
    try {
        gpu.copyToSurface(candidateImage, &deadline);
    } catch (...) {
        if (!frames) glfwHideWindow(gpu.window.get());
        throw;
    }
#ifndef __APPLE__
    if (!frames) glfwShowWindow(gpu.window.get());
#endif
    gpu.pipeline = std::move(gpu.compiled->handle);
    gpu.activeBindings = std::move(gpu.candidateBindings);
    gpu.compiled.reset();
    gpu.phase = State::Preparation::idle;
    gpu.image = std::move(candidateImage);
    pixels = candidatePixels;
    ++frames;
}

M1Preview::~M1Preview() = default;

void M1Preview::State::gpuDone(Clock::time_point deadline) {
    auto completed = std::make_shared<bool>(false);
    auto work = queue.OnSubmittedWorkDone(waitOnly,
        [completed](wgpu::QueueWorkDoneStatus status, wgpu::StringView) {
            *completed = status == wgpu::QueueWorkDoneStatus::Success;
        });
    wait(instance, work, deadline, "WebGPU surface copy");
    if (!*completed) throw std::runtime_error("WebGPU queue did not finish the surface copy");
}

void M1Preview::State::checkValidation(Clock::time_point deadline) {
    auto failure = std::make_shared<std::string>();
    auto scope = device.PopErrorScope(waitOnly,
        [failure](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type, wgpu::StringView detail) {
            if (status != wgpu::PopErrorScopeStatus::Success || type != wgpu::ErrorType::NoError) {
                *failure = message(detail);
                if (failure->empty()) *failure = "WebGPU could not validate the candidate frame";
            }
        });
    wait(instance, scope, deadline, "WebGPU validation");
    if (!failure->empty()) throw std::runtime_error(*failure);
}

void M1Preview::State::render(wgpu::CommandEncoder encoder, const wgpu::RenderPipeline& program,
                              const Bindings& bindings, const wgpu::Texture& target) {
    const std::array<float, 4> values{static_cast<float>(target.GetWidth()), static_cast<float>(target.GetHeight()),
        bindings.fixedTime.value_or(std::chrono::duration<float>(Clock::now() - started).count()), 0.f};
    static_assert(sizeof(values) == rendererUniformBytes);
    queue.WriteBuffer(uniformBuffer, 0, values.data(), sizeof(values));
    wgpu::RenderPassColorAttachment color;
    color.view = target.CreateView();
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor passDescriptor;
    passDescriptor.colorAttachmentCount = 1;
    passDescriptor.colorAttachments = &color;
    auto pass = encoder.BeginRenderPass(&passDescriptor);
    pass.SetPipeline(program);
    pass.SetBindGroup(0, bindings.group);
    pass.Draw(3);
    pass.End();
}

void M1Preview::State::copyToSurface(const wgpu::Texture& target, const Clock::time_point* deadline) {
    wgpu::SurfaceTexture current;
    surface.GetCurrentTexture(&current);
    if (current.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
        current.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal)
        throw std::runtime_error("Could not acquire a WebGPU surface texture (status " +
                                 std::to_string(static_cast<int>(current.status)) + ")");
    if (deadline) device.PushErrorScope(wgpu::ErrorFilter::Validation);
    auto encoder = device.CreateCommandEncoder();
    if (!deadline) render(encoder, pipeline, activeBindings, target);
    wgpu::TexelCopyTextureInfo from;
    from.texture = target;
    wgpu::TexelCopyTextureInfo to;
    to.texture = current.texture;
    const wgpu::Extent3D size{target.GetWidth(), target.GetHeight(), 1};
    encoder.CopyTextureToTexture(&from, &to, &size);
    auto commands = encoder.Finish();
    queue.Submit(1, &commands);
    if (deadline) {
        checkValidation(*deadline);
        gpuDone(*deadline);
    }
    if (surface.Present() != wgpu::Status::Success)
        throw std::runtime_error("WebGPU could not present the preview frame");
}

void M1Preview::draw() {
    if (shouldClose()) throw std::runtime_error("WebGPU preview window closed");
    state->instance.ProcessEvents();
    state->copyToSurface(state->image, nullptr);
    ++frames;
}

#ifdef VOSC_RECOVERY_TESTING
std::vector<uint8_t> M1Preview::readbackFixedFrame() const {
    const auto& gpu = *state;
    if (!gpu.activeBindings.fixedTime || !gpu.image || uint64_t(width) * height > 1024 * 1024)
        throw std::runtime_error("A bounded fixed-time WebGPU frame is required");
    const uint32_t rowBytes = width * 4;
    const uint32_t stride = (rowBytes + bytesPerRow - 1) & ~(bytesPerRow - 1);
    wgpu::BufferDescriptor bufferDescriptor;
    bufferDescriptor.size = static_cast<uint64_t>(stride) * height;
    bufferDescriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    auto buffer = gpu.device.CreateBuffer(&bufferDescriptor);
    auto encoder = gpu.device.CreateCommandEncoder();
    wgpu::TexelCopyTextureInfo from;
    from.texture = gpu.image;
    wgpu::TexelCopyBufferInfo to;
    to.buffer = buffer;
    to.layout.bytesPerRow = stride;
    const wgpu::Extent3D size{width, height, 1};
    encoder.CopyTextureToBuffer(&from, &to, &size);
    auto commands = encoder.Finish();
    gpu.queue.Submit(1, &commands);
    auto mapped = std::make_shared<bool>(false);
    auto map = buffer.MapAsync(wgpu::MapMode::Read, 0, bufferDescriptor.size, waitOnly,
        [mapped](wgpu::MapAsyncStatus status, wgpu::StringView) { *mapped = status == wgpu::MapAsyncStatus::Success; });
    wait(gpu.instance, map, Clock::now() + frameTimeout, "WebGPU frame capture");
    if (!*mapped) throw std::runtime_error("Could not map the WebGPU frame");
    std::vector<uint8_t> rgba(static_cast<size_t>(rowBytes) * height);
    for (uint32_t y = 0; y < height; ++y) {
        if (buffer.ReadMappedRange(static_cast<size_t>(stride) * y, rgba.data() + static_cast<size_t>(rowBytes) * y,
                                   rowBytes) != wgpu::Status::Success)
            throw std::runtime_error("Could not read a WebGPU frame row");
    }
    buffer.Unmap();
    if (gpu.format == wgpu::TextureFormat::BGRA8Unorm)
        for (size_t offset = 0; offset < rgba.size(); offset += 4)
            std::swap(rgba[offset], rgba[offset + 2]);
    return rgba;
}
#endif

bool M1Preview::shouldClose() const { return glfwWindowShouldClose(state->window.get()); }

bool M1Preview::hasDevice() const { return static_cast<bool>(state->device); }

bool M1Preview::deviceLost() const {
    return hasDevice() && state->instance.WaitAny(state->device.GetLostFuture(), 0) == wgpu::WaitStatus::Success;
}

bool M1Preview::visible() const {
    return glfwGetWindowAttrib(state->window.get(), GLFW_VISIBLE) == GLFW_TRUE;
}

} // namespace webgpu

#endif
