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
#include <stdexcept>
#include <string_view>
#include <utility>

#ifdef __APPLE__
void* voscM1MetalLayer(void* cocoaWindow);
#endif

namespace webgpu {
namespace {

constexpr auto waitOnly = wgpu::CallbackMode::WaitAnyOnly;
constexpr uint64_t timeoutNanoseconds = 1'000'000'000;
constexpr uint32_t bytesPerRow = 256;

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

void wait(wgpu::Instance instance, wgpu::Future future, const char* operation) {
    if (instance.WaitAny(future, timeoutNanoseconds) != wgpu::WaitStatus::Success)
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
    Window window(glfwCreateWindow(width, height, "VOSC Draft Preview (WebGPU M1)", nullptr, nullptr),
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
    if (!source.display || !source.window) throw std::runtime_error("The Linux M1 preview requires X11");
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
    Window window;
    wgpu::Instance instance;
    wgpu::Surface surface;
    wgpu::Device device;
    wgpu::RenderPipeline pipeline;
    wgpu::Texture image;
    wgpu::Queue queue;
    void render(wgpu::CommandEncoder encoder);
    void gpuDone();
    void checkValidation();
    void present(bool drawShader = false);
};

M1Preview::M1Preview(GLFWwindow* outgoing, const std::string& fragment)
    : state(std::make_unique<State>(outgoing)) {
    auto& gpu = *state;
    const auto timedWait = wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor instanceDescriptor;
    instanceDescriptor.requiredFeatureCount = 1;
    instanceDescriptor.requiredFeatures = &timedWait;
    gpu.instance = wgpu::CreateInstance(&instanceDescriptor);
    if (!gpu.instance) throw std::runtime_error("Could not create the Dawn instance");
    gpu.surface = createSurface(gpu.instance, gpu.window.get());
    if (!gpu.surface) throw std::runtime_error("Could not create the WebGPU preview surface");

    wgpu::RequestAdapterOptions options;
#ifdef __APPLE__
    options.backendType = wgpu::BackendType::Metal;
#else
    options.backendType = wgpu::BackendType::Vulkan;
#endif
    options.compatibleSurface = gpu.surface;
    auto selected = std::make_shared<Creation<wgpu::Adapter>>();
    auto requestAdapter = gpu.instance.RequestAdapter(&options, waitOnly,
        [selected](wgpu::RequestAdapterStatus status, wgpu::Adapter handle, wgpu::StringView detail) {
            if (status == wgpu::RequestAdapterStatus::Success) selected->handle = std::move(handle);
            else selected->detail = message(detail);
        });
    wait(gpu.instance, requestAdapter, "Dawn adapter request");
    if (!selected->handle) throw std::runtime_error("Dawn adapter request failed: " + selected->detail);
    wgpu::AdapterInfo info;
    if (selected->handle.GetInfo(&info) != wgpu::Status::Success || info.backendType != options.backendType)
        throw std::runtime_error("Dawn returned an unexpected backend");
    backend = info.backendType == wgpu::BackendType::Metal ? "Metal" : "Vulkan";
    adapter = message(info.device);
    const auto driver = message(info.description);
    if (!driver.empty()) adapter += (adapter.empty() ? "" : " / ") + driver;
    auto opened = std::make_shared<Creation<wgpu::Device>>();
    auto requestDevice = selected->handle.RequestDevice(nullptr, waitOnly,
        [opened](wgpu::RequestDeviceStatus status, wgpu::Device handle, wgpu::StringView detail) {
            if (status == wgpu::RequestDeviceStatus::Success) opened->handle = std::move(handle);
            else opened->detail = message(detail);
        });
    wait(gpu.instance, requestDevice, "Dawn device request");
    if (!opened->handle) throw std::runtime_error("Dawn device request failed: " + opened->detail);
    gpu.device = std::move(opened->handle);
    gpu.queue = gpu.device.GetQueue();

    wgpu::SurfaceCapabilities capabilities;
    if (gpu.surface.GetCapabilities(selected->handle, &capabilities) != wgpu::Status::Success ||
        !(capabilities.usages & wgpu::TextureUsage::CopyDst))
        throw std::runtime_error("WebGPU preview surface cannot receive the staged image");
    auto format = wgpu::TextureFormat::Undefined;
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
    glfwGetFramebufferSize(gpu.window.get(), &backingWidth, &backingHeight);
    if (backingWidth < 2 || backingHeight < 2)
        throw std::runtime_error("WebGPU preview window has no usable framebuffer");
    width = static_cast<uint32_t>(backingWidth);
    height = static_cast<uint32_t>(backingHeight);
    gpu.device.PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::TextureDescriptor imageDescriptor;
    imageDescriptor.format = format;
    imageDescriptor.size = {width, height, 1};
    imageDescriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    gpu.image = gpu.device.CreateTexture(&imageDescriptor);

    wgpu::ColorTargetState target;
    target.format = format;
    wgpu::FragmentState fragmentState;
    fragmentState.module = shaderModule(gpu.device, fragment);
    fragmentState.entryPoint = "fs_main";
    fragmentState.targetCount = 1;
    fragmentState.targets = &target;
    wgpu::RenderPipelineDescriptor pipelineDescriptor;
    pipelineDescriptor.vertex.module = shaderModule(gpu.device, vertexSource);
    pipelineDescriptor.vertex.entryPoint = "vs_main";
    pipelineDescriptor.fragment = &fragmentState;
    auto compiled = std::make_shared<Creation<wgpu::RenderPipeline>>();
    auto createPipeline = gpu.device.CreateRenderPipelineAsync(&pipelineDescriptor, waitOnly,
        [compiled](wgpu::CreatePipelineAsyncStatus status, wgpu::RenderPipeline handle, wgpu::StringView detail) {
            if (status == wgpu::CreatePipelineAsyncStatus::Success) compiled->handle = std::move(handle);
            else compiled->detail = message(detail);
        });
    wait(gpu.instance, createPipeline, "WGSL pipeline creation");
    if (!compiled->handle) throw std::runtime_error("WGSL pipeline creation failed: " + compiled->detail);
    gpu.pipeline = std::move(compiled->handle);

    // Read four pixels from the same GPU image that the surface will present.
    pixels = {{{width / 4, height / 4, {}}, {3 * width / 4, height / 4, {}},
               {width / 4, 3 * height / 4, {}}, {3 * width / 4, 3 * height / 4, {}}}};
    wgpu::BufferDescriptor readbackDescriptor;
    readbackDescriptor.size = bytesPerRow * pixels.size();
    readbackDescriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    auto readback = gpu.device.CreateBuffer(&readbackDescriptor);
    auto encoder = gpu.device.CreateCommandEncoder();
    gpu.render(encoder);
    const wgpu::Extent3D onePixel{1, 1, 1};
    for (size_t i = 0; i < pixels.size(); ++i) {
        wgpu::TexelCopyTextureInfo from;
        from.texture = gpu.image;
        from.origin = {pixels[i].x, pixels[i].y, 0};
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
    wait(gpu.instance, map, "WebGPU readback");
    if (!*mapped) throw std::runtime_error("Could not map the staged WebGPU image");
    for (size_t i = 0; i < pixels.size(); ++i) {
        auto& rgba = pixels[i].rgba;
        if (readback.ReadMappedRange(i * bytesPerRow, rgba.data(), rgba.size()) != wgpu::Status::Success)
            throw std::runtime_error("Could not read the staged WebGPU pixel");
        if (format == wgpu::TextureFormat::BGRA8Unorm) std::swap(rgba[0], rgba[2]);
    }
    readback.Unmap();
    gpu.checkValidation();

    gpu.device.PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::SurfaceConfiguration configuration;
    configuration.device = gpu.device;
    configuration.format = format;
    configuration.width = width;
    configuration.height = height;
    configuration.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopyDst;
    configuration.presentMode = wgpu::PresentMode::Fifo;
    gpu.surface.Configure(&configuration);
#ifdef __APPLE__
    // The completed offscreen draw gates showing the Cocoa window. Keep
    // OpenGL visible while waiting for the first drawable to present.
    glfwShowWindow(gpu.window.get());
#endif
    gpu.present();
    gpu.gpuDone();
    gpu.checkValidation();
#ifndef __APPLE__
    glfwShowWindow(gpu.window.get());
#endif
    ++frames;
}

M1Preview::~M1Preview() = default;

void M1Preview::State::gpuDone() {
    auto completed = std::make_shared<bool>(false);
    auto work = queue.OnSubmittedWorkDone(waitOnly,
        [completed](wgpu::QueueWorkDoneStatus status, wgpu::StringView) {
            *completed = status == wgpu::QueueWorkDoneStatus::Success;
        });
    wait(instance, work, "WebGPU surface copy");
    if (!*completed) throw std::runtime_error("WebGPU queue did not finish the surface copy");
}

void M1Preview::State::checkValidation() {
    auto failure = std::make_shared<std::string>();
    auto scope = device.PopErrorScope(waitOnly,
        [failure](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type, wgpu::StringView detail) {
            if (status != wgpu::PopErrorScopeStatus::Success || type != wgpu::ErrorType::NoError) {
                *failure = message(detail);
                if (failure->empty()) *failure = "WebGPU could not validate the candidate frame";
            }
        });
    wait(instance, scope, "WebGPU validation");
    if (!failure->empty()) throw std::runtime_error(*failure);
}

void M1Preview::State::render(wgpu::CommandEncoder encoder) {
    wgpu::RenderPassColorAttachment color;
    color.view = image.CreateView();
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor passDescriptor;
    passDescriptor.colorAttachmentCount = 1;
    passDescriptor.colorAttachments = &color;
    auto pass = encoder.BeginRenderPass(&passDescriptor);
    pass.SetPipeline(pipeline);
    pass.Draw(3);
    pass.End();
}

void M1Preview::State::present(bool drawShader) {
    wgpu::SurfaceTexture current;
    surface.GetCurrentTexture(&current);
    if (current.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
        current.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal)
        throw std::runtime_error("Could not acquire a WebGPU surface texture (status " +
                                 std::to_string(static_cast<int>(current.status)) + ")");
    auto encoder = device.CreateCommandEncoder();
    if (drawShader) render(encoder);
    wgpu::TexelCopyTextureInfo from;
    from.texture = image;
    wgpu::TexelCopyTextureInfo to;
    to.texture = current.texture;
    const wgpu::Extent3D size{image.GetWidth(), image.GetHeight(), 1};
    encoder.CopyTextureToTexture(&from, &to, &size);
    auto commands = encoder.Finish();
    queue.Submit(1, &commands);
    if (surface.Present() != wgpu::Status::Success)
        throw std::runtime_error("WebGPU could not present the preview frame");
}

void M1Preview::draw() {
    if (glfwWindowShouldClose(state->window.get())) throw std::runtime_error("WebGPU preview window closed");
    state->instance.ProcessEvents();
    state->present(true);
    ++frames;
}

bool M1Preview::visible() const {
    return glfwGetWindowAttrib(state->window.get(), GLFW_VISIBLE) == GLFW_TRUE;
}

} // namespace webgpu

#endif
