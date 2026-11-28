#include "EngineControl.hpp"
#include "../Logger.h"
#include "../RuntimeContext.hpp"
#include "../systems/CommandSystem.hpp"
#include "../systems/RenderSystem.hpp"
#include "../ResourceRegistry.hpp"
#include "../var/ComponentLiteral.hpp"
#include "../var/VariablePool.h"
#include "../tex/ImageTex.h"
#include "../tex/ShaderPingPongTex.h"
#include "../light/Lights.h"
#include "ofAppGLFWWindow.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <utility>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <string_view>
#ifdef VOSC_WEBGPU_M1
#include <unistd.h>
#include <iostream>
#endif

namespace control {
namespace {
using Json = ofJson;

constexpr char draftCallbackUniform[] = "u_vosc_callback";
constexpr float draftCallbackInitial = 0.25f;

struct Invalid : std::runtime_error {
    std::string code;
    Invalid(const std::string& code, const std::string& message)
        : std::runtime_error(message), code(code) {}
};

void keys(const Json& params, std::initializer_list<const char*> required,
          std::initializer_list<const char*> optional = {}) {
    if (!params.is_object()) throw Invalid("invalid_params", "params must be an object");
    std::set<std::string> allowed;
    for (auto name : required) {
        allowed.insert(name);
        if (!params.count(name)) throw Invalid("invalid_params", std::string("missing ") + name);
    }
    for (auto name : optional) allowed.insert(name);
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (!allowed.count(it.key())) throw Invalid("invalid_params", "unknown parameter: " + it.key());
    }
}

int integer(const Json& value, int minimum, int maximum, const std::string& name) {
    if (!value.is_number_integer() || value.get<double>() < minimum || value.get<double>() > maximum)
        throw Invalid("invalid_params", name + " must be an integer in range " +
                      std::to_string(minimum) + ".." + std::to_string(maximum));
    return value.get<int>();
}

uint64_t cursor(const Json& value, uint64_t maximum, const std::string& name) {
    if (!value.is_number_integer() || value.get<double>() < 0 ||
        value.get<double>() > static_cast<double>(maximum))
        throw Invalid("invalid_params", name + " must be an available nonnegative integer");
    return value.get<uint64_t>();
}

double number(const Json& value, double minimum, double maximum) {
    if (!value.is_number()) throw Invalid("invalid_params", "expected a number");
    double result = value.get<double>();
    if (!std::isfinite(result) || result < minimum || result > maximum)
        throw Invalid("invalid_params", "number outside supported range");
    return result;
}

std::string text(const Json& value, size_t maximum, const std::string& name) {
    if (!value.is_string()) throw Invalid("invalid_params", name + " must be a string");
    auto result = value.get<std::string>();
    if (result.empty() || result.size() > maximum)
        throw Invalid("invalid_params", name + " has invalid length");
    return result;
}

std::shared_ptr<Logger> logger() {
    return std::dynamic_pointer_cast<Logger>(ofGetLoggerChannel());
}

GLFWwindow* nativeGlWindow() {
    auto* window = dynamic_cast<ofAppGLFWWindow*>(ofGetWindowPtr());
    return window ? window->getGLFWWindow() : nullptr;
}

std::pair<int, int> nativeFramebufferSize() {
    auto* window = nativeGlWindow();
    if (!window) return {-1, -1};
    int width = -1;
    int height = -1;
    glfwGetFramebufferSize(window, &width, &height);
    return {width, height};
}

int uniformComponents(GLenum type) {
    switch (type) {
        case GL_FLOAT: return 1;
        case GL_FLOAT_VEC2: return 2;
        case GL_FLOAT_VEC3: return 3;
        case GL_FLOAT_VEC4: return 4;
        default: return 0;
    }
}

int recipeUniformComponents(GLenum type) {
    switch (type) {
        case GL_INT: return 1;
        case GL_INT_VEC2: return 2;
        case GL_INT_VEC3: return 3;
        case GL_INT_VEC4: return 4;
        default: return uniformComponents(type);
    }
}

bool managedUniform(const Layer& layer, const std::string& name) {
    for (const auto& binding : layer.shader.getTextures()) {
        if (name == binding.first + "Size") return true;
    }
    for (const auto& binding : layer.shader.getBuffers()) {
        if (name == binding.first + "Size") return true;
    }
    return Shader::isRendererUniform(name) ||
           layer.vars.count(name) || name.find('[') != std::string::npos;
}

void setNumericBinding(VarsHolder& holder, const std::string& name, const std::vector<float>& values) {
    auto& pool = VariablePool::getPool(&holder);
    auto found = holder.vars.find(name);
    auto variable = found == holder.vars.end() ? nullptr :
        std::dynamic_pointer_cast<Variable<float>>(found->second);
    if (!variable) variable = std::make_shared<Variable<float>>();
    variable->set(values);
    pool[name] = variable;
    holder.vars[name] = variable;
}

int editableUniformComponents(const Layer& layer, const std::string& name) {
    const auto& shader = layer.shader;
    const auto& types = shader.getUniforms();
    const auto found = types.find(name);
    if (managedUniform(layer, name) ||
        (found != types.end() && (found->second.isArray() || found->second.graphSampler)))
        throw Invalid("invalid_uniform", "uniform is engine-managed or an array");
    if (!shader.isShaderLoaded()) throw Invalid("invalid_uniform", "no ordinary GLSL shader loaded");
    if (found == types.end() || !uniformComponents(found->second.type))
        throw Invalid("invalid_uniform", "uniform is absent or has an unsupported type");
    return uniformComponents(found->second.type);
}

std::string draftSlot(const Json& value) {
    auto id = text(value, 32, "slot");
    if (!validDraftSlot(id)) throw Invalid("invalid_slot", "slot ID must be an ASCII expression identifier");
    return id;
}

std::shared_ptr<Variable<float>> draftBinding(const Json& components, const Inputs* staged = nullptr) {
    std::vector<float> values;
    std::vector<std::string> expressions;
    std::vector<bool> mapped;
    bool hasMapping = false;
    for (const auto& component : components) {
        if (component.contains("fixed")) {
            values.push_back(component["fixed"].get<float>());
            expressions.push_back(component["fixed"].dump());
            mapped.push_back(false);
        } else {
            const auto low = component["low"].get<double>();
            const auto high = component["high"].get<double>();
            expressions.push_back("(" + component["low"].dump() + ")+(" + Json(high - low).dump() +
                                  ")*in_" + component["slot"].get<std::string>());
            mapped.push_back(true);
            hasMapping = true;
        }
    }
    auto variable = std::make_shared<Variable<float>>();
    if (hasMapping) variable->set(expressions, mapped, staged);
    else variable->set(std::move(values));
    return variable;
}

void draftSample(Inputs& inputs, const std::string& slot, float value) {
    ofxOscMessage sample;
    sample.addStringArg(slot);
    sample.addFloatArg(value);
    sample.addFloatArg(0.f);
    sample.addInt32Arg(0);
    inputs.oscCommand("/input/data", sample);
}

void draftConfigureSlot(Inputs& inputs, const std::string& id, float initial) {
    ofxOscMessage setup;
    setup.addStringArg(id);
    setup.addStringArg("");
    setup.addInt32Arg(0);
    setup.addInt32Arg(256);
    inputs.oscCommand("/input", setup);
    ofxOscMessage gain;
    gain.addStringArg(id);
    gain.addStringArg("maxAmp");
    gain.addFloatArg(1.f);
    inputs.oscCommand("/input/set", gain);
    draftSample(inputs, id, initial);
}

const char* draftUniformType(int arity) {
    static constexpr const char* types[] = {"unsupported", "float", "vec2", "vec3", "vec4"};
    return types[arity];
}

std::string draftDigest(const Json& value) {
    const auto digest = text(value, 64, "digest");
    if (digest.size() != 64 || digest.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw Invalid("invalid_params", "expected lowercase SHA-256");
    return digest;
}

void checkDraftComponents(const Json& components, const std::set<std::string>& slots, int arity) {
    if (!components.is_array() || components.empty() || components.size() > 4 ||
        (arity && components.size() != static_cast<size_t>(arity)))
        throw Invalid("invalid_params", "uniform value component count mismatch");
    for (const auto& part : components) {
        if (part.contains("fixed")) {
            keys(part, {"fixed"});
            number(part["fixed"], -1000000, 1000000);
        } else {
            keys(part, {"slot", "low", "high"});
            if (!slots.count(draftSlot(part["slot"])))
                throw Invalid("missing_slot", "OSC slot is absent in this draft");
            number(part["low"], -1000000, 1000000);
            number(part["high"], -1000000, 1000000);
        }
    }
}

constexpr std::string_view base64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr size_t maxDraftSourceBytes = 128 * 1024;
constexpr size_t maxEncodedDraftSourceBytes = (maxDraftSourceBytes + 2) / 3 * 4;

std::string base64(const ofBuffer& buffer) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(buffer.getData());
    std::string result;
    result.reserve((buffer.size() + 2) / 3 * 4);
    for (size_t i = 0; i < buffer.size(); i += 3) {
        unsigned value = unsigned(bytes[i]) << 16;
        if (i + 1 < buffer.size()) value |= unsigned(bytes[i + 1]) << 8;
        if (i + 2 < buffer.size()) value |= bytes[i + 2];
        result += base64Alphabet[(value >> 18) & 63];
        result += base64Alphabet[(value >> 12) & 63];
        result += i + 1 < buffer.size() ? base64Alphabet[(value >> 6) & 63] : '=';
        result += i + 2 < buffer.size() ? base64Alphabet[value & 63] : '=';
    }
    return result;
}

std::string decodeDraftSource(const Json& input) {
    const auto encoded = text(input, maxEncodedDraftSourceBytes, "source_base64");
    if (encoded.size() % 4)
        throw Invalid("invalid_params", "Draft source must be padded base64");
    std::string decoded;
    decoded.reserve(encoded.size() / 4 * 3);
    for (size_t i = 0; i < encoded.size(); i += 4) {
        const bool last = i + 4 == encoded.size();
        const bool padThird = last && encoded[i + 2] == '=';
        const bool padFourth = last && encoded[i + 3] == '=';
        const auto a = base64Alphabet.find(encoded[i]);
        const auto b = base64Alphabet.find(encoded[i + 1]);
        const auto c = padThird ? 0 : base64Alphabet.find(encoded[i + 2]);
        const auto d = padFourth ? 0 : base64Alphabet.find(encoded[i + 3]);
        if (a > 63 || b > 63 || c > 63 || d > 63 ||
            (padThird && (!padFourth || (b & 15))) || (padFourth && (c & 3)))
            throw Invalid("invalid_params", "Draft source is not canonical base64");
        decoded += static_cast<char>((a << 2) | (b >> 4));
        if (!padThird) decoded += static_cast<char>(((b & 15) << 4) | (c >> 2));
        if (!padFourth) decoded += static_cast<char>(((c & 3) << 6) | d);
    }
    if (decoded.empty() || decoded.size() > maxDraftSourceBytes || decoded.find('\0') != std::string::npos)
        throw Invalid("invalid_params", "Draft source must be nonempty, NUL-free and no larger than 128 KiB");
    return decoded;
}

bool excessiveNesting(const std::string& line) {
    bool quoted = false, escaped = false;
    int depth = 0;
    for (char ch : line) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
        } else if (ch == '"') quoted = true;
        else if (ch == '{' || ch == '[') { if (++depth > 64) return true; }
        else if (ch == '}' || ch == ']') --depth;
    }
    return false;
}

double monotonicSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Json recipeArg(const char* type, const Json& value) {
    return {{"type", type}, {"value", value}};
}

Json recipeCommand(const std::string& address, const Json& args) {
    return {{"address", address}, {"args", args}};
}

bool containsReservedReference(const std::string& expression) {
    for (const auto& name : {"camPos", "camLook", "camOrbit"}) {
        const auto at = expression.find(name);
        if (at != std::string::npos) {
            const bool left = at == 0 || !(std::isalnum(static_cast<unsigned char>(expression[at - 1])) || expression[at - 1] == '_');
            const size_t end = at + std::strlen(name);
            const bool right = end == expression.size() || !(std::isalnum(static_cast<unsigned char>(expression[end])) || expression[end] == '_');
            if (left && right) return true;
        }
    }
    return false;
}

template<typename T>
Json variableRecipeArgs(const std::shared_ptr<Variable<T>>& variable) {
    Json result = Json::array();
    const auto expressions = variable->getExpressions();
    const size_t componentCount = static_cast<size_t>(T::length());
    if (variable->size() != 1 || (!expressions.empty() && expressions.size() != componentCount))
        throw Invalid("scene_unsupported", "repeated or noncanonical vector variable shape is unsupported");
    if (!expressions.empty()) {
        for (size_t index = 0; index < expressions.size(); ++index) {
            const auto& expression = expressions[index];
            if (variable->isExpressionComponent(index)) {
                if (expression.empty() || expression.size() > 2048 || containsReservedReference(expression))
                    throw Invalid("scene_unsupported", "expression is empty, oversized, or references a reserved camera variable");
                result.push_back(recipeArg("s", expression));
            } else {
                const auto component = parseComponentLiteral(expression);
                if (!component)
                    throw Invalid("scene_unsupported", "vector component is invalid");
                if (std::abs(*component) > 1000000.)
                    throw Invalid("scene_unsupported", "vector component is outside recipe bounds");
                result.push_back(recipeArg("f", static_cast<float>(*component)));
            }
        }
        return result;
    }
    for (const auto& value : variable->getVec()) {
        const float* components = glm::value_ptr(value);
        for (size_t i = 0; i < sizeof(T) / sizeof(float); ++i) {
            if (!std::isfinite(components[i]) || std::abs(components[i]) > 1000000.f)
                throw Invalid("scene_unsupported", "variable component is outside recipe bounds");
            result.push_back(recipeArg("f", components[i]));
        }
    }
    return result;
}

template<>
Json variableRecipeArgs<float>(const std::shared_ptr<Variable<float>>& variable) {
    Json result = Json::array();
    const auto expressions = variable->getExpressions();
    if (variable->size() < 1 || variable->size() > 4 ||
        (!expressions.empty() && expressions.size() != variable->size()))
        throw Invalid("scene_unsupported", "repeated or noncanonical float variable shape is unsupported");
    if (!expressions.empty()) {
        for (const auto& expression : expressions) {
            if (expression.empty() || expression.size() > 2048 || containsReservedReference(expression))
                throw Invalid("scene_unsupported", "expression is empty, oversized, or references a reserved camera variable");
            result.push_back(recipeArg("s", expression));
        }
    } else for (float value : variable->getVec()) {
        if (!std::isfinite(value) || std::abs(value) > 1000000.f)
            throw Invalid("scene_unsupported", "variable component is outside recipe bounds");
        result.push_back(recipeArg("f", value));
    }
    return result;
}

template<>
Json variableRecipeArgs<int>(const std::shared_ptr<Variable<int>>& variable) {
    Json result = Json::array();
    const auto expressions = variable->getExpressions();
    if (variable->size() < 1 || variable->size() > 4 ||
        (!expressions.empty() && expressions.size() != variable->size()))
        throw Invalid("scene_unsupported", "noncanonical integer variable shape is unsupported");
    if (!expressions.empty()) for (size_t index = 0; index < expressions.size(); ++index) {
        const auto& expression = expressions[index];
        if (variable->isExpressionComponent(index)) {
            if (expression.empty() || expression.size() > 2048 || containsReservedReference(expression))
                throw Invalid("scene_unsupported", "integer expression is unsupported");
            result.push_back(recipeArg("s", expression));
        } else {
            size_t consumed = 0;
            int value = 0;
            try { value = std::stoi(expression, &consumed); }
            catch (...) { throw Invalid("scene_unsupported", "integer component is invalid"); }
            if (consumed != expression.size())
                throw Invalid("scene_unsupported", "integer component is invalid");
            result.push_back(recipeArg("i", value));
        }
    } else for (int value : variable->getVec()) result.push_back(recipeArg("i", value));
    return result;
}

template<>
Json variableRecipeArgs<ofFloatColor>(const std::shared_ptr<Variable<ofFloatColor>>& variable) {
    Json result = Json::array();
    const auto expressions = variable->getExpressions();
    if (variable->size() != 1 || (!expressions.empty() && expressions.size() != 4))
        throw Invalid("scene_unsupported", "repeated or noncanonical color variable shape is unsupported");
    if (!expressions.empty()) {
        for (size_t index = 0; index < expressions.size(); ++index) {
            const auto& expression = expressions[index];
            if (expression.empty() || expression.size() > 2048 || containsReservedReference(expression))
                throw Invalid("scene_unsupported", "color expression is invalid");
            if (variable->isExpressionComponent(index)) {
                result.push_back(recipeArg("s", expression));
            } else {
                const auto component = parseComponentLiteral(expression);
                if (!component)
                    throw Invalid("scene_unsupported", "color component is invalid");
                if (std::abs(*component) > 1000000.)
                    throw Invalid("scene_unsupported", "color component is outside recipe bounds");
                result.push_back(recipeArg("f", static_cast<float>(*component)));
            }
        }
    } else for (const auto& value : variable->getVec()) {
        for (float component : {value.r, value.g, value.b, value.a}) {
            if (!std::isfinite(component) || std::abs(component) > 1000000.f)
                throw Invalid("scene_unsupported", "color component is outside recipe bounds");
            result.push_back(recipeArg("f", component));
        }
    }
    return result;
}

Json describeVariable(const std::shared_ptr<BaseVar>& value, size_t numericComponents = 1) {
    if (auto variable = std::dynamic_pointer_cast<Variable<float>>(value)) {
        if (variable->size() != numericComponents)
            throw Invalid("scene_unsupported", "repeated or noncanonical float variable shape is unsupported");
        return variableRecipeArgs(variable);
    }
    if (auto variable = std::dynamic_pointer_cast<Variable<int>>(value)) {
        if (variable->size() != numericComponents)
            throw Invalid("scene_unsupported", "repeated or noncanonical integer variable shape is unsupported");
        return variableRecipeArgs(variable);
    }
    if (auto variable = std::dynamic_pointer_cast<Variable<glm::vec2>>(value)) return variableRecipeArgs(variable);
    if (auto variable = std::dynamic_pointer_cast<Variable<glm::vec3>>(value)) return variableRecipeArgs(variable);
    if (auto variable = std::dynamic_pointer_cast<Variable<ofFloatColor>>(value)) return variableRecipeArgs(variable);
    throw Invalid("scene_unsupported", "variable type is outside editable recipe scope");
}

void appendAsset(Json& assets, const std::string& name, const std::string& kind,
                 const std::string& encoding, const std::string& data,
                 size_t& fragmentBytes, size_t& imageBytes) {
    if (assets.size() >= 64) throw Invalid("scene_unsupported", "scene exceeds 64 assets");
    if (kind == "fragment" || kind == "feedback_fragment") {
        if (data.empty() || data.size() > 128 * 1024 || fragmentBytes + data.size() > 1024 * 1024)
            throw Invalid("scene_unsupported", "fragment asset exceeds recipe bounds");
        fragmentBytes += data.size();
    } else {
        const size_t decoded = data.size() / 4 * 3;
        if (imageBytes + decoded > 4 * 1024 * 1024) throw Invalid("scene_unsupported", "image assets exceed recipe bounds");
        imageBytes += decoded;
    }
    assets.push_back({{"name", name}, {"kind", kind}, {"encoding", encoding}, {"data", data}});
}

void appendHolderCommand(Json& commands, const std::string& address, int layer,
                         const std::shared_ptr<BaseVar>& value) {
    Json args = Json::array({recipeArg("i", layer)});
    for (auto item : describeVariable(value)) {
        if (address == "/layer/visible" && item["type"] == "f") {
            const float visible = item["value"].get<float>();
            if (visible != 0.f && visible != 1.f)
                throw Invalid("scene_unsupported", "constant layer visibility must be zero or one");
            item = recipeArg("i", int(visible));
        }
        args.push_back(item);
    }
    commands.push_back(recipeCommand(address, args));
}

Json exportRecipe(RenderSystem& render, const RuntimeContext& runtime, const CommandSystem& commandSystem,
                  int width, int height,
                  int onsetGate) {
    if (width < 64 || height < 64 || width > 2048 || height > 2048)
        throw Invalid("scene_unsupported", "window dimensions are outside recipe bounds");
    if (runtime.camera.isEnabled()) throw Invalid("scene_unsupported", "camera must be disabled");
    if (!Lights::get().all().empty()) throw Invalid("scene_unsupported", "lights are outside editable recipe scope");
    if (render.getLayers().size() > 32 || !render.supportsScenePersistence())
        throw Invalid("scene_unsupported", "layer count or render passes are outside recipe scope");
    auto* registry = ResourceRegistry::current();
    if (!registry) throw Invalid("scene_unsupported", "resource registry is unavailable");
    auto reservedVec = [&](const char* name, const glm::vec3& expected) {
        auto found = registry->variableSharedPool.find(name);
        auto value = found == registry->variableSharedPool.end() ? nullptr :
            std::dynamic_pointer_cast<Variable<glm::vec3>>(found->second);
        return value && value->isConst() && value->size() == 1 && value->get() == expected;
    };
    auto orbit = registry->variableSharedPool.find("camOrbit");
    auto orbitValue = orbit == registry->variableSharedPool.end() ? nullptr :
        std::dynamic_pointer_cast<Variable<float>>(orbit->second);
    if (!reservedVec("camPos", {0, 0, -870}) || !reservedVec("camLook", {0, 0, 0}) ||
        !orbitValue || !orbitValue->isConst() || orbitValue->size() != 1 || orbitValue->get() != 0)
        throw Invalid("scene_unsupported", "reserved camera variables must retain their default values");
    if (!registry->textureSharedPool.empty() || !registry->geomSharedPool.empty() ||
        !registry->shaderSharedPool.empty())
        throw Invalid("scene_unsupported", "named object aliases or custom geometry are unsupported (texture=" +
            std::to_string(registry->textureSharedPool.size()) + ", geom=" +
            std::to_string(registry->geomSharedPool.size()) + ", geom_layer=" +
            std::to_string(registry->geomLayerPool.size()) + ", shader=" +
            std::to_string(registry->shaderSharedPool.size()) + ")");
    Json commands = Json::array();
    Json assets = Json::array();
    std::string layout = "stack";
    for (const auto& item : LayoutMap) if (item.second == render.getLayout()) layout = item.first;
    size_t feedbackCount = 0;
    commands.push_back(recipeCommand("/layers/reset", Json::array({recipeArg("i", int(render.getLayers().size())), recipeArg("s", layout)})));

    for (const auto& item : runtime.inputs.all()) {
        const auto& input = item.second;
        if (!input) throw Invalid("scene_unsupported", "input is null");
        Json inputId = input->requestedIdIsInteger ? recipeArg("i", input->requestedIntegerId) : recipeArg("s", item.first);
        Json args = Json::array({inputId, recipeArg("s", input->requestedDevice),
            recipeArg("i", input->requestedChannels), recipeArg("i", input->requestedBufferSize)});
        commands.push_back(recipeCommand("/input", args));
        for (const auto& property : {"maxAmp", "maxLoud"}) if (input->vars.count(property)) {
            auto value = std::dynamic_pointer_cast<Variable<float>>(input->vars.at(property));
            if (!value || !value->isConst() || value->size() != 1 ||
                (std::string(property) == "maxAmp" && value->get() <= 0))
                throw Invalid("scene_unsupported", std::string("input calibration ") + property + " must be one constant float");
            commands.push_back(recipeCommand("/input/set", Json::array({inputId,
                recipeArg("s", property), recipeArg("f", value->get())})));
        }
    }
    std::map<std::string, Json> sharedExpressions;
    std::map<std::string, std::set<std::string>> sharedDependencies;
    for (const auto& item : registry->variableSharedPool) {
        if (item.first == "camPos" || item.first == "camLook" || item.first == "camOrbit") continue;
        auto variable = std::dynamic_pointer_cast<Variable<float>>(item.second);
        if (!variable || variable->size() != 1) throw Invalid("scene_unsupported", "only scalar shared floats are supported");
        Json described = describeVariable(item.second);
        const float bootstrap = variable->isConst() ? variable->get() : 0.f;
        commands.push_back(recipeCommand("/var", Json::array({recipeArg("s", item.first), recipeArg("f", bootstrap)})));
        if (!variable->isConst()) sharedExpressions[item.first] = described.at(0);
    }
    for (const auto& expression : sharedExpressions) {
        const std::string text = expression.second.at("value").get<std::string>();
        for (const auto& candidate : sharedExpressions) {
            size_t at = text.find(candidate.first);
            while (at != std::string::npos) {
                const bool left = at == 0 || !(std::isalnum(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '_');
                const size_t end = at + candidate.first.size();
                const bool right = end == text.size() || !(std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_');
                if (left && right) sharedDependencies[expression.first].insert(candidate.first);
                at = text.find(candidate.first, at + candidate.first.size());
            }
        }
    }
    std::set<std::string> visiting, visited;
    std::vector<std::string> expressionOrder;
    std::function<void(const std::string&)> visit = [&](const std::string& name) {
        if (visiting.count(name)) throw Invalid("scene_unsupported", "shared expression dependency cycle is unsupported");
        if (visited.count(name)) return;
        visiting.insert(name);
        for (const auto& dependency : sharedDependencies[name]) if (sharedExpressions.count(dependency)) visit(dependency);
        visiting.erase(name); visited.insert(name); expressionOrder.push_back(name);
    };
    for (const auto& expression : sharedExpressions) visit(expression.first);
    for (const auto& name : expressionOrder)
        commands.push_back(recipeCommand("/var", Json::array({recipeArg("s", name), sharedExpressions.at(name)})));

    size_t fragmentBytes = 0, imageBytes = 0, decodedImageBytes = 0;
    for (size_t index = 0; index < render.getLayers().size(); ++index) {
        try {
            const auto& layerPtr = render.getLayers()[index];
            if (!layerPtr) throw Invalid("scene_unsupported", "layer is null");
            const auto& layer = *layerPtr;
            if (layer.delay != 0 || layer.behaviour != -1 || layer.data.noClear || !layer.data.aspectRatio ||
                !layer.data.hasDefaultStorageSettings())
                throw Invalid("scene_unsupported", "layer has unsupported delay, behavior, clearing, or storage state");
            const std::set<std::string> layerNames = {"pos", "size", "rotation", "scale", "pivot", "tint", "bri",
                "alpha", "speed", "visible", "visibleThresh", "onsetThresh"};
            if (layer.vars.size() != layerNames.size()) throw Invalid("scene_unsupported", "layer has extra or missing settings");
            for (const auto& binding : layer.vars) if (!layerNames.count(binding.first))
                throw Invalid("scene_unsupported", "layer has an unsupported setting");
            for (const auto& threshold : {"visibleThresh", "onsetThresh"}) {
                auto found = layer.vars.find(threshold);
                auto value = found == layer.vars.end() ? nullptr : std::dynamic_pointer_cast<Variable<float>>(found->second);
                if (!value || !value->isConst() || value->size() != 1 || value->get() != 1.f)
                    throw Invalid("scene_unsupported", std::string("layer ") + threshold + " must remain one");
            }
            bool sharedGeom = false;
            for (const auto& item : registry->geomSharedPool) if (item.second == layer.geom) sharedGeom = true;
            if (sharedGeom || (layer.geom && layer.geom->isLoaded() && !layer.geom->isCanonicalQuad()))
                throw Invalid("scene_unsupported", "layer geometry is not the canonical quad");
            const std::string fragmentName = "assets/layer-" + std::to_string(index) + ".frag";
            if (layer.shader.hasSelfFeedback())
                throw Invalid("scene_unsupported", "self-feedback requires Editor standalone replay export");
            if (!layer.shader.getBuffers().empty()) throw Invalid("scene_unsupported", "shader buffers are unsupported");
            if (layer.shader.isLoaded() && !layer.shader.isShaderLoaded())
                throw Invalid("scene_unsupported", "Shadertoy shaders are outside editable recipe scope");
            if (layer.shader.isShaderLoaded()) {
                std::string retainedSource, sourceReason;
                if (!layer.shader.getExportSource(retainedSource, sourceReason)) throw Invalid("scene_unsupported", sourceReason);
                appendAsset(assets, fragmentName, "fragment", "utf8", retainedSource, fragmentBytes, imageBytes);
                commands.push_back(recipeCommand("/shader", Json::array({recipeArg("i", int(index)), recipeArg("asset", fragmentName)})));
            }
            for (const auto& binding : layer.shader.vars) {
                const auto& types = layer.shader.getUniforms();
                const auto reflected = types.find(binding.first);
                if (!layer.shader.isShaderLoaded())
                    throw Invalid("scene_unsupported", "shader binding has no active program: " + binding.first);
                if (const char* error = Shader::sceneUniformError(types, binding.first))
                    throw Invalid("scene_unsupported", std::string(error) + ": " + binding.first);
                if (!recipeUniformComponents(reflected->second.type))
                    throw Invalid("scene_unsupported", "unsupported reflected shader uniform type: " + binding.first);
                Json args = Json::array({recipeArg("i", int(index)), recipeArg("s", binding.first)});
                for (const auto& item : describeVariable(binding.second, recipeUniformComponents(reflected->second.type))) args.push_back(item);
                commands.push_back(recipeCommand("/shader/var", args));
            }
            for (const auto& binding : layer.shader.getTextures()) {
                if (layer.shader.isShaderLoaded()) {
                    const auto error = Shader::sceneTextureError(layer.shader.getUniforms(),
                        binding.first, layer.shader.vars.count(binding.first + "Size"));
                    if (!error.empty()) throw Invalid("scene_unsupported", error);
                }
                const auto& texture = binding.second;
                auto feedback = texture ? dynamic_cast<ShaderPingPongTex*>(texture->tex.get()) : nullptr;
                if (feedback) {
                    std::string source, reason;
                    if (++feedbackCount > 1 || binding.first != DEFAULT_TEX || !texture->isLoaded() ||
                        texture->numFrames != 1 || !texture->passes.empty() || texture->hasLooper() ||
                        !feedback->firstUpdateComplete() || !feedback->validateConfiguration(texture->data, reason) ||
                        !feedback->getExportSource(source, reason))
                        throw Invalid("scene_unsupported", "feedback cannot be exported: " + reason);
                    auto speed = describeVariable(texture->vars.at("speed"));
                    if (speed != Json::array({recipeArg("f", 1.f)}))
                        throw Invalid("scene_unsupported", "feedback speed must remain constant one");
                    const std::set<std::string> defaults = {"size", "speed", "color", "tint", "timePct"};
                    for (const auto& name : {"color", "tint"}) {
                        auto value = texture->vars.count(name) ? std::dynamic_pointer_cast<Variable<ofFloatColor>>(texture->vars.at(name)) : nullptr;
                        if (!value || !value->isConst() || value->size() != 1 || value->get() != ofFloatColor(1.f, 1.f))
                            throw Invalid("scene_unsupported", "feedback color/tint must be white");
                    }
                    auto time = std::dynamic_pointer_cast<Variable<float>>(texture->vars.at("timePct"));
                    auto size = std::dynamic_pointer_cast<Variable<glm::vec3>>(texture->vars.at("size"));
                    if (!time || !time->isConst() || time->get() != 0 || !size || !size->isConst())
                        throw Invalid("scene_unsupported", "feedback size/timePct must be constant");
                    const std::string name = "assets/layer-" + std::to_string(index) + "-feedback.frag";
                    appendAsset(assets, name, "feedback_fragment", "utf8", source, fragmentBytes, imageBytes);
                    commands.push_back(recipeCommand("/tex", Json::array({recipeArg("i", int(index)), recipeArg("asset", name)})));
                    commands.push_back(recipeCommand("/tex/size", Json::array({recipeArg("i", int(index)), recipeArg("f", texture->data.getSize().x), recipeArg("f", texture->data.getSize().y)})));
                    commands.push_back(recipeCommand("/tex/speed", Json::array({recipeArg("i", int(index)), recipeArg("f", 1.f)})));
                    for (const auto& setting : std::vector<std::pair<std::string,int>>{{"internalformat",GL_RGBA8},{"textureTarget",GL_TEXTURE_2D},{"filter",GL_LINEAR},{"blendMode",0},{"aspectRatio",1},{"static",int(texture->isStatic)}})
                        commands.push_back(recipeCommand("/tex/set", Json::array({recipeArg("i", int(index)), recipeArg("s", setting.first), recipeArg("i", setting.second)})));
                    const auto& uniforms = feedback->getUniforms();
                    size_t controls = 0;
                    for (const auto& control : texture->vars) {
                        if (defaults.count(control.first)) continue;
                        const auto found = uniforms.find(control.first);
                        if (++controls > 64 || found == uniforms.end() || !uniformComponents(found->second.type))
                            throw Invalid("scene_unsupported", "feedback control lacks float reflection");
                        Json args = Json::array({recipeArg("i", int(index)), recipeArg("s", control.first)});
                        for (const auto& value : describeVariable(control.second, uniformComponents(found->second.type))) {
                            if (value.at("type") != "f") throw Invalid("scene_unsupported", "feedback control must be constant float");
                            args.push_back(value);
                        }
                        commands.push_back(recipeCommand("/tex/var", args));
                    }
                    continue;
                }
                if (!texture || !texture->isLoaded() || !dynamic_cast<ImageTex*>(texture->tex.get()) ||
                    texture->numFrames != 1 || !texture->passes.empty() || texture->hasLooper() ||
                    !texture->data.hasDefaultStorageSettings() || texture->getPixels().getWidth() > 2048 ||
                    texture->getPixels().getHeight() > 2048)
                    throw Invalid("scene_unsupported", "texture is not a bounded holder-local image");
                const std::set<std::string> textureNames = {"size", "speed", "color", "tint", "timePct"};
                if (texture->vars.size() != textureNames.size())
                    throw Invalid("scene_unsupported", "texture has extra or missing controls");
                for (const auto& item : texture->vars) if (!textureNames.count(item.first))
                    throw Invalid("scene_unsupported", "texture has an unsupported control");
                for (const auto& colorName : {"color", "tint"}) {
                    auto value = std::dynamic_pointer_cast<Variable<ofFloatColor>>(texture->vars.at(colorName));
                    if (!value || !value->isConst() || value->size() != 1 || value->get() != ofFloatColor(1.f, 1.f))
                        throw Invalid("scene_unsupported", std::string("texture ") + colorName + " must remain white");
                }
                auto sizeValue = std::dynamic_pointer_cast<Variable<glm::vec3>>(texture->vars.at("size"));
                auto timeValue = std::dynamic_pointer_cast<Variable<float>>(texture->vars.at("timePct"));
                const auto configuredSize = texture->data.getSize();
                if (!sizeValue || !sizeValue->isConst() || sizeValue->size() != 1 ||
                    !timeValue || !timeValue->isConst() || timeValue->size() != 1 || timeValue->get() != 0.f ||
                    !std::isfinite(configuredSize.x) || !std::isfinite(configuredSize.y) || configuredSize.x <= 0 ||
                    configuredSize.y <= 0 || configuredSize.x > 2048 || configuredSize.y > 2048)
                    throw Invalid("scene_unsupported", "texture size must be constant bounded 2D data and timePct must remain zero");
                const auto& pixels = texture->getPixels();
                const size_t decodedBytes = pixels.getWidth() * pixels.getHeight() * 4;
                if (decodedBytes > scene::MaxDecodedImageBytes - decodedImageBytes)
                    throw Invalid("scene_unsupported", "scene exceeds the 64 MiB decoded image budget");
                decodedImageBytes += decodedBytes;
                ofBuffer png;
                if (!ofSaveImage(pixels, png, OF_IMAGE_FORMAT_PNG))
                    throw Invalid("scene_unsupported", "image PNG capture failed");
                const std::string imageName = "assets/layer-" + std::to_string(index) + "-" + binding.first + ".png";
                appendAsset(assets, imageName, "image", "base64", base64(png), fragmentBytes, imageBytes);
                if (binding.first == DEFAULT_TEX) {
                    commands.push_back(recipeCommand("/tex", Json::array({recipeArg("i", int(index)), recipeArg("asset", imageName)})));
                    commands.push_back(recipeCommand("/tex/size", Json::array({recipeArg("i", int(index)), recipeArg("f", texture->data.getSize().x), recipeArg("f", texture->data.getSize().y)})));
                    commands.push_back(recipeCommand("/tex/speed", Json::array({recipeArg("i", int(index)), describeVariable(texture->vars.at("speed")).at(0)})));
                    commands.push_back(recipeCommand("/tex/set", Json::array({recipeArg("i", int(index)), recipeArg("s", "aspectRatio"), recipeArg("i", int(texture->data.aspectRatio))})));
                    commands.push_back(recipeCommand("/tex/set", Json::array({recipeArg("i", int(index)), recipeArg("s", "static"), recipeArg("i", int(texture->isStatic))})));
                    commands.push_back(recipeCommand("/tex/set", Json::array({recipeArg("i", int(index)), recipeArg("s", "blendMode"), recipeArg("i", int(texture->data.blendMode))})));
                } else {
                    commands.push_back(recipeCommand("/shader/texture", Json::array({recipeArg("i", int(index)), recipeArg("s", binding.first), recipeArg("asset", imageName)})));
                    commands.push_back(recipeCommand("/shader/texture/size", Json::array({recipeArg("i", int(index)), recipeArg("s", binding.first), recipeArg("f", texture->data.getSize().x), recipeArg("f", texture->data.getSize().y)})));
                    commands.push_back(recipeCommand("/shader/texture/speed", Json::array({recipeArg("i", int(index)), recipeArg("s", binding.first), describeVariable(texture->vars.at("speed")).at(0)})));
                    commands.push_back(recipeCommand("/shader/texture/set", Json::array({recipeArg("i", int(index)), recipeArg("s", binding.first), recipeArg("s", "aspectRatio"), recipeArg("i", int(texture->data.aspectRatio))})));
                    commands.push_back(recipeCommand("/shader/texture/set", Json::array({recipeArg("i", int(index)), recipeArg("s", binding.first), recipeArg("s", "static"), recipeArg("i", int(texture->isStatic))})));
                    commands.push_back(recipeCommand("/shader/texture/set", Json::array({recipeArg("i", int(index)), recipeArg("s", binding.first), recipeArg("s", "blendMode"), recipeArg("i", int(texture->data.blendMode))})));
                }
            }
            for (const auto& setting : {std::pair<const char*, const char*>{"pos", "/layer/pos"}, {"rotation", "/layer/rot"},
                {"scale", "/layer/scale"}, {"pivot", "/layer/pivot"}, {"size", "/layer/size"}, {"tint", "/layer/tint"},
                {"alpha", "/layer/alpha"}, {"bri", "/layer/bri"}, {"speed", "/layer/speed"}, {"visible", "/layer/visible"}}) {
                if (!layer.vars.count(setting.first)) throw Invalid("scene_unsupported", "layer is missing a required setting");
                appendHolderCommand(commands, setting.second, int(index), layer.vars.at(setting.first));
            }
            commands.push_back(recipeCommand("/layer/align", Json::array({recipeArg("i", int(index)), recipeArg("i", int(layer.alignH)), recipeArg("i", int(layer.alignV))})));
            commands.push_back(recipeCommand("/layer/blend", Json::array({recipeArg("i", int(index)), recipeArg("i", int(layer.data.blendMode))})));
        } catch (const Invalid& error) {
            throw Invalid(error.code, "layer " + std::to_string(index) + ": " + error.what());
        }
    }
    if (commands.size() > 2048) throw Invalid("scene_unsupported", "scene exceeds 2048 setup commands");
    std::string validationReason;
    if (!commandSystem.validateSceneRecipeCommands(commands, assets, validationReason))
        throw Invalid("scene_unsupported", validationReason);
    return {{"format", "vosc.osc-recipe"}, {"version", 1},
        {"resolution", {{"width", width}, {"height", height}}}, {"onset_gate", onsetGate},
        {"commands", commands}, {"assets", assets}};
}

} // namespace

EngineControl::EngineControl(RenderSystem& render, CommandSystem& commands, RuntimeContext& runtime)
    : sceneRender(&render), sceneCommands(&commands), sceneRuntime(&runtime), telemetry(makeOpenGlQueryBackend()),
      updateTelemetry(makeOpenGlQueryBackend()) {
    updateTelemetry.onGpuSample([this](uint64_t id, uint64_t origin, double ms) {
        if (measurementId == id) feedbackMeasurement.complete(origin, false, ms);
    });
    telemetry.onGpuSample([this](uint64_t id, uint64_t origin, double ms) {
        if (measurementId == id) feedbackMeasurement.complete(origin, true, ms);
    });
}

void EngineControl::bindScene(RenderSystem& render, CommandSystem& commands, RuntimeContext& runtime) {
    sceneRender = &render;
    sceneCommands = &commands;
    sceneRuntime = &runtime;
    noteExternalChange();
}

void EngineControl::setup() {
    const char* path = std::getenv("VOSC_CONTROL_SOCKET");
    if (!path || !*path) return;
    if (!server.start(path)) {
        ofLogError("control") << server.getLastError();
        return;
    }
    telemetry.setup();
    updateTelemetry.setup();
}

void EngineControl::shutdown() {
#ifdef VOSC_WEBGPU_M1
    draftWebGpuStaging.reset();
    m1Preview.reset();
#endif
    if (trial && trial->feedbackBaseline) interruptTrial();
    if (trial) {
        if (trial->layer < render().getLayers().size() && render().getLayers()[trial->layer]) {
            auto& shader = render().getLayers()[trial->layer]->shader;
            if (shader.ownsProgram(static_cast<GLuint>(trial->candidateProgram)))
                shader.restoreDirectSource(trial->baseline);
        }
        trial.reset();
    }
    telemetry.shutdown();
    updateTelemetry.shutdown();
    server.stop();
}

Json EngineControl::reply(const Json& id, const std::string& status) const {
    return {{"id", id}, {"ok", true}, {"status", status}, {"revision", revision},
            {"frame", frame}, {"external_epoch", externalEpoch}};
}

Json EngineControl::failure(const Json& id, const std::string& code,
                            const std::string& message, const Json& entries) const {
    auto result = reply(id, "failed");
    result["ok"] = false;
    result["error"] = {{"code", code}, {"message", message.substr(0, 512)}, {"diagnostics", entries}};
    return result;
}

std::string EngineControl::handle(const std::string& line) {
    Json id;
    try {
        if (excessiveNesting(line)) throw Invalid("invalid_json", "JSON nesting exceeds 64");
        Json request;
        try { request = Json::parse(line); }
        catch (const std::exception&) { throw Invalid("invalid_json", "malformed JSON request"); }
        if (request.is_object() && request.count("id")) id = request["id"];
        keys(request, {"id", "method"}, {"params"});
        text(id, 64, "id");
        text(request["method"], 64, "method");
        auto response = dispatch(request).dump();
        if (request["method"] == "export_scene" && response.size() >= 8 * 1024 * 1024)
            throw Invalid("scene_unsupported", "scene exceeds the 8 MiB export response limit");
        return response;
    } catch (const Invalid& error) {
        ofLogWarning("control") << error.what();
        return failure(id.is_string() ? id : Json(), error.code, error.what()).dump();
    } catch (const std::exception& error) {
        ofLogError("control") << error.what();
        return failure(id.is_string() ? id : Json(), "internal_error", "request failed").dump();
    } catch (...) {
        return failure(id.is_string() ? id : Json(), "internal_error", "request failed").dump();
    }
}

size_t EngineControl::pendingCount() const {
    size_t count = 0;
    for (const auto& pair : receipts) {
        auto status = pair.second.response.value("status", std::string());
        if (status == "queued" || status == "applied" || status == "evaluating" ||
            status == "ready" || status == "committing" || status == "rolling_back") ++count;
    }
    return count;
}

bool EngineControl::isPinned(const std::string& id) const {
#ifdef VOSC_WEBGPU_M1
    if (draftCandidate && draftCandidate->wgslPrepareId == id) return true;
#endif
    return (trial && (trial->id == id || trial->actionId == id)) || draftDraw.id == id;
}

bool EngineControl::makeReceiptSpace() {
    while (receipts.size() >= 64) {
        auto removable = receiptOrder.end();
        for (auto it = receiptOrder.begin(); it != receiptOrder.end(); ++it) {
            const auto status = receipts.at(*it).response.value("status", std::string());
            if (status != "queued" && status != "applied" && !isPinned(*it)) { removable = it; break; }
        }
        if (removable == receiptOrder.end()) return false;
        images.erase(std::remove(images.begin(), images.end(), *removable), images.end());
        receipts.erase(*removable);
        receiptOrder.erase(removable);
    }
    return true;
}

const EngineControl::Receipt* EngineControl::findReceipt(const std::string& id) const {
    if (draftCommitReceipt && draftCommitReceipt->request["id"] == id) return &*draftCommitReceipt;
    if (draftControlReceipt && draftControlReceipt->request["id"] == id) return &*draftControlReceipt;
    const auto found = receipts.find(id);
    return found == receipts.end() ? nullptr : &found->second;
}

Json EngineControl::dispatch(const Json& request) {
    const auto id = request["id"].get<std::string>();
    const auto method = request["method"].get<std::string>();
    const auto params = request.value("params", Json::object());
    if (const auto* existing = findReceipt(id)) {
        if (existing->request != request) throw Invalid("id_conflict", "retained ID has different content");
        return existing->response;
    }
#ifdef VOSC_WEBGPU_M1
    if (draftPreview && method == "draft_m1_status") {
        keys(params, {});
        auto response = reply(id, "ok");
        response["result"] = m1Status();
        return response;
    }
    if (draftPreview && method == "draft_m1_switch") {
        keys(params, {"backend"}, {"source"});
        auto backend = text(params["backend"], 6, "backend");
        if (backend == "webgpu") {
            if (!params.contains("source")) throw Invalid("invalid_params", "WebGPU requires WGSL source");
            text(params["source"], 128 * 1024, "source");
        } else if (backend != "opengl" || params.contains("source")) {
            throw Invalid("invalid_params", "Expected webgpu with source or opengl");
        }
        if (trial || draftWebGpuActive() || (draftCandidate && !draftCandidate->wgslPrepareId.empty()) ||
            !m1Switch.empty() || !draftDraw.id.empty() || !queuedDraftCommit.empty())
            throw Invalid("draft_busy", "A preview transaction is pending");
        if (bool(m1Preview) == (backend == "webgpu"))
            throw Invalid("invalid_params", "Requested backend is already active");
        if (pendingCount() >= 16 || !makeReceiptSpace())
            throw Invalid("queue_full", "pending request or receipt capacity reached");
        auto accepted = reply(id, "queued");
        accepted["method"] = method;
        receipts.emplace(id, Receipt{request, accepted, ofGetElapsedTimef(), {}});
        receiptOrder.push_back(id);
        draftWebGpuStaging.reset();
        m1Switch = id;
        return accepted;
    }
#endif
    const bool isQuery = method == "get_capabilities" || method == "get_scene" ||
        method == "get_performance" || method == "get_diagnostics" || method == "get_request" ||
        method == "export_scene";
    if (trial && !isQuery && method != "capture_preview" &&
        method != "accept_trial" && method != "cancel_trial")
        throw Invalid("trial_busy", "a protected shader trial owns the mutation surface");
    auto result = reply(id, "ok");
    if (method == "get_capabilities") {
        keys(params, {});
        result["result"] = capabilities();
    } else if (method == "get_scene") {
        keys(params, {});
        result["result"] = scene();
    } else if (method == "get_performance") {
        keys(params, {});
#ifdef VOSC_WEBGPU_M1
        if (draftWebGpuActive())
            throw Invalid("wgsl_operation_unsupported", "Performance telemetry is unavailable on the WGSL preview");
#endif
        result["result"] = performance();
    } else if (method == "get_diagnostics") {
        keys(params, {}, {"after", "limit"});
        uint64_t after = params.count("after") ? cursor(params["after"], UINT64_MAX, "after") : 0;
        size_t limit = params.count("limit") ? integer(params["limit"], 1, 128, "limit") : 128;
        result["result"] = diagnostics(after, limit);
    } else if (method == "get_request") {
        keys(params, {"request_id"});
        auto which = text(params["request_id"], 64, "request_id");
        const auto* found = findReceipt(which);
        if (!found) throw Invalid("request_not_found", "receipt unknown or evicted");
        result["result"] = found->response;
    } else if (method == "export_scene") {
        keys(params, {});
#ifdef VOSC_WEBGPU_M1
        if (draftWebGpuActive())
            throw Invalid("export_unsupported", "Standalone replay export is unavailable for WGSL drafts");
#endif
        if (trial && trial->feedbackBaseline)
            throw Invalid("trial_busy", "feedback trial must settle before export");
        result["result"] = {{"recipe", exportRecipe(render(), runtime(), commands(),
            ofGetWidth(), ofGetHeight(), commands().onsetGateValue())}};
    } else {
        validate(method, params);
        if (!draftDraw.id.empty() && method != "draft_fence" && method != "capture_preview")
            throw Invalid("draft_busy", "A draft mutation is awaiting its first composed frame");
        bool waits = method == "capture_preview" || params.value("timing", std::string("immediate")) == "onset";
        if (pendingCount() >= 16 || !makeReceiptSpace())
            throw Invalid("queue_full", "pending request or receipt capacity reached");
        auto accepted = reply(id, "queued");
        accepted["method"] = method;
        const auto previewOwner = method == "capture_preview" && trial && trial->feedbackBaseline
            ? trial->id : std::string();
        receipts.emplace(id, Receipt{request, accepted, ofGetElapsedTimef(), previewOwner});
        receiptOrder.push_back(id);
        if (method == "capture_preview") captures.push_back(id);
        else {
            noteControlAdmission();
            if (method == "draft_commit") {
                if (!queuedDraftCommit.empty()) {
                    receipts.at(id).response = failure(id, "draft_busy", "A draft commit is already queued");
                } else queuedDraftCommit = id;
            } else if (waits) queued.push_back(id);
            else execute(id);
        }
        return receipts.at(id).response;
    }
    return result;
}

void EngineControl::validate(const std::string& method, const Json& params) const {
#ifdef VOSC_WEBGPU_M1
    if (draftWebGpuActive() && method != "draft_prepare" && method != "draft_commit" &&
        method != "draft_cancel" && method != "draft_fence")
        throw Invalid("wgsl_operation_unsupported", "This operation is unavailable on the WGSL preview");
#endif
    if (method == "configure_layers") {
        keys(params, {"count", "layout"}, {"timing"});
        integer(params["count"], 0, 32, "count");
        auto layout = text(params["layout"], 16, "layout");
        if (!LayoutMap.count(layout)) throw Invalid("invalid_params", "unknown layout");
    } else if (method == "capture_preview") {
        keys(params, {}, {"max_width", "minimum_revision", "max_readback_pixels"});
        if (params.count("max_width")) integer(params["max_width"], 64, 1024, "max_width");
        if (params.count("max_readback_pixels"))
            integer(params["max_readback_pixels"], 1, 16 * 1024 * 1024, "max_readback_pixels");
        if (params.count("minimum_revision")) cursor(params["minimum_revision"], revision, "minimum_revision");
        return;
    } else if (method == "trial_shader") {
        keys(params, {"layer", "source"}, {"uniforms", "gpu_budget_ms", "ttl_ms", "target"});
        if (params.count("target") && params["target"] != "default_feedback_texture")
            throw Invalid("invalid_target", "unsupported shader trial target");
        integer(params["layer"], 0, 31, "layer");
        text(params["source"], 128 * 1024, "source");
        if (params.count("gpu_budget_ms")) number(params["gpu_budget_ms"], 0.05, 16.0);
        if (params.count("ttl_ms")) integer(params["ttl_ms"], 1000, 30000, "ttl_ms");
        if (params.count("uniforms")) {
            if (!params["uniforms"].is_object() || params["uniforms"].size() > 32)
                throw Invalid("invalid_params", "uniforms must be an object with at most 32 entries");
            for (auto it = params["uniforms"].begin(); it != params["uniforms"].end(); ++it) {
                text(Json(it.key()), 128, "uniform name");
                if (it.value().is_array()) {
                    if (it.value().empty() || it.value().size() > 4)
                        throw Invalid("invalid_params", "uniform vectors require one to four components");
                    for (const auto& value : it.value()) number(value, -1000000, 1000000);
                } else number(it.value(), -1000000, 1000000);
            }
        }
    } else if (method == "accept_trial" || method == "cancel_trial") {
        keys(params, {"trial_id"});
        text(params["trial_id"], 64, "trial_id");
    } else if (method == "draft_prepare" || method == "draft_commit" || method == "draft_cancel" ||
               method == "draft_fence" || method == "draft_sample" || method == "draft_callback" ||
               method == "draft_configure_slot" || method == "draft_bind_uniform") {
        if (!draftPreview) throw Invalid("unknown_method", "draft operations are disabled");
        if (method == "draft_prepare")
            keys(params, {"source_base64", "source_digest", "lease", "token", "revision", "slots", "bindings"}, {"language"});
        else if (method == "draft_commit")
            keys(params, {"lease", "token", "revision", "control_digest"});
        else if (method == "draft_cancel") keys(params, {"lease", "token"});
        else if (method == "draft_fence") keys(params, {"lease"});
        else if (method == "draft_configure_slot") keys(params, {"lease", "revision", "slot", "initial"});
        else if (method == "draft_bind_uniform") keys(params, {"lease", "revision", "layer", "name", "components"});
        else if (method == "draft_callback") keys(params, {"lease", "revision", "sequence", "value"});
        else keys(params, {"lease", "revision", "sequence", "slot", "value"});
    } else if (method == "publish_shader" || method == "set_uniform" || method == "set_layer") {
        if (method == "publish_shader")
            keys(params, {"layer", draftValidation ? "source_base64" : "source"}, {"timing"});
        else if (method == "set_uniform") keys(params, {"layer", "name", "value"}, {"timing"});
        else keys(params, {"layer", "property", "value"}, {"timing"});
        int index = integer(params["layer"], 0, 31, "layer");
        if (static_cast<size_t>(index) >= render().getLayers().size())
            throw Invalid("invalid_target", "layer does not exist");
        const auto& layer = *render().getLayers()[index];
        if (method == "publish_shader") {
            if (draftValidation) text(params["source_base64"], maxEncodedDraftSourceBytes, "source_base64");
            else text(params["source"], maxDraftSourceBytes, "source");
        } else if (method == "set_uniform") {
            auto name = text(params["name"], 128, "name");
            if (draftPreview && name == draftCallbackUniform && draftActivation.contains("callback_uniform"))
                throw Invalid("callback_owned", "The Editor callback owns this uniform");
            const int count = editableUniformComponents(layer, name);
            if (count == 1) number(params["value"], -1000000, 1000000);
            else {
                if (!params["value"].is_array() || params["value"].size() != static_cast<size_t>(count))
                    throw Invalid("invalid_params", "uniform value component count mismatch");
                for (const auto& value : params["value"]) number(value, -1000000, 1000000);
            }
        } else {
            auto property = text(params["property"], 16, "property");
            if (property == "alpha") number(params["value"], 0, 1);
            else if (property != "visible" || !params["value"].is_boolean())
                throw Invalid("invalid_params", "property must be alpha (number) or visible (boolean)");
        }
    } else throw Invalid("unknown_method", "unsupported method");
    if (params.count("timing")) {
        auto timing = text(params["timing"], 16, "timing");
        if (timing != "immediate" && timing != "onset") throw Invalid("invalid_params", "unknown timing");
    }
}

void EngineControl::execute(const std::string& id) {
    auto& entry = receipts.at(id);
    auto method = entry.request["method"].get<std::string>();
    auto params = entry.request.value("params", Json::object());
    uint64_t logStart = logger() ? logger()->latestSequence() : 0;
    try {
        if (!ResourceRegistry::hasCurrent())
            throw ResourceRegistry::Inactive("engine operation requires an active ResourceRegistry");
        // An onset-queued mutation can reach execution after another request
        // has claimed the pending draw during this update.
        if (!draftDraw.id.empty() && method != "draft_fence")
            throw Invalid("draft_busy", "A draft mutation is awaiting its first composed frame");
        validate(method, params); // Targets/types may have changed while this request waited.
        if (executeDraft(id, method, params, logStart)) return;
        if (method == "trial_shader") {
            startTrial(id);
            return;
        } else if (method == "accept_trial" || method == "cancel_trial") {
            if (!trial || params["trial_id"].get<std::string>() != trial->id)
                throw Invalid("trial_not_found", "active trial does not match trial_id");
            evaluateTrial();
            if (!trial) throw Invalid("trial_not_found", "trial ended before action could apply");
            if (trial->phase == "committing" || trial->phase == "rolling_back")
                throw Invalid("trial_busy", "trial is waiting for its matching rendered frame");
            if (method == "accept_trial") {
                telemetry.refreshGpuResults();
                if (trial->feedbackBaseline) updateTelemetry.refreshGpuResults();
                evaluateTrial();
                if (!trial || trial->phase != "ready")
                    throw Invalid("trial_not_ready", "trial has not met fresh GPU admission");
                trial->actionId = id;
                trial->phase = "committing";
                ++revision;
            } else {
                trial->actionId = id;
                rollbackTrial("trial_cancelled");
                // A failed restore interrupts and releases the trial while
                // writing trial_conflict to this action receipt. Do not resume
                // the ordinary applied/rendered path and overwrite that truth.
                if (!trial) return;
            }
            entry.response = reply(id, "applied");
            entry.response["method"] = method;
            entry.response["applied_after_frame"] = frame;
            updateTrialReceipt();
            return;
        } else if (method == "configure_layers") {
            osc::LayersPayload payload;
            payload.hasLayerCount = payload.hasLayout = true;
            payload.layerCount = params["count"].get<int>();
            payload.layout = LayoutMap.at(params["layout"].get<std::string>());
            render().applyLayersPayload(payload);
        } else {
            auto& layer = *render().getLayers()[params["layer"].get<int>()];
            if (method == "publish_shader") {
                const auto source = draftValidation ? decodeDraftSource(params["source_base64"]) :
                    params["source"].get<std::string>();
                if (!layer.shader.loadFromSource(source, draftValidation,
                        draftValidation && render().getLayers().size() == 1)) {
                    auto logs = diagnostics(logStart, 128);
                    entry.response = failure(id, "shader_failed", "shader compile/link failed", logs["entries"]);
                    return;
                }
                if (layer.shader.hasSelfFeedback()) {
                    draftDraw.id = id;
                    draftDraw.expectedProgram = layer.shader.getShader().getProgram();
                }
            } else if (method == "set_uniform") {
                std::vector<float> values;
                const auto& value = params["value"];
                if (value.is_array()) for (const auto& component : value) values.push_back(component.get<float>());
                else values.push_back(value.get<float>());
                setNumericBinding(layer.shader, params["name"].get<std::string>(), values);
            } else if (method == "set_layer") {
                auto property = params["property"].get<std::string>();
                float value = property == "visible" ? float(params["value"].get<bool>()) : params["value"].get<float>();
                setNumericBinding(layer, property, {value});
            }
        }
        discardDraftCandidate();
        ++revision;
        ++feedbackConfigurationEpoch;
        entry.response = reply(id, "applied");
        entry.response["method"] = method;
        entry.response["applied_after_frame"] = frame;
    } catch (const Invalid& error) {
        auto entries = method == "trial_shader" && error.code == "shader_failed"
            ? diagnostics(logStart, 128)["entries"] : Json::array();
        entry.response = failure(id, error.code, error.what(), entries);
        entry.response["method"] = method;
    } catch (const ResourceRegistry::Inactive& error) {
        ofLogError("control") << error.what();
        entry.response = failure(id, "execution_failed", error.what());
        entry.response["method"] = method;
    } catch (...) {
        entry.response = failure(id, "execution_failed", "engine operation failed");
        entry.response["method"] = method;
    }
    if (method == "draft_commit") draftCommitReceipt = entry;
}

void EngineControl::discardDraftCandidate(const char* code) {
#ifdef VOSC_WEBGPU_M1
    if (draftCandidate && !draftCandidate->wgslPrepareId.empty()) {
        if (auto* context = draftWebGpuContext(); context && draftCandidate->wgslStarted)
            context->cancelPreparation();
        auto& entry = receipts.at(draftCandidate->wgslPrepareId);
        if (entry.response["status"] == "queued") {
            entry.response = failure(draftCandidate->wgslPrepareId, code, "The WGSL preparation was cancelled or superseded");
            entry.response["method"] = "draft_prepare";
        }
    }
#else
    (void)code;
#endif
    draftCandidate.reset();
}

bool EngineControl::executeDraft(const std::string& id, const std::string& method,
                                 const Json& params, uint64_t logStart) {
    if (method != "draft_prepare" && method != "draft_commit" && method != "draft_cancel" &&
        method != "draft_fence" && method != "draft_sample" && method != "draft_callback" &&
        method != "draft_configure_slot" && method != "draft_bind_uniform") return false;
    auto& response = receipts.at(id).response;
    const auto lease = text(params["lease"], 64, "lease");
    if (method == "draft_fence") {
        if (fencedDraftLeases.size() >= 4096 && !fencedDraftLeases.count(lease))
            throw Invalid("lease_capacity", "The private renderer has exhausted its lease capacity");
        fencedDraftLeases.insert(lease);
        if (draftCandidate && draftCandidate->lease == lease) discardDraftCandidate("stale_lease");
        response = reply(id, "ok");
        response["result"] = {{"lease", lease}};
        return true;
    }
    if (fencedDraftLeases.count(lease)) throw Invalid("stale_lease", "The draft lease has been fenced");
    if (method == "draft_cancel") {
        const auto token = text(params["token"], 64, "token");
        if (draftCandidate && draftCandidate->lease == lease && draftCandidate->token == token)
            discardDraftCandidate();
        response = reply(id, "ok");
        response["result"] = {{"lease", lease}, {"token", token}};
        return true;
    }
    if (cursor(params["revision"], UINT64_MAX, "revision") != revision)
        throw Invalid("stale_revision", "The native draft configuration changed");
    if (render().getLayers().size() != 1 || !render().getLayers()[0])
        throw Invalid("invalid_target", "Draft transactions require one private preview layer");
    auto& layer = *render().getLayers()[0];
    const auto activeLease = draftActivation.is_object() ? draftActivation.value("lease", std::string()) : std::string();
    const bool callbackActive = draftActivation.contains("callback_uniform");

    if (method == "draft_configure_slot" || method == "draft_bind_uniform") {
        if (activeLease != lease) throw Invalid("stale_lease", "The active draft has a different lease");
        if (!layer.shader.isShaderLoaded()) throw Invalid("invalid_target", "No active draft shader");
        DraftDraw pending;
        pending.id = id;
        pending.expectedProgram = layer.shader.getShader().getProgram();
        if (method == "draft_configure_slot") {
            const auto slot = draftSlot(params["slot"]);
            const auto initial = static_cast<float>(number(params["initial"], 0, 1));
            if (runtime().inputs.all().size() >= 16 || runtime().inputs.all().count(slot))
                throw Invalid("slot_capacity", "draft slot already exists or exceeds capacity");
            pending.expectedSlots.emplace(slot, initial);
            draftConfigureSlot(runtime().inputs, slot, initial);
        } else {
            integer(params["layer"], 0, 0, "layer");
            const auto name = text(params["name"], 128, "name");
            if (callbackActive && name == draftCallbackUniform)
                throw Invalid("callback_owned", "The Editor callback owns this uniform");
            std::set<std::string> slots;
            for (const auto& [which, input] : runtime().inputs.all()) slots.insert(which);
            checkDraftComponents(params["components"], slots, editableUniformComponents(layer, name));
            auto binding = draftBinding(params["components"]);
            pending.expectedBindings.push_back(name);
            VariablePool::getPool(&layer.shader)[name] = binding;
            layer.shader.vars[name] = std::move(binding);
        }
        discardDraftCandidate();
        draftDraw = std::move(pending);
        ++revision;
        response = reply(id, "applied");
        response["method"] = method;
        response["applied_after_frame"] = frame;
        return true;
    }

    if (method == "draft_sample" || method == "draft_callback") {
        const auto sequence = cursor(params["sequence"], 9007199254740991ULL, "sequence");
        const auto amplitude = static_cast<float>(number(params["value"], 0, 1));
        if (activeLease != lease || sequence <= draftSampleSequence)
            throw Invalid("stale_sample", "The sample no longer belongs to the active draft generation");
        DraftDraw pending;
        pending.id = id;
        pending.expectedProgram = layer.shader.getShader().getProgram();
        if (method == "draft_callback") {
            if (!callbackActive || !layer.shader.isShaderLoaded())
                throw Invalid("callback_not_active", "The current draft has no Editor callback");
            pending.expectedBindings.emplace_back(draftCallbackUniform);
            setNumericBinding(layer.shader, draftCallbackUniform, {amplitude});
        } else {
            const auto slot = draftSlot(params["slot"]);
            if (!runtime().inputs.all().count(slot)) throw Invalid("missing_slot", "The draft slot is not configured");
            pending.expectedSlots.emplace(slot, amplitude);
            draftSample(runtime().inputs, slot, amplitude);
        }
        draftDraw = std::move(pending);
        draftSampleSequence = sequence;
        ++externalEpoch;
        response = reply(id, "applied");
        response["method"] = method;
        return true;
    }

    const auto token = text(params["token"], 64, "token");
    if (method == "draft_prepare") {
        const auto language = params.contains("language") ? text(params["language"], 4, "language") : "glsl";
        if (language != "glsl" && language != "wgsl")
            throw Invalid("invalid_params", "Expected glsl or wgsl draft source");
#ifdef VOSC_WEBGPU_M1
        if (language == "glsl" && draftWebGpuActive())
            throw Invalid("backend_switch_required", "Close the WebGPU preview window on the Render Host, then Run GLSL or Hydra again");
        if (language == "wgsl" && (!m1Switch.empty() || (m1Preview && !draftWebGpuActive())))
            throw Invalid("draft_busy", "An experimental WebGPU preview transaction owns this session");
#else
        if (language == "wgsl")
            throw Invalid("wgsl_unavailable", "Install a Dawn-enabled VOSC renderer on this Render Host to run WGSL drafts");
#endif
        discardDraftCandidate();
        auto candidate = std::make_unique<DraftCandidate>();
        candidate->lease = lease;
        candidate->token = token;
        candidate->sourceDigest = draftDigest(params["source_digest"]);
        candidate->revision = revision;
        if (!params["slots"].is_object() || params["slots"].size() > 16 ||
            !params["bindings"].is_object() || params["bindings"].size() > 128)
            throw Invalid("invalid_params", "Draft slot or binding capacity exceeded");
        candidate->slots = params["slots"];
        candidate->bindings = Json::object();
#ifdef VOSC_WEBGPU_M1
        if (language == "wgsl") {
            if (!candidate->slots.empty() || !params["bindings"].empty())
                throw Invalid("wgsl_controls_unsupported", "WGSL drafts currently support only the renderer uniform block");
            candidate->wgslPrepareId = id;
            candidate->expiresAt = monotonicSeconds() + 3.;
            draftCandidate = std::move(candidate);
            return true;
        }
#endif
        const auto source = decodeDraftSource(params["source_base64"]);
        auto conflicts = Json::array();
        std::set<std::string> slots;
        for (const auto& [key, value] : candidate->slots.items()) {
            const auto slot = draftSlot(key);
            const auto initial = static_cast<float>(number(value, 0, 1));
            slots.insert(slot);
            draftConfigureSlot(candidate->inputs, slot, initial);
        }
        for (const auto& [name, binding] : params["bindings"].items()) {
            text(name, 128, "uniform name");
            keys(binding, {"components"}, {"type"});
            checkDraftComponents(binding["components"], slots, 0);
            if (binding.count("type") && !binding["type"].is_null() &&
                text(binding["type"], 8, "uniform type") != draftUniformType(static_cast<int>(binding["components"].size())))
                throw Invalid("invalid_params", "Saved uniform type does not match binding arity");
        }
        if (!Shader::prepareDirectSource(source, candidate->program.source, true, true)) {
            response = failure(id, "shader_failed", "shader compile/link failed", diagnostics(logStart, 128)["entries"]);
            return true;
        }
        const auto& uniforms = candidate->program.source.uniforms;
        const auto callback = uniforms.find(draftCallbackUniform);
        const bool usesCallback = callback != uniforms.end();
        if (usesCallback) {
            if (callback->second.type != GL_FLOAT || callback->second.isArray() ||
                callback->second.location < 0 || managedUniform(layer, draftCallbackUniform))
                throw Invalid("invalid_callback_uniform", "u_vosc_callback must be an active non-array float uniform");
            auto variable = std::make_shared<Variable<float>>();
            variable->set(std::vector<float>{draftCallbackInitial});
            candidate->program.variables[draftCallbackUniform] = variable;
            candidate->program.pool[draftCallbackUniform] = std::move(variable);
        }
        for (const auto& [name, binding] : params["bindings"].items()) {
            const auto found = uniforms.find(name);
            const bool exists = found != uniforms.end();
            const int arity = exists && !found->second.isArray() ? uniformComponents(found->second.type) : 0;
            const auto previousArity = static_cast<int>(binding["components"].size());
            const auto previousType = draftUniformType(previousArity);
            const bool managed = managedUniform(layer, name) || (exists && found->second.graphSampler) ||
                (usesCallback && name == draftCallbackUniform);
            if (!exists || managed || arity != previousArity) {
                Json issue = {{"name", name}, {"previous_type", previousType},
                    {"reason", managed ? "renderer_owned" : !exists ? "inactive" : "retyped"}};
                if (exists) issue["actual_type"] = draftUniformType(arity);
                if (exists && arity && !managed) {
                    std::array<float, 4> values{};
                    glGetUniformfv(found->second.program, found->second.location, values.data());
                    issue["default_values"] = std::vector<float>(values.begin(), values.begin() + arity);
                }
                conflicts.push_back(std::move(issue));
                continue;
            }
            candidate->bindings[name] = {{"type", previousType}, {"components", binding["components"]}};
            auto variable = draftBinding(binding["components"], &candidate->inputs);
            candidate->program.variables[name] = variable;
            candidate->program.pool[name] = std::move(variable);
        }
        candidate->expiresAt = monotonicSeconds() + 30.;
        response = reply(id, "prepared");
        response["result"] = {{"token", token}, {"bindings", candidate->bindings}, {"conflicts", conflicts}};
        draftCandidate = std::move(candidate);
        return true;
    }

    if (!draftDraw.id.empty()) throw Invalid("draft_busy", "A draft mutation awaits its first composed frame");
    if (!draftCandidate || draftCandidate->revision != revision || draftCandidate->lease != lease ||
        draftCandidate->token != token || monotonicSeconds() >= draftCandidate->expiresAt)
        throw Invalid("stale_candidate", "The prepared draft expired or its context changed");
    const auto controlDigest = draftDigest(params["control_digest"]);
#ifdef VOSC_WEBGPU_M1
    if (!draftCandidate->wgslPrepareId.empty()) {
        if (receipts.at(draftCandidate->wgslPrepareId).response["status"] != "prepared")
            throw Invalid("stale_candidate", "WGSL preparation has not completed");
        draftWebGpuCommit = id;
        return true;
    }
#endif
    auto activation = Json{{"lease", lease}, {"token", token}, {"source_digest", draftCandidate->sourceDigest},
        {"control_digest", controlDigest}, {"commit_id", id}};
    const bool usesCallback = draftCandidate->program.variables.count(draftCallbackUniform) != 0;
    if (usesCallback) activation["callback_uniform"] = draftCallbackUniform;
    auto applied = reply(id, "applied");
    applied["method"] = method;
    applied["revision"] = revision + 1;
    applied["applied_after_frame"] = frame;

    // Prepare all readback and ownership state before replacing any part of the live scene.
    DraftDraw pending;
    pending.id = id;
    pending.previousActivation = draftActivation;
    pending.previousRevision = revision;
    pending.previousSampleSequence = draftSampleSequence;
    pending.expectedProgram = draftCandidate->program.source.program->getProgram();
    if (activeLease == lease) {
        for (const auto& [slot, initial] : draftCandidate->slots.items()) {
            const auto input = runtime().inputs.all().find(slot);
            if (input != runtime().inputs.all().end())
                draftSample(draftCandidate->inputs, slot, input->second->get());
        }
    }
    for (const auto& [slot, input] : draftCandidate->inputs.all())
        pending.expectedSlots.emplace(slot, input->get());
    for (const auto& [name, binding] : draftCandidate->bindings.items())
        pending.expectedBindings.push_back(name);
    if (usesCallback) pending.expectedBindings.emplace_back(draftCallbackUniform);
    layer.shader.swapDraftProgram(draftCandidate->program);
    runtime().inputs.swap(draftCandidate->inputs);
    pending.baseline = std::move(draftCandidate);
    draftDraw = std::move(pending);
    draftActivation = std::move(activation);
    if (activeLease != lease) draftSampleSequence = 0;
    ++revision;
    response = std::move(applied);
    return true;
}

bool EngineControl::settleDraftDraw() {
    if (draftDraw.id.empty()) return true;
    auto& entry = receipts.at(draftDraw.id);
    const auto method = entry.request["method"].get<std::string>();
    const int layer = method == "publish_shader" ? entry.request["params"]["layer"].get<int>() : 0;
    auto& shader = render().getLayers()[layer]->shader;
    const auto coherentDraw = [&] {
        if (draftDraw.expectedProgram ? !shader.ownsProgram(draftDraw.expectedProgram) : shader.isShaderLoaded())
            return false;
        if (shader.hasSelfFeedback() && !shader.drewSelfFeedbackThisFrame()) return false;
        for (const auto& [slot, amplitude] : draftDraw.expectedSlots) {
            const auto input = runtime().inputs.all().find(slot);
            if (input == runtime().inputs.all().end() || input->second->get() != amplitude) return false;
        }
        if (!draftDraw.expectedBindings.empty()) {
            const auto uniformValues = shader.getUniformValues(true);
            for (const auto& name : draftDraw.expectedBindings) {
                const auto readback = uniformValues.find(name);
                const auto binding = shader.vars.find(name);
                if (readback == uniformValues.end() || binding == shader.vars.end()) return false;
                const auto variable = std::dynamic_pointer_cast<const Variable<float>>(binding->second);
                if (!variable) return false;
                auto observed = Json::parse(readback->second, nullptr, false);
                if (observed.is_number()) observed = Json::array({observed});
                const auto& composed = variable->getVec();
                if (!observed.is_array() || observed.size() != composed.size() ||
                    !shader.graphOutputBindingMatches(name, composed)) return false;
                for (size_t i = 0; i < composed.size(); ++i) {
                    if (!observed[i].is_number() || observed[i].get<float>() != composed[i]) return false;
                }
            }
        }
        return true;
    };
    bool coherent = coherentDraw();
    if (method == "publish_shader") {
        if (!coherent)
            entry.response = failure(draftDraw.id, "shader_failed", "Self-feedback did not produce a composed frame");
    } else if (draftDraw.baseline) {
#ifdef VOSC_RECOVERY_TESTING
        if (entry.request["params"]["token"] == "test-fail-first-draft-draw") coherent = false;
#endif
        if (!coherent) {
            auto& baseline = *draftDraw.baseline;
            draftDraw.expectedProgram = baseline.program.source.program ? baseline.program.source.program->getProgram() : 0;
            draftDraw.expectedSlots.clear();
            for (const auto& [slot, input] : baseline.inputs.all())
                draftDraw.expectedSlots.emplace(slot, input->get());
            draftDraw.expectedBindings.clear();
            for (const auto& [name, variable] : baseline.program.variables)
                draftDraw.expectedBindings.push_back(name);
            shader.swapDraftProgram(baseline.program);
            runtime().inputs.swap(baseline.inputs);
            draftActivation = std::move(draftDraw.previousActivation);
            draftSampleSequence = draftDraw.previousSampleSequence;
            revision = draftDraw.previousRevision;
            renderedRevision = revision;
            // GLFW has not swapped the back buffer yet. Replace the rejected draw without
            // re-entering command processing, simulation updates, or receipt callbacks.
            render().draw(runtime().camera);
            observeSelfFeedbackDraw();
            coherent = coherentDraw();
            entry.response = failure(draftDraw.id, coherent ? "draft_rolled_back" : "draft_state_lost",
                coherent ? "The previous draft was restored and composed" : "The previous draft could not be observed");
            entry.response["result"] = {{"scene", scene()}, {"restored_previous", coherent}};
        } else {
            entry.response = reply(draftDraw.id, "rendered");
            auto result = draftActivation;
            result["scene"] = scene();
            entry.response["result"] = std::move(result);
        }
    } else if (!coherent) {
        entry.response = failure(draftDraw.id, "draft_state_lost", "The changed draft state could not be observed");
    } else {
        entry.response = reply(draftDraw.id, "rendered");
        entry.response["result"] = {{"lease", draftActivation["lease"]}, {"scene", scene()}};
        if (method == "draft_sample" || method == "draft_callback")
            entry.response["result"]["sequence"] = draftSampleSequence;
    }
    entry.response["method"] = method;
    if (method == "draft_commit") draftCommitReceipt = entry;
    else if (method == "draft_configure_slot" || method == "draft_bind_uniform") draftControlReceipt = entry;
    draftDraw = {};
    return coherent;
}

void EngineControl::observeSelfFeedbackDraw() {
    selfFeedbackDraw = nullptr;
    graphDraw = nullptr;
    const auto& layers = render().getLayers();
    if (layers.size() != 1 || !layers[0]) return;
    const auto& shader = layers[0]->shader;
    if (!shader.drewSelfFeedbackThisFrame()) return;
    const auto program = shader.getShader().getProgram();
    const auto step = shader.selfFeedbackStep();
    const auto outputProgram = shader.graphOutputProgram();
    if (outputProgram) {
        graphDraw = {{"frame", frame}, {"step", step}, {"displayed_output", "o1"},
                     {"passes", Json::array({
                         {{"output", "o0"}, {"program", program}, {"source_step", step - 1}},
                         {{"output", "o1"}, {"program", outputProgram}, {"source_step", step}}
                     })}};
        if (draftPreview && draftActivation.contains("commit_id"))
            graphDraw["commit_id"] = draftActivation["commit_id"];
    } else {
        selfFeedbackDraw = {{"step", step}, {"program", program}, {"frame", frame}};
        if (draftPreview && draftActivation.contains("commit_id"))
            selfFeedbackDraw["commit_id"] = draftActivation["commit_id"];
    }
}

void EngineControl::startTrial(const std::string& id) {
    if (trial) throw Invalid("trial_busy", "a protected shader trial is already active");
    if (!queued.empty() || !captures.empty() || commands().pendingCount() != 0)
        throw Invalid("trial_busy", "queued controls or captures must drain before a trial");
    auto& entry = receipts.at(id);
    const auto params = entry.request.value("params", Json::object());
    const size_t layerIndex = static_cast<size_t>(params["layer"].get<int>());
    if (params.count("target")) {
        if (layerIndex != 0) throw Invalid("invalid_target", "feedback target supports exactly layer 0");
        auto active = std::make_unique<Trial>();
        active->id = id;
        active->layer = layerIndex;
        active->budgetMilliseconds = params.value("gpu_budget_ms", 8.0);
        startFeedbackTrial(*active, params);
        active->deadlineSeconds = monotonicSeconds() + 10.0;
        active->evaluationRevision = ++revision;
        active->firstFrame = frame + 1;
        active->context = performanceContext();
        trial = std::move(active);
        updateTrialReceipt();
        return;
    }
    if (runtime().camera.isEnabled() || !render().supportsDirectShaderTrial())
        throw Invalid("trial_unsupported", "trial requires one layer, camera off, and no post/deferred passes");
    if (layerIndex != 0)
        throw Invalid("invalid_target", "first trial scope supports exactly layer 0");
    auto& shader = render().getLayers()[layerIndex]->shader;
    Shader::DirectSourceState baseline;
    std::string reason;
    if (!shader.snapshotDirectSource(baseline, reason))
        throw Invalid("trial_unsupported", reason);

    PerformanceContext before = performanceContext();
    telemetry.refreshGpuResults();
    if (!telemetry.snapshot(before).gpuSupported)
        throw Invalid("trial_gpu_unavailable", "GPU elapsed timing is unavailable");

    Shader::PreparedDirectSource prepared;
    uint64_t logStart = logger() ? logger()->latestSequence() : 0;
    if (!shader.prepareDirectSource(params["source"].get<std::string>(), prepared)) {
        auto logs = diagnostics(logStart, 128);
        throw Invalid("shader_failed", logs["entries"].empty()
            ? "shader compile/link failed" : "shader compile/link failed; inspect diagnostics");
    }

    auto bindings = baseline.bindings;
    const auto overrides = params.value("uniforms", Json::object());
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        if (it.key().find('[') != std::string::npos || managedUniform(*render().getLayers()[layerIndex], it.key()))
            throw Invalid("invalid_uniform", "uniform is engine-managed or an array");
        auto found = prepared.uniforms.find(it.key());
        const int components = found == prepared.uniforms.end() || found->second.isArray()
            ? 0 : uniformComponents(found->second.type);
        if (components == 0) throw Invalid("invalid_uniform", "uniform is absent or has an unsupported type");
        std::vector<float> values;
        if (it.value().is_array()) for (const auto& value : it.value()) values.push_back(value.get<float>());
        else values.push_back(it.value().get<float>());
        if (values.size() != static_cast<size_t>(components))
            throw Invalid("invalid_params", "uniform value component count mismatch");
        bindings[it.key()] = std::move(values);
    }

    shader.installDirectSource(std::move(prepared), bindings);
    ++revision;
    auto active = std::make_unique<Trial>();
    active->id = id;
    active->method = "trial_shader";
    active->layer = layerIndex;
    active->budgetMilliseconds = params.value("gpu_budget_ms", 8.0);
    active->deadlineSeconds = monotonicSeconds() + params.value("ttl_ms", 10000) / 1000.0;
    active->evaluationRevision = revision;
    active->firstFrame = frame + 1;
    active->candidateProgram = shader.getShader().getProgram();
    active->baseline = std::move(baseline);
    active->context = performanceContext();
    trial = std::move(active);
    updateTrialReceipt();
}

void EngineControl::startFeedbackTrial(Trial& active, const Json& params) {
    if (commands().replayActive() || pendingCount() != 1)
        throw Invalid("trial_busy", "pending controls and correlated replay must settle before a feedback trial");
    if (!params.value("uniforms", Json::object()).empty() || params.value("ttl_ms", 10000) != 10000)
        throw Invalid("trial_unsupported", "feedback trials require unchanged bindings and a ten-second lease");
    const auto& layers = render().getLayers();
    if (layers.size() != 2 || !layers[0] || !layers[1] || render().getLayout() != Layout::STACK ||
        ofGetWidth() != 800 || ofGetHeight() != 600 || runtime().camera.isEnabled() ||
        !render().supportsScenePersistence())
        throw Invalid("trial_unsupported", "feedback trial requires the bounded 800x600 two-layer stack");
    auto& layer = *layers[0];
    auto& overlay = *layers[1];
    const auto& textures = layer.shader.getTextures();
    const auto alphaBinding = overlay.vars.find("alpha");
    const auto alpha = alphaBinding == overlay.vars.end() ? nullptr
        : std::dynamic_pointer_cast<Variable<float>>(alphaBinding->second);
    if (!alpha || alpha->isConst() || alpha->size() != 1 || overlay.data.blendMode != OF_BLENDMODE_ALPHA ||
        layer.shader.isLoaded() || textures.size() != 1 || !textures.count(DEFAULT_TEX) ||
        !layer.getVarBool("visible") || !overlay.getVarBool("visible") ||
        !overlay.shader.isShaderLoaded() || !overlay.shader.getTextures().empty())
        throw Invalid("trial_unsupported", "feedback trial requires a visible default texture and shader overlay");

    const auto& holder = textures.at(DEFAULT_TEX);
    auto baseline = holder ? std::dynamic_pointer_cast<ShaderPingPongTex>(holder->tex) : nullptr;
    std::string reason;
    if (!baseline || !baseline->replayBounded || !baseline->firstUpdateComplete() ||
        holder->isStatic || holder->feedbackSetupPending || holder->numFrames != 1 ||
        holder->hasLooper() || !holder->passes.empty() || holder->data.getSize() != glm::vec2(800, 600) ||
        !baseline->validateConfiguration(holder->data, reason) || !baseline->storageMatches(holder->data))
        throw Invalid("trial_unsupported", reason.empty() ? "feedback holder is outside bounded trial scope" : reason);

    // Apply the same resource, expression and layer eligibility as editable
    // replay, including rejecting shared or aliased holders.
    exportRecipe(render(), runtime(), commands(), ofGetWidth(), ofGetHeight(), commands().onsetGateValue());
    if (!feedbackTimersSupported())
        throw Invalid("trial_gpu_unavailable", "paired GPU timing is unavailable");

    std::string code;
    auto candidate = baseline->prepareTrial(holder->data, params.at("source"), code, reason);
    if (!candidate) throw Invalid(code, reason);
    if (!baseline->pauseTrialWatch())
        throw Invalid("trial_conflict", "watched feedback source changed before trial activation");
    active.feedbackHolder = holder;
    active.feedbackBaseline = baseline;
    active.feedbackCandidate = candidate;
    active.baselineProgram = baseline->getShader().getProgram();
    active.candidateProgram = candidate->getShader().getProgram();

}

ShaderPingPongTex& EngineControl::expectedFeedback() const {
    return *(trial->phase == "rolling_back" || !trial->feedbackActivated
        ? trial->feedbackBaseline : trial->feedbackCandidate);
}

std::shared_ptr<Texture> EngineControl::ownedFeedbackHolder() const {
    const auto holder = trial->feedbackHolder.lock();
    const auto& layers = render().getLayers();
    if (!holder || trial->layer >= layers.size() || !layers[trial->layer]) return {};
    const auto& textures = layers[trial->layer]->shader.getTextures();
    const auto target = textures.find(DEFAULT_TEX);
    if (target == textures.end() || target->second != holder || holder->tex.get() != &expectedFeedback()) return {};
    const auto program = (&expectedFeedback() == trial->feedbackBaseline.get())
        ? trial->baselineProgram : trial->candidateProgram;
    return expectedFeedback().ownsProgram(static_cast<GLuint>(program)) ? holder : nullptr;
}

bool EngineControl::checkFeedbackOwnership() {
    if (ownedFeedbackHolder() && !trial->feedbackBaseline->trialWatchChanged()) return true;
    interruptTrial();
    return false;
}

void EngineControl::beforeLegacyControl(bool inputSample) {
    if (inputSample && trial && trial->feedbackBaseline) return;
    interruptTrial();
}

void EngineControl::settleFeedbackDraw() {
    if (!checkFeedbackOwnership() || trial->updatedFrame != frame || !expectedFeedback().drewCurrentUpdate()) return;
    const bool restored = trial->phase == "rolling_back";
    if (!restored) {
        trial->candidateDrawFrame = frame;
        feedbackPreviewDraw = {{"trial_id", trial->id}, {"evaluation_revision", trial->evaluationRevision},
            {"frame", frame}, {"candidate_program", trial->candidateProgram}};
    }
    const bool accepted = trial->phase == "committing";
#ifdef VOSC_RECOVERY_TESTING
    recoveryTestCaptureFeedback(restored ? "restored" : accepted ? "accepted" : "candidate", expectedFeedback());
#endif
    if (!accepted && !restored) {
        updateTrialReceipt();
        return;
    }
    auto terminal = restored
        ? failure(trial->id, trial->rollbackCode, "protected feedback trial rolled back")
        : reply(trial->id, "rendered");
    terminal["method"] = trial->method;
    auto metadata = trialSummary();
    metadata["phase"] = restored ? "rolled_back" : "accepted";
    metadata["rendered_frame"] = frame;
    if (restored) {
        metadata["restored_previous"] = true;
        metadata["restoration_revision"] = trial->restorationRevision;
        trial->feedbackBaseline->resumeTrialWatch();
    } else {
        expectedFeedback().trialCandidate = false;
        terminal["applied_after_frame"] = trial->firstFrame - 1;
    }
    terminal["result"] = {{"trial", metadata}};
    receipts.at(trial->id).response = std::move(terminal);
    if (!trial->actionId.empty()) {
        auto& action = receipts.at(trial->actionId);
        if (restored && action.request.at("method") == "accept_trial") {
            action.response = failure(trial->actionId, trial->rollbackCode, "feedback admission failed before draw");
            action.response["method"] = "accept_trial";
        } else {
            action.response["status"] = "rendered";
            action.response["frame"] = frame;
            action.response["external_epoch"] = externalEpoch;
            action.response["result"] = {{"feedback_draw", {{"trial_id", trial->id},
                {"evaluation_revision", trial->evaluationRevision}, {"frame", frame},
                {"program", expectedFeedback().getShader().getProgram()}}}};
        }
    }
    trial.reset();
}

void EngineControl::updateTrialReceipt() {
    if (!trial) return;
    auto found = receipts.find(trial->id);
    if (found == receipts.end()) return;
    auto& response = found->second.response;
    response = reply(trial->id, trial->phase);
    response["method"] = trial->method;
    response["result"] = {{"trial", trialSummary()}};
    response["applied_after_frame"] = trial->firstFrame - 1;
}

Json EngineControl::trialSummary() const {
    if (!trial) return Json();
    const double remaining = std::max(0.0, trial->deadlineSeconds - monotonicSeconds());
    Json summary = {{"trial_id", trial->id}, {"layer", trial->layer}, {"phase", trial->phase},
        {"gpu_budget_ms", trial->budgetMilliseconds},
        {"lease_remaining_ms", static_cast<uint64_t>(remaining * 1000.0)},
        {"evaluation_revision", trial->evaluationRevision}, {"first_frame", trial->firstFrame},
        {"warmup_frames", 8}, {"minimum_samples", 30}, {"freshness_frames", 8},
        {"context", {{"revision", trial->context.revision},
            {"external_epoch", trial->context.externalEpoch}, {"width", trial->context.width},
            {"height", trial->context.height}}},
        {"samples", trial->metrics.samples.samples},
        {"p95_ms", trial->metrics.samples.p95 ? Json(*trial->metrics.samples.p95) : Json()},
        {"hard_max_ms", trial->metrics.lifetimeMaximum ? Json(*trial->metrics.lifetimeMaximum) : Json()},
        {"last_gpu_frame", trial->metrics.lastFrame ? Json(*trial->metrics.lastFrame) : Json()}};
    if (trial->feedbackBaseline) {
        summary["target"] = "default_feedback_texture";
        summary["candidate_program"] = trial->candidateProgram;
        summary["measurement_id"] = trial->measurementId;
        summary["pair_coverage"] = trial->measurementId ? trial->metrics.coverage : 0.0;
        if (trial->candidateDrawFrame) summary["candidate_draw_frame"] = trial->candidateDrawFrame;
    }
    if (!trial->rollbackCode.empty()) summary["rollback_code"] = trial->rollbackCode;
    return summary;
}

void EngineControl::rollbackTrial(const std::string& code) {
    if (!trial || trial->phase == "rolling_back") return;
    if (trial->feedbackBaseline) {
        auto holder = ownedFeedbackHolder();
        if (!holder || !trial->feedbackBaseline->ownsProgram(static_cast<GLuint>(trial->baselineProgram))) {
            interruptTrial();
            return;
        }
        holder->tex = trial->feedbackBaseline;
        ShaderPingPongTex::invalidateFeedbackGeneration();
        trial->updatedFrame = 0;
        trial->rollbackCode = code;
        trial->phase = "rolling_back";
        trial->restorationRevision = ++revision;
        trial->rollbackDeadlineSeconds = monotonicSeconds() + 2.0;
        updateTrialReceipt();
        return;
    }
    if (trial->layer >= render().getLayers().size() || !render().getLayers()[trial->layer]) {
        interruptTrial();
        return;
    }
    auto& shader = render().getLayers()[trial->layer]->shader;
    if (!shader.ownsProgram(static_cast<GLuint>(trial->candidateProgram))) {
        interruptTrial();
        return;
    }
    const bool restored = shader.restoreDirectSource(trial->baseline);
    if (!restored) {
        interruptTrial();
        return;
    }
    trial->rollbackCode = code;
    trial->phase = "rolling_back";
    trial->rollbackDeadlineSeconds = monotonicSeconds() + 2.0;
    ++revision;
    updateTrialReceipt();
}

void EngineControl::evaluateTrial() {
    if (!trial) return;
    if (trial->feedbackBaseline) {
        if (!checkFeedbackOwnership()) return;
        if (trial->phase == "rolling_back") {
            if (monotonicSeconds() >= trial->rollbackDeadlineSeconds) interruptTrial();
            return;
        }
        if (monotonicSeconds() >= trial->deadlineSeconds) {
            rollbackTrial("trial_expired");
            return;
        }
        const auto current = performanceContext();
        const bool matches = measurementContext == feedbackContext(current) && !measurementStale &&
            trial->measurementId == measurementId;
        if (!trial->measurementId) return; // The first update has not started yet.
        trial->metrics = feedbackMeasurement.admission(trial->firstFrame + 8,
            feedbackTimersSupported(), matches);
        const auto decision = evaluateShaderTrial(trial->metrics, trial->budgetMilliseconds,
            monotonicSeconds(), trial->deadlineSeconds, frame);
        if (decision.decision == ShaderTrialDecision::Rollback) {
            rollbackTrial(decision.errorCode);
        } else if (trial->phase == "committing") {
            if (decision.decision != ShaderTrialDecision::Ready) rollbackTrial("trial_not_ready");
        } else {
            trial->phase = decision.decision == ShaderTrialDecision::Ready && trial->candidateDrawFrame
                ? "ready" : "evaluating";
            updateTrialReceipt();
        }
        return;
    }
    if (trial->phase == "committing") return;
    if (trial->phase == "rolling_back") return;
    if (trial->layer >= render().getLayers().size() || !render().getLayers()[trial->layer]) {
        interruptTrial();
        return;
    }
    auto& shader = render().getLayers()[trial->layer]->shader;
    if (!shader.ownsProgram(static_cast<GLuint>(trial->candidateProgram))) {
        interruptTrial();
        return;
    }
    const double now = monotonicSeconds();
    if (now >= trial->deadlineSeconds) {
        rollbackTrial("trial_expired");
        return;
    }
    if (!telemetry.snapshot(trial->context).gpuSupported) {
        rollbackTrial("trial_gpu_unavailable");
        return;
    }
    // The candidate is installed during update, before its first beforeDraw.
    // An accept from another already-connected client in that same update is
    // merely not ready; the still-baseline telemetry context is not a loss.
    if (!hasShaderTrialCandidateFrame(frame, trial->firstFrame)) {
        trial->phase = "evaluating";
        updateTrialReceipt();
        return;
    }
    trial->metrics = telemetry.filteredGpuSnapshot(trial->context, trial->firstFrame, 8);
    const auto decision = evaluateShaderTrial(trial->metrics, trial->budgetMilliseconds,
        now, trial->deadlineSeconds, frame);
    if (decision.decision == ShaderTrialDecision::Rollback) {
        rollbackTrial(decision.errorCode);
        return;
    }
    trial->phase = decision.decision == ShaderTrialDecision::Ready ? "ready" : "evaluating";
    updateTrialReceipt();
}

void EngineControl::interruptTrial() {
    if (!trial) return;
    bool restored = false;
    if (trial->feedbackBaseline) {
        if (auto holder = ownedFeedbackHolder()) {
            restored = trial->feedbackBaseline->ownsProgram(static_cast<GLuint>(trial->baselineProgram));
            if (restored && holder->tex != trial->feedbackBaseline) {
                holder->tex = trial->feedbackBaseline;
                ShaderPingPongTex::invalidateFeedbackGeneration();
                ++revision;
            }
        }
        trial->feedbackBaseline->resumeTrialWatch();
    } else if (trial->layer < render().getLayers().size() && render().getLayers()[trial->layer]) {
        auto& shader = render().getLayers()[trial->layer]->shader;
        if (shader.ownsProgram(static_cast<GLuint>(trial->candidateProgram))) {
            restored = shader.restoreDirectSource(trial->baseline);
            if (restored) ++revision;
        } else restored = shader.ownsProgram(trial->baseline.program->getProgram());
    }
    auto interrupted = failure(trial->id, "trial_conflict", "competing control interrupted shader trial");
    interrupted["method"] = trial->method;
    auto metadata = trialSummary();
    metadata["phase"] = "interrupted";
    metadata["restored_previous"] = restored;
    interrupted["result"] = {{"trial", metadata}};
    receipts.at(trial->id).response = interrupted;
    if (!trial->actionId.empty() && receipts.count(trial->actionId)) {
        auto action = failure(trial->actionId, "trial_conflict", "trial action was superseded before draw");
        action["method"] = receipts.at(trial->actionId).request["method"];
        receipts.at(trial->actionId).response = action;
    }
    trial.reset();
}

#ifdef VOSC_RECOVERY_TESTING
Json EngineControl::recoveryTestSnapshot() const {
    Json holderSizes = Json::object();
    size_t holderVariables = 0;
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry) {
        for (const auto& holder : registry->variableHolderPool) {
            holderSizes[std::to_string(holder.first)] = holder.second.size();
            holderVariables += holder.second.size();
        }
    }
    size_t layerBindings = 0, shaderBindings = 0, shaderTextures = 0, shaderBuffers = 0;
    size_t loadedGeometry = 0;
    Json programs = Json::array();
    for (const auto& layer : render().getLayers()) {
        if (!layer) continue;
        layerBindings += layer->vars.size();
        shaderBindings += layer->shader.vars.size();
        shaderTextures += layer->shader.getTextures().size();
        shaderBuffers += layer->shader.getBuffers().size();
        loadedGeometry += layer->geom && layer->geom->isLoaded() ? 1 : 0;
        const unsigned program = layer->shader.isShaderLoaded() ? layer->shader.getShader().getProgram() : 0;
        if (program) programs.push_back({{"id", program}, {"gl_is_program", bool(glIsProgram(program))}});
    }
    const auto framebuffer = nativeFramebufferSize();
    Json managedCamera = Json::object();
    const auto cameraVariables = runtime().camera.recoveryTestManagedVariables();
    Json cameraValues = Json::object();
    const char* cameraNames[] = {"camPos", "camLook", "camOrbit"};
    for (size_t index = 0; index < cameraVariables.size(); ++index) {
        const BaseVar* registered = nullptr;
        if (registry) {
            const auto found = registry->variableSharedPool.find(cameraNames[index]);
            if (found != registry->variableSharedPool.end()) registered = found->second.get();
        }
        managedCamera[cameraNames[index]] = {
            {"camera_identity", static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cameraVariables[index]))},
            {"registry_identity", static_cast<uint64_t>(reinterpret_cast<uintptr_t>(registered))},
            {"matches", registered && registered == cameraVariables[index]}};
        if (!registered || registered->size() == 0) continue;
        if (const auto* vector = dynamic_cast<const Variable<glm::vec3>*>(registered)) {
            const auto value = vector->get();
            cameraValues[cameraNames[index]] = {value.x, value.y, value.z};
        } else if (const auto* scalar = dynamic_cast<const Variable<float>*>(registered)) {
            cameraValues[cameraNames[index]] = scalar->get();
        }
    }
    Json inputSamples = Json::object();
    for (const auto& [name, input] : runtime().inputs.all()) {
        if (!input) continue;
        inputSamples[name] = {{"amplitude", input->amplitude}, {"scaled", input->ampScaled},
            {"loudness", input->loudness}, {"onset", input->onset}, {"mfcc", input->mfcc}};
    }
    return {{"phase", trial ? trial->phase : "none"}, {"trial_id", trial ? trial->id : ""},
            {"managed_camera", managedCamera}, {"inputs", runtime().inputs.all().size()},
            {"input_samples", inputSamples},
            {"camera_values", cameraValues}, {"render_pipeline", render().recoveryTestPipeline()},
            {"render_targets", render().recoveryTestTargets()},
            {"lights", Lights::get().all().size()}, {"tidal_notes", runtime().tidal->notes.size()},
            {"legacy_pending", commands().pendingCount()},
            {"frame", frame}, {"width", ofGetWidth()}, {"height", ofGetHeight()},
            {"framebuffer_width", framebuffer.first}, {"framebuffer_height", framebuffer.second},
            {"revision", revision}, {"external_epoch", externalEpoch},
            {"resources", {{"layers", render().getLayers().size()},
                {"variable_holder_pools", registry ? registry->variableHolderPool.size() : 0},
                {"variable_holder_variables", holderVariables}, {"holder_sizes", holderSizes},
                {"variable_shared", registry ? registry->variableSharedPool.size() : 0},
                {"texture_shared", registry ? registry->textureSharedPool.size() : 0},
                {"texture_shader", registry ? registry->textureShaderPool.size() : 0},
                {"geometry_shared", registry ? registry->geomSharedPool.size() : 0},
                {"geometry_layer", registry ? registry->geomLayerPool.size() : 0},
                {"shader_shared", registry ? registry->shaderSharedPool.size() : 0},
                {"layer_bindings", layerBindings}, {"shader_bindings", shaderBindings},
                {"shader_textures", shaderTextures}, {"shader_buffers", shaderBuffers},
                {"loaded_geometry", loadedGeometry}, {"programs", programs}}}};
}

Json EngineControl::recoveryTestInspectPrograms(const std::vector<unsigned>& programs) const {
    Json inspected = Json::array();
    for (const auto program : programs) {
        inspected.push_back({{"id", program}, {"gl_is_program", bool(glIsProgram(program))}});
    }
    return {{"source", "glIsProgram"}, {"programs", inspected}, {"snapshot", recoveryTestSnapshot()}};
}

#endif

void EngineControl::update() {
    if (!server.isRunning()) return;
#ifdef VOSC_WEBGPU_M1
    if (m1OpenGlStaged) completeM1OpenGlHandoff();
#endif
    telemetry.refreshGpuResults();
    updateTelemetry.refreshGpuResults();
    evaluateTrial();
    if (draftCandidate && monotonicSeconds() >= draftCandidate->expiresAt) {
#ifdef VOSC_WEBGPU_M1
        if (!draftCandidate->wgslPrepareId.empty()) {
            const auto& id = draftCandidate->wgslPrepareId;
            auto& entry = receipts.at(id);
            if (entry.response["status"] == "queued")
                entry.response = failure(id, "wgsl_prepare_timeout", "Dawn did not finish preparing the WGSL draft");
        }
#endif
        discardDraftCandidate();
    }
    server.update([this](const std::string& line) { return handle(line); }, ofGetElapsedTimef());
#ifdef VOSC_WEBGPU_M1
    pollDraftWebGpuPrepare();
#endif
    if (!queuedDraftCommit.empty()) {
        const auto commit = std::exchange(queuedDraftCommit, {});
        execute(commit);
    }
    for (auto it = queued.begin(); it != queued.end();) {
        auto id = *it;
        if (ofGetElapsedTimef() - receipts.at(id).acceptedAt > 30) {
            receipts.at(id).response = failure(id, "queue_timeout", "onset wait exceeded 30 seconds");
            it = queued.erase(it);
        } else if (commands().didOpenGateThisUpdate()) {
            execute(id);
            it = queued.erase(it);
        } else ++it;
    }
    for (auto it = captures.begin(); it != captures.end();) {
        if (ofGetElapsedTimef() - receipts.at(*it).acceptedAt > 30) {
            receipts.at(*it).response = failure(*it, "queue_timeout", "capture wait exceeded 30 seconds");
            it = captures.erase(it);
        } else ++it;
    }
}

Json EngineControl::diagnostics(uint64_t after, size_t limit) const {
    Json entries = Json::array();
    bool gap = false;
    uint64_t latest = 0, next = after;
    auto channel = logger();
    if (channel) {
        for (const auto& line : channel->entriesSince(after, limit, gap, &latest)) {
            entries.push_back({{"sequence", line.sequence}, {"time", line.time},
                               {"level", int(line.level)}, {"module", line.module}, {"message", line.message}});
            next = line.sequence;
        }
    }
    return {{"entries", entries}, {"next_cursor", next}, {"latest_cursor", latest}, {"gap", gap}};
}

Json EngineControl::capabilities() const {
    Json result = {{"version", 1}, {"transport", "unix-ndjson"},
            {"operations", {"get_capabilities", "get_scene", "get_performance", "get_diagnostics", "get_request",
                "export_scene",
                "configure_layers", "publish_shader", "set_uniform", "set_layer", "capture_preview",
                "trial_shader", "accept_trial", "cancel_trial"}},
            {"features", {{"texture_arrays", bool(ALLOW_TEX_2D_ARRAY)}, {"html", bool(USE_OFX_ULTRALIGHT)},
                           {"hpv", bool(USE_OFX_HPVPLAYER)}, {"safe_explicit_shader_replacement", true},
                           {"protected_shader_trials", true}, {"protected_feedback_shader_trials", true}, {"scene_recipe_export", true}, {"recipe_feedback_version", 1}}},
            {"limits", {{"clients", 8}, {"request_bytes", 262144}, {"response_bytes", 8388608},
                        {"source_bytes", 131072}, {"layers", 32}, {"pending", 16}, {"receipts", 64},
                        {"preview_images", 2}, {"preview_width", 1024}, {"diagnostics", 512},
                        {"preview_height", 1024}, {"readback_pixels", 16 * 1024 * 1024},
                        {"json_depth", 64}, {"io_chunk_bytes", 65536}, {"requests_per_client_update", 1},
                        {"client_idle_seconds", 10}, {"diagnostic_message_bytes", 2048},
                        {"diagnostic_page", 128}, {"queue_timeout_seconds", 30},
                        {"trial_uniforms", 32}, {"trial_baseline_bindings", 64},
                        {"recipe_commands", 2048}, {"recipe_assets", 64}, {"recipe_components", 1024},
                        {"recipe_fragment_bytes", 131072}, {"recipe_fragments_bytes", 1048576},
                        {"recipe_images_bytes", 4194304}, {"recipe_bundle_bytes", 61440},
                        {"trial_ttl_ms_min", 1000}, {"trial_ttl_ms_max", 30000}}},
            {"revision_scope", "successful API controls; external_epoch counts legacy dispatch/input; animation and file-watch reloads are not versioned"}};
    if (draftPreview) {
#ifdef VOSC_WEBGPU_M1
        result["features"]["draft_wgsl"] = true;
        result["operations"].push_back("draft_m1_status");
        result["operations"].push_back("draft_m1_switch");
#endif
        for (auto method : {"draft_prepare", "draft_commit", "draft_cancel", "draft_fence", "draft_sample",
                            "draft_callback", "draft_configure_slot", "draft_bind_uniform"})
            result["operations"].push_back(method);
    }
    return result;
}

namespace {
Json performanceStats(const PerformanceStats& stats) {
    auto value = [](const std::optional<double>& number) -> Json {
        return number ? Json(*number) : Json();
    };
    return {{"samples", stats.samples}, {"mean", value(stats.mean)}, {"p50", value(stats.p50)},
            {"p95", value(stats.p95)}, {"max", value(stats.maximum)}};
}
} // namespace

PerformanceContext EngineControl::performanceContext() const {
    return {revision, externalEpoch, static_cast<unsigned>(std::max(0, ofGetWidth())),
            static_cast<unsigned>(std::max(0, ofGetHeight())), ShaderPingPongTex::feedbackGeneration()};
}

EngineControl::FeedbackContext EngineControl::feedbackContext(const PerformanceContext& current) const {
    const auto& layers = render().getLayers();
    if (layers.empty() || !layers[0]) return {};
    const auto& textures = layers[0]->shader.getTextures();
    const auto target = textures.find(DEFAULT_TEX);
    if (target == textures.end() || !target->second) return {};
    auto* feedback = dynamic_cast<ShaderPingPongTex*>(target->second->tex.get());
    if (!feedback || !feedback->replayBounded || !feedback->isShaderLoaded()) return {};
    return {feedbackConfigurationEpoch, current.feedbackGeneration,
        trial && trial->feedbackBaseline ? trial->evaluationRevision : 0,
        current.width, current.height, feedback->getShader().getProgram()};
}

bool EngineControl::feedbackTimersSupported() const {
#ifdef VOSC_RECOVERY_TESTING
    if (recoveryTestFeedbackTimerLost) return false;
#endif
    return telemetry.gpuSupported() && updateTelemetry.gpuSupported();
}

Json EngineControl::feedbackPerformance(const PerformanceContext& current) const {
    const auto context = feedbackContext(current);
    const bool matches = context == measurementContext && !measurementStale && context.program != 0;
    const bool supported = feedbackTimersSupported();
    Json raw = Json::array(), attempts = Json::array();
    if (matches && supported) {
        for (const auto& item : feedbackMeasurement.pairs()) {
            raw.push_back({{"frame", item.first}, {"update_ms", item.second.update},
                {"draw_ms", item.second.draw}, {"total_ms", item.second.update + item.second.draw}});
        }
        for (const auto& item : feedbackMeasurement.window()) {
            attempts.push_back({{"frame", item.frame}, {"interval_ms", item.interval ? Json(*item.interval) : Json()},
                {"update_submitted", item.update}, {"draw_submitted", item.draw}});
        }
    }
    return {{"id", measurementId}, {"matches_current_context", matches}, {"supported", supported},
        {"configuration", measurementContext.configuration}, {"generation", measurementContext.generation},
        {"program", measurementContext.program}, {"owner_revision", measurementContext.owner},
        {"width", measurementContext.width}, {"height", measurementContext.height},
        {"attempts", feedbackMeasurement.attempts}, {"update_drops", feedbackMeasurement.updateDrops},
        {"draw_drops", feedbackMeasurement.drawDrops}, {"crossing_frames", crossingFrames},
        {"last_crossing_frame", lastCrossingFrame},
        {"aged_pending", feedbackMeasurement.agedPending}, {"samples", raw}, {"attempt_window", attempts}};
}

Json EngineControl::performance() const {
    PerformanceContext current = performanceContext();
    const auto data = telemetry.snapshot(current);
    Json context = data.hasContext
        ? Json{{"revision", data.context.revision}, {"external_epoch", data.context.externalEpoch},
               {"width", data.context.width}, {"height", data.context.height}}
        : Json{{"revision", current.revision}, {"external_epoch", current.externalEpoch},
               {"width", current.width}, {"height", current.height}};
    const double target = ofGetTargetFrameRate();
    Json targetFps = std::isfinite(target) && target >= 0 ? Json(target) : Json();
    auto gpu = performanceStats(data.gpu);
    gpu.update({{"supported", data.gpuSupported}, {"state", data.gpuState},
                {"reason", data.gpuReason ? Json(*data.gpuReason) : Json()},
                {"last_frame", data.gpuLastFrame ? Json(*data.gpuLastFrame) : Json()},
                {"pending_queries", data.pendingQueries}, {"dropped_samples", data.droppedSamples}});
    const auto updates = updateTelemetry.snapshot(current);
    const bool matches = data.matchesCurrentContext && updates.matchesCurrentContext;
    Json pairs = Json::array();
    PerformanceSampleWindow paired;
    if (matches) {
        auto draw = data.rawSamples.begin();
        for (const auto& update : updates.rawSamples) {
            while (draw != data.rawSamples.end() && draw->first < update.first) ++draw;
            if (draw != data.rawSamples.end() && draw->first == update.first) {
                pairs.push_back({{"frame", update.first}, {"update_ms", update.second}, {"draw_ms", draw->second}});
                paired.add(update.first, update.second + draw->second);
            }
        }
    }
    const bool supported = data.gpuSupported && updates.gpuSupported;
    Json feedback{{"supported", supported}, {"state", !supported ? "unsupported" : !matches ? "stale" : pairs.empty() ? "warming_up" : "available"},
                  {"reason", !supported ? Json(updates.gpuReason.value_or(data.gpuReason.value_or("timer unavailable"))) : !matches ? Json("configuration changed between update and draw") : Json()},
                  {"feedback_generation", current.feedbackGeneration}, {"matches_current_context", matches},
                  {"update_gpu_ms", performanceStats(updates.gpu)}, {"paired_gpu_ms", performanceStats(paired.stats())},
                  {"samples", pairs}, {"last_attempt_frame", updates.lastAttemptFrame}, {"attempts", updates.attempts}, {"draw_attempts", data.attempts},
                  {"pending_queries", updates.pendingQueries}, {"draw_pending_queries", data.pendingQueries},
                  {"dropped_samples", updates.droppedSamples}, {"draw_dropped_samples", data.droppedSamples}};
    return {{"version", 1}, {"scope", "composed_scene"}, {"window_frames", 120},
            {"context", context}, {"matches_current_context", data.matchesCurrentContext},
            {"target_fps", targetFps}, {"frame_interval_ms", performanceStats(data.frameInterval)},
            {"cpu_submit_ms", performanceStats(data.cpuSubmit)}, {"gpu_ms", gpu}, {"feedback", feedback}, {"feedback_measurement", feedbackPerformance(current)}};
}

Json EngineControl::scene() const {
#ifdef VOSC_WEBGPU_M1
    const bool wgsl = draftWebGpuActive();
#else
    constexpr bool wgsl = false;
#endif
    size_t feedbackCount = 0;
    if (!wgsl) for (const auto& layer : render().getLayers()) for (const auto& binding : layer->shader.getTextures())
        if (binding.second && dynamic_cast<ShaderPingPongTex*>(binding.second->tex.get())) ++feedbackCount;
    Json layers = Json::array();
    for (const auto& layer : render().getLayers()) {
        if (wgsl) break;
        const auto& shader = layer->shader;
        Json uniforms = Json::object(), textures = Json::object();
        auto gpuValues = shader.getUniformValues(draftPreview);
        for (const auto& [name, uniform] : shader.getUniforms()) {
            const bool array = uniform.isArray();
            const bool callbackOwned = draftPreview && name == draftCallbackUniform &&
                draftActivation.contains("callback_uniform");
            Json value = {{"gl_type", uniform.type}, {"array", array},
                {"writable", uniformComponents(uniform.type) > 0 && !managedUniform(*layer, name) &&
                    !array && !callbackOwned && !uniform.graphSampler}};
            auto gpu = gpuValues.find(name);
            if (gpu != gpuValues.end()) value["rendered_value"] = gpu->second;
            auto var = shader.vars.find(name);
            if (var != shader.vars.end()) {
                auto floats = std::dynamic_pointer_cast<Variable<float>>(var->second);
                if (floats) {
                    value["configured_value"] = floats->getVec();
                    value["binding"] = floats->isConst() ? "constant" : "expression";
                }
            }
            uniforms[name] = value;
        }
        for (const auto& pair : shader.getTextures()) {
            textures[pair.first] = {{"loaded", pair.second && pair.second->isLoaded()},
                {"source", pair.second && pair.second->tex ? pair.second->tex->path : ""}};
            auto feedback = pair.second ? dynamic_cast<ShaderPingPongTex*>(pair.second->tex.get()) : nullptr;
            if (feedback) {
                std::string source, reason;
                const bool provenance = feedback->getExportSource(source, reason);
                bool supported = feedback->validateConfiguration(pair.second->data, reason);
                if (supported && (pair.first != DEFAULT_TEX || feedbackCount != 1 ||
                    pair.second->numFrames != 1 || !pair.second->passes.empty() || pair.second->hasLooper() ||
                    !pair.second->isLoaded() || !feedback->firstUpdateComplete())) {
                    supported = false; reason = "feedback holder is outside export scope or has not completed its first update";
                }
                if (auto* registry = ResourceRegistry::current()) for (const auto& shared : registry->textureSharedPool)
                    if (shared.second == pair.second) { supported = false; reason = "shared feedback is outside export scope"; }
                const auto size = pair.second->data.getSize();
                const auto settings = pair.second->data.getFboSettings();
                Json controls = Json::object();
                for (const auto& binding : pair.second->vars) {
                    if (binding.first == "size" || binding.first == "speed" || binding.first == "color" || binding.first == "tint" || binding.first == "timePct") continue;
                    try { controls[binding.first] = describeVariable(binding.second); }
                    catch (const Invalid& error) { controls[binding.first] = {{"unsupported", error.what()}}; }
                }
                textures[pair.first]["feedback"] = {{"kind", "shader_feedback"}, {"retained_fragment", provenance ? Json(source) : Json(nullptr)},
                    {"provenance", provenance}, {"export_supported", supported && provenance}, {"reason", reason},
                    {"width", size.x}, {"height", size.y}, {"internalformat", settings.internalformat},
                    {"textureTarget", settings.textureTarget}, {"static", pair.second->isStatic},
                    {"white_seed", true}, {"history_exported", false}, {"updates", feedback->updateCount()},
                    {"color_payload_bytes", 2.0 * size.x * size.y * 4.0}, {"controls", controls}};
            }
        }
        layers.push_back({{"index", layer->index}, {"alpha", layer->getVar("alpha")},
                          {"visible", layer->getVarBool("visible")},
                          {"size", {layer->data.getSize().x, layer->data.getSize().y}},
                          {"geometry_loaded", layer->geom && layer->geom->isLoaded()},
                          {"shader", {{"loaded", shader.isLoaded()}, {"path", shader.getShaderPath()},
                            {"program", shader.isShaderLoaded() ? shader.getShader().getProgram() : 0},
                            {"uniforms", uniforms}, {"textures", textures}}}});
    }
    std::string layout;
    for (const auto& pair : LayoutMap) if (pair.second == render().getLayout()) layout = pair.first;
    Json pending = Json::array();
    for (const auto& id : receiptOrder) {
        const auto& response = receipts.at(id).response;
        const auto status = response.value("status", std::string());
        if (status == "queued" || status == "applied" || status == "evaluating" ||
            status == "ready" || status == "committing" || status == "rolling_back") pending.push_back(response);
    }
    Json result = {{"layers", layers}, {"layout", layout}, {"camera_enabled", runtime().camera.isEnabled()},
            {"revision", revision}, {"rendered_revision", renderedRevision}, {"frame", frame},
            {"external_epoch", externalEpoch}, {"pending", pending},
            {"trial", trial ? trialSummary() : Json()},
            {"onset_gate", commands().isGateEnabled()}, {"legacy_pending", commands().pendingCount()},
            {"width", ofGetWidth()}, {"height", ofGetHeight()}, {"fps", ofGetFrameRate()}};
    if (draftPreview) {
        result["backend"] = wgsl ? "webgpu" : "opengl";
        result["draft_activation"] = draftActivation;
        result["draft_sample_sequence"] = draftSampleSequence;
        auto slots = Json::object();
        if (!wgsl) for (const auto& [name, input] : runtime().inputs.all()) slots[name] = input->get();
        result["draft_slots"] = std::move(slots);
    }
#ifdef VOSC_WEBGPU_M1
    if (wgsl) {
        result["frame"] = draftWebGpuFrame;
        result["width"] = m1Preview->width;
        result["height"] = m1Preview->height;
        if (m1Preview->deviceLost() || m1Preview->shouldClose()) result["draft_device_lost"] = true;
    }
#endif
    if (!wgsl && !selfFeedbackDraw.is_null()) result["self_feedback"] = selfFeedbackDraw;
    if (!wgsl && !graphDraw.is_null()) result["graph_draw"] = graphDraw;
    return result;
}

namespace {
void captureComposedPng(const Json& params, int& backingWidth, int& backingHeight,
                        ofPixels& pixels, ofBuffer& png) {
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    int width = viewport[2], height = viewport[3];
    int budget = params.value("max_readback_pixels", 16 * 1024 * 1024);
    if (width <= 0 || height <= 0 || uint64_t(width) * height > static_cast<uint64_t>(budget))
        throw Invalid("readback_failed", "framebuffer unavailable or exceeds requested readback budget");
    backingWidth = width;
    backingHeight = height;
    pixels.allocate(width, height, OF_PIXELS_RGBA);
    GLint packAlignment, packBuffer;
    glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i) {} // Discard prior errors boundedly.
    glReadPixels(viewport[0], viewport[1], width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.getData());
    GLenum error = glGetError();
    glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
    if (error != GL_NO_ERROR) throw Invalid("readback_failed", "OpenGL pixel readback failed");
    pixels.mirror(true, false);
    int maximum = params.value("max_width", 640);
    double scale = std::min(1.0, std::min(double(maximum) / width, 1024.0 / height));
    if (scale < 1) pixels.resize(std::max(1, int(width * scale)), std::max(1, int(height * scale)));
    if (!ofSaveImage(pixels, png, OF_IMAGE_FORMAT_PNG)) throw Invalid("readback_failed", "PNG encoding failed");
}
} // namespace

#ifdef VOSC_RECOVERY_TESTING
void EngineControl::recoveryTestCaptureGraph(const char* phase, const Json& previous, uint64_t heldStep) {
    const auto* directory = std::getenv("VOSC_TEST_HYDRA_CAPTURE_DIR");
    if (!directory || directory[0] != '/') return;
    const auto path = std::string(directory) + "/" + phase + "-" + std::to_string(frame);
    try {
        int width = 0, height = 0;
        ofPixels pixels;
        ofBuffer png;
        captureComposedPng(Json{{"max_width", 640}}, width, height, pixels, png);
        if (!ofBufferToFile(path + ".png", png, true)) throw std::runtime_error("PNG write failed");
        Json receipt = {{"phase", phase}, {"frame", frame}, {"revision", revision},
                        {"graph_draw", graphDraw}, {"previous_graph_draw", previous},
                        {"held_step", heldStep}, {"draft_activation", draftActivation}};
        if (draftCommitReceipt && draftCommitReceipt->response.value("frame", uint64_t(0)) == frame)
            receipt["commit"] = draftCommitReceipt->response;
        ofBuffer metadata;
        metadata.set(receipt.dump(2));
        if (!ofBufferToFile(path + ".json", metadata, false))
            throw std::runtime_error("draw witness receipt write failed");
    } catch (const std::exception& error) {
        ofLogError("control") << "Could not capture Hydra graph witness: " << error.what();
    }
}

Json EngineControl::recoveryTestConflictFeedbackTrial(const Json& command) {
    Json result;
    if (command.value("invalidate_baseline_program", false) && trial && trial->feedbackBaseline) {
        trial->baselineProgram = 0;
        result["candidate_owned"] = bool(ownedFeedbackHolder());
    }
    if (command.count("action")) result["action"] = Json::parse(handle(command.at("action").dump()));
    if (command.count("foreign_source") && trial && trial->feedbackBaseline)
        result["foreign_installed"] = expectedFeedback().loadFromSource(command.at("foreign_source").get<std::string>());
    interruptTrial();
    noteExternalChange();
    return result;
}

Json EngineControl::recoveryTestConfigureFeedbackTrial(const Json& command) {
    recoveryTestFeedbackTimerLost = command.value("timer_lost", false);
    recoveryTestSuppressFeedbackPairs = command.value("suppress_pairs", false);
    recoveryTestFeedbackCapture = command.value("capture", std::string());
    recoveryTestFeedbackCaptureResult = nullptr;
    return {{"capture", recoveryTestFeedbackCapture}, {"timer_lost", recoveryTestFeedbackTimerLost},
        {"suppress_pairs", recoveryTestSuppressFeedbackPairs}};
}

void EngineControl::recoveryTestCaptureFeedback(std::string phase, ShaderPingPongTex& texture) {
    if (recoveryTestFeedbackCapture != phase) return;
    recoveryTestFeedbackCapture.clear();
    auto& result = recoveryTestFeedbackCaptureResult;
    result = {{"phase", phase}, {"frame", frame}, {"revision", revision},
        {"external_epoch", externalEpoch}, {"updates", texture.updateCount()},
        {"program", texture.getShader().getProgram()}, {"source_fbo", texture.fbo.source().getId()},
        {"destination_fbo", texture.fbo.dest().getId()},
        {"alpha", render().getLayers()[1]->getVar("alpha")}, {"monotonic_seconds", monotonicSeconds()},
        {"transition_interval_ms", recoveryTestDrawInterval ? Json(*recoveryTestDrawInterval) : Json()}};
    if (trial && trial->feedbackBaseline) {
        result["trial_id"] = trial->id;
        result["evaluation_revision"] = trial->evaluationRevision;
        result["baseline_updates"] = trial->feedbackBaseline->updateCount();
    }
    try {
        int width = 0, height = 0;
        ofPixels pixels;
        ofBuffer png;
        captureComposedPng(Json{{"max_width", 800}}, width, height, pixels, png);
        const auto path = std::string(std::getenv("VOSC_RECOVERY_TEST_DIR")) +
            "/feedback-" + phase + "-" + std::to_string(frame) + ".png";
        if (!ofBufferToFile(path, png, true)) throw std::runtime_error("PNG write failed");
        result["path"] = path;
    } catch (const std::exception& error) {
        result["error"] = error.what();
    }
}

uint64_t EngineControl::recoveryTestFeedbackUpdates() const {
    const auto& layers = render().getLayers();
    if (layers.size() != 2 || !layers[0]) return 0;
    const auto& textures = layers[0]->shader.getTextures();
    const auto target = textures.find(DEFAULT_TEX);
    if (target == textures.end() || !target->second) return 0;
    const auto* texture = dynamic_cast<ShaderPingPongTex*>(target->second->tex.get());
    return texture ? texture->updateCount() : 0;
}
#endif

void EngineControl::capture(const std::string& id) {
    auto& entry = receipts.at(id);
    try {
        int width = 0, height = 0;
        ofPixels pixels;
        ofBuffer png;
        const auto params = entry.request.value("params", Json::object());
        const auto minimumRevision = params.value("minimum_revision", uint64_t(0));
        if (renderedRevision < minimumRevision)
            throw Invalid("stale_revision", "The rendered scene did not reach the requested revision");
        if (!entry.feedbackPreviewOwner.empty() &&
            (feedbackPreviewDraw.is_null() || feedbackPreviewDraw.at("frame") != frame ||
             feedbackPreviewDraw.at("trial_id") != entry.feedbackPreviewOwner))
            throw Invalid("trial_conflict", "preview owner did not draw the requested frame");
        captureComposedPng(params, width, height, pixels, png);
        entry.response = reply(id, "captured");
        entry.response["result"] = {{"mime_type", "image/png"}, {"data_base64", base64(png)},
            {"width", pixels.getWidth()}, {"height", pixels.getHeight()},
            {"backing_width", width}, {"backing_height", height},
            {"minimum_revision", minimumRevision}};
        if (!entry.feedbackPreviewOwner.empty())
            entry.response["result"]["feedback_trial"] = feedbackPreviewDraw;
        if (!selfFeedbackDraw.is_null() && selfFeedbackDraw["frame"] == frame)
            entry.response["result"]["self_feedback"] = selfFeedbackDraw;
        if (!graphDraw.is_null() && graphDraw["frame"] == frame)
            entry.response["result"]["graph_draw"] = graphDraw;
        images.push_back(id);
        while (images.size() > 2) {
            auto old = receipts.find(images.front());
            if (old != receipts.end()) {
                old->second.response = failure(images.front(), "preview_expired", "image payload retention exceeded");
                old->second.response["status"] = "expired";
            }
            images.pop_front();
        }
    } catch (const Invalid& error) {
        entry.response = failure(id, error.code, error.what());
    } catch (...) {
        entry.response = failure(id, "readback_failed", "capture failed");
    }
}

#ifdef VOSC_WEBGPU_M1
bool EngineControl::draftWebGpuActive() const {
    return draftActivation.is_object() && draftActivation.value("backend", std::string()) == "webgpu";
}

webgpu::M1Preview* EngineControl::draftWebGpuContext() const {
    return m1Preview ? m1Preview.get() : draftWebGpuStaging.get();
}

void EngineControl::pollDraftWebGpuPrepare() {
    auto* context = draftWebGpuContext();
    if (!draftCandidate || draftCandidate->wgslPrepareId.empty()) {
        if (context) context->pollPreparation(); // Drain an abandoned Dawn callback.
        return;
    }
    const auto& id = draftCandidate->wgslPrepareId;
    auto& entry = receipts.at(id);
    if (entry.response["status"] != "queued") return;
    const auto fail = [&](const char* code, const std::string& detail, const Json& diagnostics = Json::array()) {
        entry.response = failure(id, code, detail, diagnostics);
        entry.response["method"] = "draft_prepare";
        discardDraftCandidate();
    };
    try {
        if (!context) {
            auto* window = nativeGlWindow();
            if (!window) throw std::runtime_error("OpenGL preview window is unavailable");
            draftWebGpuStaging = std::make_unique<webgpu::M1Preview>(window);
            context = draftWebGpuStaging.get();
        }
        if (context->deviceLost()) {
            if (draftWebGpuActive()) {
                fail("draft_state_lost", "The WebGPU device was lost. Start a fresh preview session");
                return;
            }
            throw std::runtime_error("The WebGPU device was lost during preparation");
        }
        const bool ready = context->pollPreparation();
        if (!draftCandidate->wgslStarted) {
            const auto source = decodeDraftSource(entry.request["params"]["source_base64"]);
            draftCandidate->wgslStarted = context->prepare(source);
        } else if (ready) {
            draftCandidate->expiresAt = monotonicSeconds() + 30.;
            entry.response = reply(id, "prepared");
            entry.response["method"] = "draft_prepare";
            entry.response["result"] = {{"token", draftCandidate->token},
                {"bindings", Json::object()}, {"conflicts", Json::array()}};
        }
    } catch (const Invalid& error) {
        fail(error.code.c_str(), error.what());
    } catch (const std::exception& error) {
        if (draftWebGpuActive() && context->deviceLost()) {
            fail("draft_state_lost", "The WebGPU device was lost. Start a fresh preview session");
        } else if (context && context->hasDevice() && !context->deviceLost()) {
            const std::string detail = error.what();
            fail("shader_failed", detail, Json::array({{{"message", detail.substr(0, 2048)}}}));
        } else {
            fail("wgsl_unavailable", std::string("WebGPU is unavailable. Check the Render Host's Vulkan or Metal adapter. ") + error.what());
            draftWebGpuStaging.reset();
        }
    }
}

bool EngineControl::settleDraftWebGpuDraw() {
    if (draftWebGpuCommit.empty()) return false;
    const auto id = std::exchange(draftWebGpuCommit, {});
    auto& entry = receipts.at(id);
    const auto& params = entry.request["params"];
    auto* context = draftWebGpuContext();
    if (!context || !draftCandidate || draftCandidate->wgslPrepareId.empty() ||
        draftCandidate->lease != params["lease"].get_ref<const std::string&>() ||
        draftCandidate->token != params["token"].get_ref<const std::string&>() ||
        fencedDraftLeases.count(draftCandidate->lease) || revision != draftCandidate->revision ||
        monotonicSeconds() >= draftCandidate->expiresAt) {
        entry.response = failure(id, "stale_candidate", "The prepared WGSL draft no longer owns this preview");
        entry.response["method"] = "draft_commit";
        draftCommitReceipt = entry;
        return false;
    }
    auto activation = Json{{"lease", draftCandidate->lease}, {"token", draftCandidate->token},
        {"source_digest", draftCandidate->sourceDigest}, {"control_digest", params["control_digest"]},
        {"commit_id", id}, {"backend", "webgpu"}, {"first_frame", frame}};
    bool rejectFrame = false;
#ifdef VOSC_RECOVERY_TESTING
    rejectFrame = draftCandidate->token == "test-fail-first-draft-draw";
#endif
    try {
        context->presentPrepared(rejectFrame);
    } catch (const std::exception& error) {
        discardDraftCandidate();
        const bool priorWebGpu = draftWebGpuActive();
        bool restored = !priorWebGpu;
        if (priorWebGpu && !context->deviceLost() && !context->shouldClose()) {
            try {
                context->draw();
                draftWebGpuFrame = frame;
                restored = true;
            } catch (const std::exception& rollbackError) {
                ofLogWarning("control") << rollbackError.what();
            }
        }
        const bool lost = context->deviceLost() || context->shouldClose();
        entry.response = failure(id, priorWebGpu && lost ? "draft_state_lost" : "draft_rolled_back", error.what());
        if (draftWebGpuStaging && lost) draftWebGpuStaging.reset();
        entry.response["result"] = {{"scene", scene()}, {"restored_previous", restored}};
        entry.response["method"] = "draft_commit";
        draftCommitReceipt = entry;
        return priorWebGpu && !lost;
    }
    if (draftWebGpuStaging) {
        if (auto* window = nativeGlWindow()) glfwHideWindow(window);
        m1Preview = std::move(draftWebGpuStaging);
    }
    draftCandidate.reset();
    draftActivation = std::move(activation);
    draftSampleSequence = 0;
    draftWebGpuFrame = frame;
    renderedRevision = ++revision;
    entry.response = reply(id, "rendered");
    auto result = draftActivation;
    const auto& preview = *m1Preview;
    result["capture_width"] = preview.width;
    result["capture_height"] = preview.height;
    result["pixels"] = Json::array();
    for (const auto& pixel : preview.pixels)
        result["pixels"].push_back({{"x", pixel.x}, {"y", pixel.y}, {"rgba", pixel.rgba}});
    result["scene"] = scene();
    entry.response["result"] = std::move(result);
    entry.response["method"] = "draft_commit";
    draftCommitReceipt = entry;
    return true;
}

Json EngineControl::m1Status() const {
    const auto* glAdapter = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    auto* window = nativeGlWindow();
    Json result = {{"pid", ::getpid()}, {"active", m1Preview ? "webgpu" : "opengl"},
        {"backend", m1Preview ? m1Preview->backend : "OpenGL"},
        {"adapter", m1Preview ? m1Preview->adapter : (glAdapter ? glAdapter : "")},
        {"opengl_frame", m1OpenGlFrame}, {"webgpu_frames", m1Preview ? m1Preview->frames : 0},
        {"webgpu_context", m1Preview ? m1Preview->contextId : 0},
        {"opengl_visible", window && glfwGetWindowAttrib(window, GLFW_VISIBLE) == GLFW_TRUE},
        {"webgpu_visible", m1Preview && m1Preview->visible()}};
    return result;
}

void EngineControl::completeM1OpenGlHandoff() {
    const auto id = std::exchange(m1Switch, {});
    m1OpenGlStaged = false;
    auto* window = nativeGlWindow();
    if (!window) {
        receipts.at(id).response = failure(id, "m1_switch_failed", "OpenGL window is unavailable");
        return;
    }
    // The previous normal OF frame has returned and swapped by this update.
    glfwShowWindow(window);
    m1Preview.reset();
    auto& response = receipts.at(id).response;
    response = reply(id, "rendered");
    response["result"] = m1Status();
    std::cout << "VOSC_WEBGPU_M1 " << response["result"].dump() << std::endl;
}

void EngineControl::drawM1Preview() {
    m1OpenGlFrame = frame;
    if (settleDraftWebGpuDraw()) return;
    if (draftWebGpuActive()) {
        if (m1Preview->deviceLost() || m1Preview->shouldClose()) {
            if (auto* window = nativeGlWindow()) glfwSetWindowShouldClose(window, GLFW_TRUE);
            return;
        }
        try {
            m1Preview->draw();
            draftWebGpuFrame = frame;
        } catch (const std::exception& error) {
            ofLogWarning("control") << error.what();
        }
        return;
    }
    if (!m1Switch.empty()) {
        auto& receipt = receipts.at(m1Switch);
        const auto& params = receipt.request.at("params");
        if (params.at("backend") == "opengl") {
            m1OpenGlStaged = true;
            return;
        }
        const auto id = std::exchange(m1Switch, {});
        try {
            auto* window = nativeGlWindow();
            if (!window) throw std::runtime_error("OpenGL window is unavailable");
            auto candidate = std::make_unique<webgpu::M1Preview>(window, params.at("source").get<std::string>());
            glfwHideWindow(window);
            m1Preview = std::move(candidate);
            receipt.response = reply(id, "rendered");
            auto& result = receipt.response["result"] = m1Status();
            result["capture_width"] = m1Preview->width;
            result["capture_height"] = m1Preview->height;
            auto& pixels = result["pixels"] = Json::array();
            for (const auto& pixel : m1Preview->pixels)
                pixels.push_back({{"x", pixel.x}, {"y", pixel.y}, {"rgba", pixel.rgba}});
            std::cout << "VOSC_WEBGPU_M1 " << result.dump() << std::endl;
        } catch (const std::exception& error) {
            receipt.response = failure(id, "m1_switch_failed", error.what());
            receipt.response["result"] = m1Status();
            std::cerr << "VOSC_WEBGPU_M1 candidate failed: " << error.what() << std::endl;
        }
    } else if (m1Preview) {
        try {
            m1Preview->draw();
        } catch (const std::exception& error) {
            if (auto* window = nativeGlWindow()) glfwShowWindow(window);
            m1Preview.reset();
            std::cerr << "VOSC_WEBGPU_M1 preview stopped: " << error.what() << std::endl;
        }
    }
}
#endif

void EngineControl::afterDraw() {
    if (!server.isRunning()) return;
    ++frame;
#ifdef VOSC_WEBGPU_M1
    if (draftPreview) drawM1Preview();
    if (draftWebGpuActive()) {
        while (!captures.empty()) {
            const auto id = captures.front();
            captures.pop_front();
            receipts.at(id).response = failure(id, "wgsl_operation_unsupported", "GL preview capture is unavailable on the WGSL preview");
        }
        return;
    }
#endif
    renderedRevision = revision;
#ifdef VOSC_RECOVERY_TESTING
    const auto previousGraph = graphDraw;
    const auto& layers = render().getLayers();
    const auto* drawnShader = layers.size() == 1 && layers[0] ? &layers[0]->shader : nullptr;
    const bool graphFault = drawnShader && drawnShader->graphFaultThisFrame();
    const bool graphCommitFault = graphFault && !draftDraw.id.empty() && draftDraw.baseline;
    const auto heldStep = graphFault ? drawnShader->selfFeedbackStep() : 0;
#endif
    observeSelfFeedbackDraw();
    const bool coherentDraft = settleDraftDraw();
#ifdef VOSC_RECOVERY_TESTING
    if (graphCommitFault) {
        recoveryTestCaptureGraph("rollback", previousGraph, 0);
    } else if (graphFault) {
        recoveryTestGraphHeld = true;
        recoveryTestCaptureGraph("held", previousGraph, heldStep);
    } else if (!graphDraw.is_null()) {
        if (graphDraw["step"] == 1) recoveryTestCaptureGraph("first", previousGraph, 0);
        if (recoveryTestGraphHeld) {
            recoveryTestCaptureGraph("resumed", previousGraph, 0);
            recoveryTestGraphHeld = false;
        }
    }
#endif
    for (auto& pair : receipts) {
        if (pair.second.response["status"] == "applied" &&
            draftDraw.id != pair.first &&
            !(trial && trial->feedbackBaseline && trial->actionId == pair.first)) {
            pair.second.response["status"] = "rendered";
            pair.second.response["frame"] = frame;
            pair.second.response["external_epoch"] = externalEpoch;
        }
    }
    if (trial && trial->feedbackBaseline) {
        if (trial->phase == "committing") {
            telemetry.refreshGpuResults();
            updateTelemetry.refreshGpuResults();
            evaluateTrial();
        }
        if (trial) settleFeedbackDraw();
    } else if (trial && trial->phase == "committing") {
        if (trial->layer >= render().getLayers().size() || !render().getLayers()[trial->layer]) {
            interruptTrial();
        } else {
        auto& shader = render().getLayers()[trial->layer]->shader;
        if (!shader.ownsProgram(static_cast<GLuint>(trial->candidateProgram))) {
            interruptTrial();
        } else {
            auto& original = receipts.at(trial->id).response;
            original = reply(trial->id, "rendered");
            original["method"] = trial->method;
            auto metadata = trialSummary();
            metadata["phase"] = "accepted";
            metadata["rendered_frame"] = frame;
            original["result"] = {{"trial", metadata}};
            original["applied_after_frame"] = trial->firstFrame - 1;
            trial.reset();
        }
        }
    } else if (trial && trial->phase == "rolling_back") {
        if (trial->layer >= render().getLayers().size() || !render().getLayers()[trial->layer]) {
            interruptTrial();
        } else {
        auto& shader = render().getLayers()[trial->layer]->shader;
        const bool restored = shader.ownsProgram(trial->baseline.program->getProgram());
        auto terminal = failure(trial->id, restored ? trial->rollbackCode : "trial_conflict",
            restored ? "protected shader trial rolled back" : "baseline ownership changed before draw");
        terminal["method"] = trial->method;
        auto metadata = trialSummary();
        metadata["phase"] = restored ? "rolled_back" : "interrupted";
        metadata["restored_previous"] = restored;
        metadata["restoration_revision"] = revision;
        if (restored) metadata["rendered_frame"] = frame;
        terminal["result"] = {{"trial", metadata}};
        receipts.at(trial->id).response = terminal;
        if (!trial->actionId.empty() && receipts.count(trial->actionId)) {
            if (restored) {
                auto& action = receipts.at(trial->actionId).response;
                action["status"] = "rendered";
                action["frame"] = frame;
                action["external_epoch"] = externalEpoch;
            } else {
                auto action = failure(trial->actionId, "trial_conflict",
                    "trial action was superseded before its target scene was drawn");
                action["method"] = receipts.at(trial->actionId).request["method"];
                receipts.at(trial->actionId).response = action;
            }
        }
        trial.reset();
        }
    }
#ifdef VOSC_RECOVERY_TESTING
    if (!trial && (recoveryTestFeedbackCapture == "baseline" || recoveryTestFeedbackCapture == "white_replay")) {
        const auto updates = recoveryTestFeedbackUpdates();
        if (updates && (recoveryTestFeedbackCapture == "baseline" || updates == 1)) {
            auto& tex = *static_cast<ShaderPingPongTex*>(render().getLayers()[0]->shader.getDefaultTexture()->tex.get());
            if (tex.drewCurrentUpdate()) recoveryTestCaptureFeedback(recoveryTestFeedbackCapture, tex);
        }
    }
#endif
    if (!captures.empty()) {
        auto id = captures.front();
        captures.pop_front();
        if (coherentDraft) capture(id);
        else receipts.at(id).response = failure(id, "draft_state_lost", "The composed draft could not be observed");
    }
}

void EngineControl::beforeUpdate() {
    if (!server.isRunning()) return;
    if (trial && trial->feedbackBaseline && checkFeedbackOwnership()) {
        if (!trial->feedbackActivated && trial->phase != "rolling_back") {
            auto holder = ownedFeedbackHolder();
            holder->tex = trial->feedbackCandidate;
            trial->feedbackActivated = true;
            trial->firstFrame = frame + 1;
            trial->feedbackCandidate->trackFeedback();
        }
        trial->updatesBefore = expectedFeedback().updateCount();
    }
    const auto current = performanceContext();
    const auto context = feedbackContext(current);
    if (!(context == measurementContext) || measurementStale) {
        measurementContext = context;
        feedbackMeasurement.reset();
        ++measurementId;
        measurementStale = false;
    }
    const uint64_t tag = context.program ? measurementId : 0;
    if (trial && trial->feedbackBaseline && trial->phase != "rolling_back" && !trial->measurementId)
        trial->measurementId = tag;
    bool record = tag != 0;
#ifdef VOSC_RECOVERY_TESTING
    record = record && !recoveryTestSuppressFeedbackPairs;
#endif
    submittedFeedbackUpdate = updateTelemetry.beginFrame(current, frame + 1, monotonicSeconds(), record ? tag : 0) && record;
}

void EngineControl::afterUpdate() {
    if (!server.isRunning()) return;
    updateTelemetry.endComposedDraw(monotonicSeconds());
    if (!trial || !trial->feedbackBaseline || !checkFeedbackOwnership()) return;
    if (expectedFeedback().updateCount() == trial->updatesBefore + 1) {
        trial->updatedFrame = frame + 1;
    } else if (trial->phase != "rolling_back") {
        rollbackTrial("trial_feedback_update_failed");
    }
}

void EngineControl::beforeDraw() {
    if (!server.isRunning()) return;
    if (trial && trial->feedbackBaseline) {
        evaluateTrial();
        if (trial && trial->phase == "rolling_back" && trial->updatedFrame != frame + 1) {
            // A failed candidate write or a rollback between update and draw
            // leaves this frame without a baseline update. Restore one step.
            if (auto holder = ownedFeedbackHolder()) {
                const auto previous = trial->feedbackBaseline->updateCount();
                trial->feedbackBaseline->update(holder->data);
                if (trial->feedbackBaseline->updateCount() == previous + 1)
                    trial->updatedFrame = frame + 1;
            }
        }
    }
    PerformanceContext current = performanceContext();
    const auto context = feedbackContext(current);
    measurementStale = !(context == measurementContext);
    if (measurementStale && (context.program || measurementContext.program)) {
        ++crossingFrames;
        lastCrossingFrame = frame + 1;
    }
    const uint64_t tag = context.program && !measurementStale ? measurementId : 0;
    bool record = tag != 0;
#ifdef VOSC_RECOVERY_TESTING
    record = record && !recoveryTestSuppressFeedbackPairs;
#endif
    const double now = monotonicSeconds();
#ifdef VOSC_RECOVERY_TESTING
    if (recoveryTestPreviousDrawStart) recoveryTestDrawInterval = (now - *recoveryTestPreviousDrawStart) * 1000;
    recoveryTestPreviousDrawStart = now;
#endif
    const bool submitted = telemetry.beginFrame(current, frame + 1, now, record ? tag : 0) && record;
    if (tag) feedbackMeasurement.attempt(frame + 1, now, submittedFeedbackUpdate, submitted);
}

void EngineControl::afterComposedDraw() {
    if (!server.isRunning()) return;
    telemetry.endComposedDraw(monotonicSeconds());
}

} // namespace control
