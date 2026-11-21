#include "CommandSystem.hpp"
#include "../GeomPool.h"
#include "../light/Lights.h"
#include "../TexturePool.h"
#include "../tex/ImageTex.h"
#include "../tex/ShaderTex.h"
#include "../tex/ShaderPingPongTex.h"
#include "../shader/ShaderPool.h"
#include "../var/VariablePool.h"
#include "../ResourceRegistry.hpp"
#include "../Logger.h"
#include "ofAppGLFWWindow.h"
#include <GLFW/glfw3.h>
#ifdef VOSC_RECOVERY_TESTING
#include "ofBufferObject.h"
#include <thread>
#endif
#include "ofxOscSender.h"
#include <arpa/inet.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <iomanip>
#include <locale>
#include <sstream>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>

namespace {
double replayNow() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool validReplayToken(const std::string& value) {
    if (value.size() < 16 || value.size() > 64) return false;
    for (unsigned char ch : value) if (!std::isalnum(ch) && ch != '_' && ch != '-') return false;
    return true;
}

bool recipeAddress(const std::string& value) {
    static const std::set<std::string> allowed = {
        "/layers/reset", "/input", "/input/set", "/var", "/shader", "/shader/var", "/tex",
        "/shader/texture", "/tex/var", "/tex/size", "/tex/speed", "/tex/set", "/shader/texture/size",
        "/shader/texture/speed", "/shader/texture/set", "/layer/pos", "/layer/rot",
        "/layer/scale", "/layer/pivot", "/layer/size", "/layer/tint", "/layer/alpha",
        "/layer/bri", "/layer/speed", "/layer/visible", "/layer/align", "/layer/blend"};
    return allowed.count(value) != 0;
}

#ifdef VOSC_MINIMAL_BUILD
std::string excludedTextureBackend(const osc::Command& command, bool resolveAliases) {
    if (command.type != osc::CommandType::TARGETED_RESOURCE) return {};
    const auto& path = command.resource.commandPath;
    const bool choosing = path == "/tex/choose";
    const bool namedBinding = path == "/shader/texture" || path == "/shader/tex";
    if (path != "/tex" && !choosing && !namedBinding) return {};
    const int argument = namedBinding ? 2 : 1;
    const auto& message = command.raw;
    if (message.getNumArgs() <= argument || message.getArgType(argument) != OFXOSC_TYPE_STRING) return {};
    const auto source = message.getArgAsString(argument);
    if (resolveAliases && (namedBinding || (path == "/tex" && command.resource.target.kind != osc::TargetKind::NAME)) &&
        TexturePool::hasShared(source)) return {};
    return Texture::excludedBackend(source, choosing);
}

void logExcludedFeature(const ofxOscMessage& message, const std::string& feature) {
    // A rejected foreign control is a diagnostic for its sender, not a
    // renderer error attributable to the scene currently being replayed.
    ofLogWarning("minimal") << message.getAddress() << ": " << feature << " unavailable in this build";
}
#endif

bool integerUniformType(GLenum type) {
    return type == GL_INT || type == GL_INT_VEC2 || type == GL_INT_VEC3 || type == GL_INT_VEC4;
}

bool hasImageStorage(const Texture& texture, const ofPixels& pixels) {
    if (!pixels.isAllocated() || !texture.hasTexture(0)) return false;
    const auto& data = texture.getTexture().getTextureData();
    if (data.textureTarget != GL_TEXTURE_2D || glIsTexture(data.textureID) != GL_TRUE)
        return false;
    GLint previous = 0, width = 0, height = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, data.textureID);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    glBindTexture(GL_TEXTURE_2D, previous);
    return width == pixels.getWidth() && height == pixels.getHeight();
}

int uniformComponentCount(GLenum type) {
    if (type == GL_INT || type == GL_FLOAT) return 1;
    if (type == GL_INT_VEC2 || type == GL_FLOAT_VEC2) return 2;
    if (type == GL_INT_VEC3 || type == GL_FLOAT_VEC3) return 3;
    if (type == GL_INT_VEC4 || type == GL_FLOAT_VEC4) return 4;
    return 0;
}

template<typename T>
void setMixedComponents(VarsHolder& owner, const std::string& name,
                        const ofxOscMessage& message, int first, int count) {
    std::vector<std::string> values;
    std::vector<bool> expressionMask;
    values.reserve(count);
    expressionMask.reserve(count);
    for (int index = first; index < first + count; ++index) {
        if (message.getArgType(index) == OFXOSC_TYPE_STRING) {
            values.push_back(message.getArgAsString(index));
            expressionMask.push_back(true);
        } else {
            std::ostringstream component;
            component.imbue(std::locale::classic());
            component << std::setprecision(std::numeric_limits<float>::max_digits10)
                      << message.getArgAsFloat(index);
            values.push_back(component.str());
            expressionMask.push_back(false);
        }
    }
    auto& pool = VariablePool::getPool(&owner);
    if (!pool.count(name) || !std::dynamic_pointer_cast<Variable<T>>(pool.at(name)))
        pool[name] = std::make_shared<Variable<T>>();
    auto variable = std::dynamic_pointer_cast<Variable<T>>(pool.at(name));
    variable->set(values, expressionMask);
    owner.vars[name] = variable;
}

bool recipeIdentifier(const std::string& value) {
    if (value.empty() || value.size() > 64 ||
        !(std::isalpha(static_cast<unsigned char>(value[0])) || value[0] == '_')) return false;
    for (unsigned char ch : value) if (!std::isalnum(ch) && ch != '_') return false;
    return value != "camPos" && value != "camLook" && value != "camOrbit";
}

bool recipeType(const ofxOscMessage& message, int ordinaryIndex, ofxOscArgType type) {
    const int wrappedIndex = ordinaryIndex + 3;
    return wrappedIndex < message.getNumArgs() && message.getArgType(wrappedIndex) == type;
}

bool recipeNumber(const ofxOscMessage& message, int ordinaryIndex) {
    if (recipeType(message, ordinaryIndex, OFXOSC_TYPE_STRING))
        return !message.getArgAsString(ordinaryIndex + 3).empty();
    return recipeType(message, ordinaryIndex, OFXOSC_TYPE_FLOAT) &&
        std::isfinite(message.getArgAsFloat(ordinaryIndex + 3)) &&
        std::abs(message.getArgAsFloat(ordinaryIndex + 3)) <= 1000000.f;
}

bool validUtf8(const std::string& value) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    for (size_t index = 0; index < value.size();) {
        const unsigned char first = bytes[index++];
        if (first <= 0x7f) continue;
        int continuation = 0;
        uint32_t codepoint = 0;
        if (first >= 0xc2 && first <= 0xdf) { continuation = 1; codepoint = first & 0x1f; }
        else if (first >= 0xe0 && first <= 0xef) { continuation = 2; codepoint = first & 0x0f; }
        else if (first >= 0xf0 && first <= 0xf4) { continuation = 3; codepoint = first & 0x07; }
        else return false;
        if (index + continuation > value.size()) return false;
        for (int offset = 0; offset < continuation; ++offset) {
            const unsigned char next = bytes[index++];
            if ((next & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if ((continuation == 2 && codepoint < 0x800) ||
            (continuation == 3 && codepoint < 0x10000) ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff) return false;
    }
    return true;
}

bool recipeString(const std::string& value, bool empty = false) {
    return value.size() <= 2048 && (empty || !value.empty()) &&
        value.find('\0') == std::string::npos && validUtf8(value);
}

bool recipeExpression(const std::string& expression, const std::set<std::string>& shared,
                      const std::set<std::string>& inputs, std::set<std::string>* dependencies = nullptr) {
    if (!recipeString(expression)) return false;
    static const std::set<std::string> constants = {"left", "right", "top", "bottom", "center", "pi", "e", "i", "total"};
    static const std::regex identifier("[A-Za-z_][A-Za-z0-9_]*");
    for (auto it = std::sregex_iterator(expression.begin(), expression.end(), identifier);
         it != std::sregex_iterator(); ++it) {
        const std::string token = it->str();
        size_t next = static_cast<size_t>(it->position() + it->length());
        while (next < expression.size() && std::isspace(static_cast<unsigned char>(expression[next]))) ++next;
        if ((next < expression.size() && expression[next] == '(') || constants.count(token)) continue;
        if (token == "camPos" || token == "camLook" || token == "camOrbit") return false;
        if (shared.count(token)) {
            if (dependencies) dependencies->insert(token);
            continue;
        }
        if (token.rfind("in_", 0) == 0 && inputs.count(token.substr(3))) continue;
        if (token == "t" || token == "f") continue;
        return false;
    }
    return true;
}

bool recipeDependencyGraphAcyclic(const std::map<std::string, std::set<std::string>>& graph) {
    std::set<std::string> visiting, visited;
    std::function<bool(const std::string&)> visit = [&](const std::string& name) {
        if (visiting.count(name)) return false;
        if (visited.count(name)) return true;
        visiting.insert(name);
        auto found = graph.find(name);
        if (found != graph.end()) for (const auto& dependency : found->second)
            if (graph.count(dependency) && !visit(dependency)) return false;
        visiting.erase(name); visited.insert(name); return true;
    };
    for (const auto& item : graph) if (!visit(item.first)) return false;
    return true;
}

std::shared_ptr<Logger> replayLogger() {
    return std::dynamic_pointer_cast<Logger>(ofGetLoggerChannel());
}

std::pair<int, int> replayFramebufferSize() {
    auto* window = dynamic_cast<ofAppGLFWWindow*>(ofGetWindowPtr());
    if (!window || !window->getGLFWWindow()) return {-1, -1};
    int width = -1, height = -1;
    glfwGetFramebufferSize(window->getGLFWWindow(), &width, &height);
    return {width, height};
}
}

void CommandSystem::setup() {
    commandRouter.clear();

    commandRouter.registerHandler(osc::CommandType::INPUT, [this](const osc::Command& command) {
        runtime.inputs.oscCommand(command.input.commandPath, command.raw);
        if (command.input.action == osc::InputAction::DATA && waitOnset == -1) {
            waitOnset = 1;
        }
    });

    commandRouter.registerHandler(osc::CommandType::MIDI, [this](const osc::Command& command) {
        handleMidi(command);
    });

    commandRouter.registerHandler(osc::CommandType::DIRT_PLAY, [this](const osc::Command& command) {
        ofxOscMessage nonConstM = command.raw;
        runtime.tidal->parse(nonConstM);
        if (waitOnset == -1) {
            waitOnset = 1;
        }
    });

    commandRouter.registerHandler(osc::CommandType::ONSET, [this](const osc::Command& command) {
        waitOnset = command.hasOnsetValue
            ? static_cast<int>(command.onsetValue)
            : static_cast<int>(!(bool)waitOnset);
    });

    commandRouter.registerHandler(osc::CommandType::ONSET_FORCE, [this](const osc::Command&) {
        forceOnset = true;
    });

    commandRouter.registerHandler(osc::CommandType::LAYERS, [this](const osc::Command& command) {
        renderSystem.applyLayersPayload(command.layers);
    });
    commandRouter.registerHandler(osc::CommandType::LAYERS_RESET, [this](const osc::Command& command) {
        renderSystem.resetLayers(command.layers);
    });
    commandRouter.registerHandler(osc::CommandType::LAYERS_LAYOUT, [this](const osc::Command& command) {
        renderSystem.layoutLayers(command.layers.hasLayout ? command.layers.layout : renderSystem.getLayout());
    });

    commandRouter.registerHandler(osc::CommandType::CAMERA, [this](const osc::Command& command) {
        runtime.camera.oscCommand(command.camera.commandPath, command.raw);
    });

    commandRouter.registerHandler(osc::CommandType::LIGHT, [this](const osc::Command& command) {
        Lights::get().create(command.raw);
    });
    commandRouter.registerHandler(osc::CommandType::LIGHT_REMOVE, [this](const osc::Command& command) {
        Lights::get().remove(command.raw);
    });

    commandRouter.registerHandler(osc::CommandType::SHADING_MODE, [this](const osc::Command& command) {
        renderSystem.setDeferredShading(command.shadingMode == "deferred");
    });
    commandRouter.registerHandler(osc::CommandType::SHADING_PASSES, [this](const osc::Command& command) {
        renderSystem.applyShadingPasses(command.shadingPasses);
    });

    commandRouter.registerHandler(osc::CommandType::TARGETED_RESOURCE, [this](const osc::Command& command) {
        routeTargetedResource(command);
    });
}

void CommandSystem::update() {
    if (!startupScenePending()) {
        inputSystem.forEachOscMessage([this](const ofxOscMessage& m) {
            parseMessage(m);
        });
    }

    gateOpened = waitOnset < 1 || forceOnset || checkOnset();
    if (gateOpened) {
        processQueue();
        forceOnset = false;
    }
    if (sceneReplay && replayNow() >= sceneReplay->deadline) failSceneReplay("native replay observation timed out");
}

bool CommandSystem::isQueuedCommand(osc::CommandType type) const {
    return type == osc::CommandType::SHADING_MODE
        || type == osc::CommandType::SHADING_PASSES
        || type == osc::CommandType::TARGETED_RESOURCE;
}

bool CommandSystem::checkOnset() const {
    if (!runtime.tidal->notes.empty()) {
        return true;
    }
    return runtime.inputs.checkOnset();
}

void CommandSystem::parseMessage(const ofxOscMessage& m) {
    if (!hasCommandRegistry(m)) return;
    if (handleSceneEnvelope(m)) return;
#ifndef VOSC_MINIMAL_BUILD
    if (sceneReplay && m.getAddress() != "/input/data") failSceneReplay("foreign control superseded replay before draw");
#endif
    auto parsed = commandParser.parse(m);
#ifdef VOSC_MINIMAL_BUILD
    if (parsed.isOk()) {
        const auto feature = RenderSystem::excludedShadingFeature(parsed.value());
        if (!feature.empty()) {
            logExcludedFeature(m, feature);
            return;
        }
        if (!excludedTextureBackend(parsed.value(), false).empty()) {
            // An earlier queued command can create a shared texture with this name.
            // Delay both validation and the external-control hooks until dispatch.
            messageQueue.push_back(parsed.value());
            return;
        }
    }
    if (sceneReplay && m.getAddress() != "/input/data") failSceneReplay("foreign control superseded replay before draw");
#endif
    if (!parsed.isOk()) {
        invalidCommand(parsed.error());
        return;
    }

    const osc::Command& command = parsed.value();
    const bool inputSample = command.type == osc::CommandType::INPUT &&
        command.input.action == osc::InputAction::DATA;
    if (inputSample && !runtime.inputs.hasConfiguredTarget(command.raw)) return;
    const bool replaySample = sceneReplay && inputSample;
    if (!replaySample && onBeforeLegacyControl) onBeforeLegacyControl(inputSample);
    if (isQueuedCommand(command.type)) {
        messageQueue.push_back(command);
        return;
    }

    if (!routeLegacyCommand(command)) return;
    if (inputSample && onLegacySample) {
        onLegacySample();
    } else if (onLegacyControl) {
        onLegacyControl();
    }
}

bool CommandSystem::handleSceneEnvelope(const ofxOscMessage& message) {
    const std::string address = message.getAddress();
    if (address != "/scene/reset" && address != "/scene/command" && address != "/scene/replay/end") return false;
    const std::string host = message.getRemoteHost();
    const int sourcePort = message.getRemotePort();
    if (address == "/scene/reset") {
        if (message.getNumArgs() != 4 || message.getArgType(0) != OFXOSC_TYPE_STRING ||
            message.getArgType(1) != OFXOSC_TYPE_INT32 || message.getArgType(2) != OFXOSC_TYPE_INT32 ||
            message.getArgType(3) != OFXOSC_TYPE_INT32) return true;
        const std::string token = message.getArgAsString(0);
        const int replyPort = message.getArgAsInt(1);
        const int width = message.getArgAsInt(2);
        const int height = message.getArgAsInt(3);
        if (!validReplayToken(token) || replyPort < 1024 || replyPort > 65535 ||
            width < 64 || width > 2048 || height < 64 || height > 2048) {
            if (validReplayToken(token) && replyPort >= 1024 && replyPort <= 65535)
                sendSceneReply(host, replyPort, token, "failed", -1, "invalid replay opener");
            return true;
        }
        if (sceneReplay || (canBeginSceneReplay && !canBeginSceneReplay())) {
            sendSceneReply(host, replyPort, token, "failed", -1, "renderer replay surface is busy");
            return true;
        }
        const uint64_t diagnosticStart = replayLogger() ? replayLogger()->latestSequence() : 0;
        const uint64_t serial = currentConfigurationSerial ? currentConfigurationSerial() : 0;
        sceneReplay = SceneReplay{token, host, sourcePort, replyPort, width, height, 1, 0, -1, -1,
                                  false, false, replayNow() + 12.0, false, diagnosticStart, serial};
        resetScene(width, height);
        return true;
    }
    if (message.getNumArgs() < 1 || message.getArgType(0) != OFXOSC_TYPE_STRING) return true;
    const std::string token = message.getArgAsString(0);
    if (!sceneReplay || token != sceneReplay->token || host != sceneReplay->host || sourcePort != sceneReplay->sourcePort) {
        if (address == "/scene/replay/end" && message.getNumArgs() == 4 &&
            message.getArgType(1) == OFXOSC_TYPE_INT32) {
            const int replyPort = message.getArgAsInt(1);
            if (validReplayToken(token) && replyPort >= 1024 && replyPort <= 65535)
                sendSceneReply(host, replyPort, token, "failed", -1, "unknown or refused replay token");
        }
        return true;
    }
    if (address == "/scene/command") {
        SceneReplay validated = *sceneReplay;
        bool routed = false;
        try {
            routed = !sceneReplay->ended && message.getNumArgs() >= 3 &&
                message.getArgType(1) == OFXOSC_TYPE_INT32 && message.getArgType(2) == OFXOSC_TYPE_STRING &&
                sceneReplay->nextSequence <= 2048 && message.getArgAsInt(1) == sceneReplay->nextSequence &&
                recipeAddress(message.getArgAsString(2)) &&
                validateSceneCommand(message, validated, liveSceneUniforms(message)) && routeSceneCommand(message);
        } catch (const std::exception&) {
            routed = false;
        } catch (...) {
            routed = false;
        }
        if (!routed) {
            failSceneReplay("invalid, out-of-order, or unroutable setup command");
            return true;
        }
        *sceneReplay = std::move(validated);
        ++sceneReplay->nextSequence;
        ++sceneReplay->received;
        return true;
    }
    if (message.getNumArgs() != 4 || message.getArgType(1) != OFXOSC_TYPE_INT32 ||
        message.getArgType(2) != OFXOSC_TYPE_INT32 || message.getArgType(3) != OFXOSC_TYPE_INT32) {
        failSceneReplay("invalid replay end marker"); return true;
    }
    const int replyPort = message.getArgAsInt(1);
    const int expected = message.getArgAsInt(2);
    const int finalGate = message.getArgAsInt(3);
    if (replyPort != sceneReplay->replyPort || expected < 0 || expected > 2048 ||
        expected != sceneReplay->received || finalGate < -1 || finalGate > 1) {
        failSceneReplay("replay command count or end metadata mismatch"); return true;
    }
    std::string feedbackReason;
    if (!finishFeedbackSetup(*sceneReplay, feedbackReason)) {
        failSceneReplay(feedbackReason); return true;
    }
    sceneReplay->expected = expected;
    sceneReplay->finalGate = finalGate;
    sceneReplay->ended = true;
    sceneReplay->configurationSerial = currentConfigurationSerial ? currentConfigurationSerial() : 0;
    return true;
}

const Shader::Uniforms* CommandSystem::liveSceneUniforms(const ofxOscMessage& wrapped) const {
    if (wrapped.getArgAsString(2) != "/shader/var" || !recipeType(wrapped, 0, OFXOSC_TYPE_INT32))
        return nullptr;
    const int layer = wrapped.getArgAsInt32(3);
    const auto& layers = renderSystem.getLayers();
    if (layer < 0 || static_cast<size_t>(layer) >= layers.size() || !layers[layer]) return nullptr;
    return &layers[layer]->shader.getUniforms();
}

bool CommandSystem::validateSceneCommand(const ofxOscMessage& m, SceneReplay& state,
                                        const Shader::Uniforms* uniforms) {
    const std::string address = m.getArgAsString(2);
    const int count = m.getNumArgs() - 3;
    auto intAt = [&](int index) { return m.getArgAsInt32(index + 3); };
    auto floatAt = [&](int index) { return m.getArgAsFloat(index + 3); };
    auto stringAt = [&](int index) { return m.getArgAsString(index + 3); };
    auto addComponents = [&](int amount) {
        if (amount < 0 || state.components > 1024 - amount) return false;
        state.components += amount;
        return true;
    };
    auto addBinding = [&](int layerIndex, const std::string& key) {
        state.shaderBindings[layerIndex].insert(key);
        return state.shaderBindings[layerIndex].size() <= 128;
    };
    auto expressionAt = [&](int index, std::set<std::string>* dependencies = nullptr) {
        return recipeType(m, index, OFXOSC_TYPE_STRING) &&
            recipeExpression(stringAt(index), state.sharedVariables, state.inputs, dependencies);
    };
    auto layer = [&]() { return count > 0 && recipeType(m, 0, OFXOSC_TYPE_INT32) &&
        intAt(0) >= 0 && intAt(0) < state.layerCount; };
    if (address == "/layers/reset") {
        if (state.received != 0 || count != 2 || !recipeType(m, 0, OFXOSC_TYPE_INT32) ||
            !recipeType(m, 1, OFXOSC_TYPE_STRING) || intAt(0) < 0 || intAt(0) > 32 ||
            std::set<std::string>{"stack", "grid", "col", "row"}.count(stringAt(1)) == 0) return false;
        state.layerCount = intAt(0); return true;
    }
    if (state.layerCount < 0) return false;
    if (address == "/input") {
        if (state.dependenciesClosed) return false;
        if (count != 4 || !(recipeType(m, 0, OFXOSC_TYPE_STRING) || recipeType(m, 0, OFXOSC_TYPE_INT32)) ||
            !recipeType(m, 1, OFXOSC_TYPE_STRING) || !recipeString(stringAt(1), true) ||
            !recipeType(m, 2, OFXOSC_TYPE_INT32) ||
            !recipeType(m, 3, OFXOSC_TYPE_INT32)) return false;
        const std::string name = recipeType(m, 0, OFXOSC_TYPE_STRING) ? stringAt(0) : std::to_string(intAt(0));
        if ((recipeType(m, 0, OFXOSC_TYPE_STRING) && !recipeIdentifier(name)) ||
            (recipeType(m, 0, OFXOSC_TYPE_INT32) && intAt(0) < 0) || state.inputs.size() >= 16 ||
            state.inputs.count(name) || intAt(2) < 0 || intAt(2) > 32 || intAt(3) < 64 || intAt(3) > 8192 ||
            (intAt(2) != 0 && stringAt(1).empty())) return false;
        state.inputs.insert(name); return true;
    }
    if (address == "/input/set") {
        if (state.dependenciesClosed) return false;
        if (count != 3 || !(recipeType(m, 0, OFXOSC_TYPE_STRING) || recipeType(m, 0, OFXOSC_TYPE_INT32)) ||
            !recipeType(m, 1, OFXOSC_TYPE_STRING) || !recipeType(m, 2, OFXOSC_TYPE_FLOAT)) return false;
        const std::string name = recipeType(m, 0, OFXOSC_TYPE_STRING) ? stringAt(0) : std::to_string(intAt(0));
        const std::string property = stringAt(1);
        return addComponents(1) && state.inputs.count(name) && (property == "maxAmp" || property == "maxLoud") &&
            std::isfinite(floatAt(2)) && std::abs(floatAt(2)) <= 1000000.f &&
            (property != "maxAmp" || floatAt(2) > 0);
    }
    if (address == "/var") {
        if (count != 2 || !recipeType(m, 0, OFXOSC_TYPE_STRING) || !recipeIdentifier(stringAt(0)) ||
            !recipeNumber(m, 1)) return false;
        const std::string name = stringAt(0);
        if (!state.sharedVariables.count(name)) {
            if (state.dependenciesClosed || state.sharedVariables.size() >= 64 ||
                !recipeType(m, 1, OFXOSC_TYPE_FLOAT) || !addComponents(1)) return false;
            state.sharedVariables.insert(name); return true;
        }
        if (state.resourcesStarted || !recipeType(m, 1, OFXOSC_TYPE_STRING) ||
            state.sharedExpressions.count(name) || state.referencedShared.count(name) || !addComponents(1)) return false;
        std::set<std::string> dependencies;
        if (!expressionAt(1, &dependencies)) return false;
        auto graph = state.sharedDependencies;
        graph[name] = dependencies;
        if (!recipeDependencyGraphAcyclic(graph)) return false;
        state.dependenciesClosed = true;
        state.sharedExpressions.insert(name);
        state.referencedShared.insert(dependencies.begin(), dependencies.end());
        state.sharedDependencies = std::move(graph); return true;
    }
    if (!layer()) return false;
    const int index = intAt(0);
    state.dependenciesClosed = true;
    state.resourcesStarted = true;
    if (address == "/shader" || address == "/tex") {
        if (count != 2 || !recipeType(m, 1, OFXOSC_TYPE_STRING) || !recipeString(stringAt(1))) return false;
        auto& declared = address == "/shader" ? state.shaders : state.defaultTextures;
        if (declared.count(index) ||
            (address == "/tex" && state.namedTextures.count({index, DEFAULT_TEX}))) return false;
        if (address == "/shader" && stringAt(1).find(':') != std::string::npos) return false;
        if (address == "/tex" && stringAt(1).rfind("shader_pp:", 0) == 0) {
            if (state.feedbackLayer >= 0 || stringAt(1).size() <= 10) return false;
            state.feedbackLayer = index;
        } else if (address == "/tex" && stringAt(1).find(':') != std::string::npos) return false;
        declared.insert(index);
        return true;
    }
    if (address == "/tex/var") {
        if (index != state.feedbackLayer || count < 3 || count > 6 ||
            !recipeType(m, 1, OFXOSC_TYPE_STRING) || !recipeIdentifier(stringAt(1))) return false;
        static const std::set<std::string> reserved = {"time", "resolution", "random", "randomSeed", "srctex", "size", "speed", "color", "tint", "timePct",
            "mouse", "frame", "modelViewProjectionMatrix", "modelViewMatrix", "projectionMatrix", "textureMatrix",
            "globalColor", "usingTexture", "usingColors"};
        if (stringAt(1).rfind("gl_", 0) == 0 || reserved.count(stringAt(1)) || !state.feedbackControls.insert(stringAt(1)).second ||
            state.feedbackControls.size() > 64 || !addComponents(count - 2)) return false;
        for (int i = 2; i < count; ++i)
            if (!recipeType(m, i, OFXOSC_TYPE_FLOAT) || !recipeNumber(m, i)) return false;
        return true;
    }
    if (address == "/shader/var") {
        if (count < 3 || count > 6 || !state.shaders.count(index) || !recipeType(m, 1, OFXOSC_TYPE_STRING) ||
            !recipeIdentifier(stringAt(1))) return false;
        bool sawInt = false, sawFloat = false;
        for (int i = 2; i < count; ++i) {
            if (!recipeType(m, i, OFXOSC_TYPE_INT32) && !recipeType(m, i, OFXOSC_TYPE_FLOAT) &&
                !recipeType(m, i, OFXOSC_TYPE_STRING)) return false;
            sawInt = sawInt || recipeType(m, i, OFXOSC_TYPE_INT32);
            sawFloat = sawFloat || recipeType(m, i, OFXOSC_TYPE_FLOAT);
            if (recipeType(m, i, OFXOSC_TYPE_FLOAT) &&
                (!std::isfinite(floatAt(i)) || std::abs(floatAt(i)) > 1000000.f)) return false;
            if (recipeType(m, i, OFXOSC_TYPE_STRING) && !expressionAt(i)) return false;
        }
        if ((sawInt && sawFloat) || !addComponents(count - 2)) return false;
        if (uniforms) {
            const auto reflected = uniforms->find(stringAt(1));
            if (reflected != uniforms->end()) {
                if (uniformComponentCount(reflected->second.type) != count - 2) return false;
                if (integerUniformType(reflected->second.type) && sawFloat) return false;
                if (!integerUniformType(reflected->second.type) && sawInt) return false;
            }
        }
        return addBinding(index, "uniform:" + stringAt(1));
    }
    if (address == "/shader/texture") {
        if (count != 3 || !state.shaders.count(index) || !recipeType(m, 1, OFXOSC_TYPE_STRING) ||
            !recipeIdentifier(stringAt(1)) || !recipeType(m, 2, OFXOSC_TYPE_STRING) || !recipeString(stringAt(2))) return false;
        if ((stringAt(1) == DEFAULT_TEX && state.defaultTextures.count(index)) ||
            !state.namedTextures.insert({index, stringAt(1)}).second) return false;
        return addBinding(index, "sampler:" + stringAt(1));
    }
    const bool named = address.rfind("/shader/texture/", 0) == 0;
    int tail = named ? 2 : 1;
    if (address.rfind("/tex/", 0) == 0 && !state.defaultTextures.count(index)) return false;
    if (named && (count < 2 || !recipeType(m, 1, OFXOSC_TYPE_STRING) ||
                  !state.namedTextures.count({index, stringAt(1)}))) return false;
    if (address == "/tex/size" || address == "/shader/texture/size") {
        const bool valid = count == tail + 2 && addComponents(2) && recipeType(m, tail, OFXOSC_TYPE_FLOAT) &&
            recipeType(m, tail + 1, OFXOSC_TYPE_FLOAT) && std::isfinite(floatAt(tail)) &&
            std::isfinite(floatAt(tail + 1)) && floatAt(tail) > 0 && floatAt(tail) <= 2048 &&
            floatAt(tail + 1) > 0 && floatAt(tail + 1) <= 2048;
        const std::string key = named ? address + ":" + stringAt(1) : address;
        if (!named && index == state.feedbackLayer) {
            if (!valid || floatAt(tail) > 800 || floatAt(tail + 1) > 600 ||
                std::floor(floatAt(tail)) != floatAt(tail) || std::floor(floatAt(tail + 1)) != floatAt(tail + 1) ||
                !state.feedbackSettings.insert("size").second) return false;
        }
        return valid && addBinding(index, key);
    }
    if (address == "/tex/speed" || address == "/shader/texture/speed") {
        const bool valid = count == tail + 1 && addComponents(1) && recipeNumber(m, tail) &&
            (!recipeType(m, tail, OFXOSC_TYPE_STRING) || expressionAt(tail));
        const std::string key = named ? address + ":" + stringAt(1) : address;
        if (!named && index == state.feedbackLayer &&
            (!recipeType(m, tail, OFXOSC_TYPE_FLOAT) || floatAt(tail) != 1 ||
             !state.feedbackSettings.insert("speed").second)) return false;
        return valid && addBinding(index, key);
    }
    if (address == "/tex/set" || address == "/shader/texture/set") {
        if (count != tail + 2 || !recipeType(m, tail, OFXOSC_TYPE_STRING) ||
            !recipeType(m, tail + 1, OFXOSC_TYPE_INT32)) return false;
        const std::string property = stringAt(tail); const int value = intAt(tail + 1);
        if (!named && index == state.feedbackLayer) {
            const std::map<std::string, int> fixed{{"aspectRatio", 1}, {"blendMode", 0},
                {"internalformat", 32856}, {"textureTarget", 3553}, {"filter", 9729},
                {"minFilter", 9729}, {"magFilter", 9729}, {"noClear", 0}};
            if (property == "static") {
                if (value != 0 && value != 1) return false;
            } else {
                const auto found = fixed.find(property);
                if (found == fixed.end() || value != found->second) return false;
            }
            return state.feedbackSettings.insert(property).second;
        }
        const bool valid = ((property == "aspectRatio" || property == "static") && (value == 0 || value == 1)) ||
            (property == "blendMode" && value >= 0 && value < 6);
        const std::string key = named ? address + ":" + stringAt(1) + ":" + property
                                      : address + ":" + property;
        return valid && addBinding(index, key);
    }
    const std::map<std::string, int> vectors{{"/layer/pos", 3}, {"/layer/rot", 3},
        {"/layer/scale", 3}, {"/layer/pivot", 3}, {"/layer/size", 3}, {"/layer/tint", 4}};
    auto vector = vectors.find(address);
    if (vector != vectors.end()) {
        if (count != vector->second + 1) return false;
        for (int i = 1; i < count; ++i) if (!recipeNumber(m, i)) return false;
        for (int i = 1; i < count; ++i) if (recipeType(m, i, OFXOSC_TYPE_STRING) && !expressionAt(i)) return false;
        if (!addComponents(vector->second)) return false;
        if (address == "/layer/size") {
            for (int i = 1; i <= 2; ++i) if (recipeType(m, i, OFXOSC_TYPE_FLOAT) &&
                (floatAt(i) <= 0 || floatAt(i) > 2048)) return false;
            if (recipeType(m, 3, OFXOSC_TYPE_FLOAT) && floatAt(3) != 0) return false;
        }
        return addBinding(index, address);
    }
    if (address == "/layer/alpha" || address == "/layer/bri" || address == "/layer/speed")
        return count == 2 && addComponents(1) && recipeNumber(m, 1) && addBinding(index, address) &&
            (!recipeType(m, 1, OFXOSC_TYPE_STRING) || expressionAt(1));
    if (address == "/layer/visible") return count == 2 &&
        addComponents(1) && addBinding(index, address) &&
        ((recipeType(m, 1, OFXOSC_TYPE_INT32) && (intAt(1) == 0 || intAt(1) == 1)) ||
        (recipeType(m, 1, OFXOSC_TYPE_STRING) && expressionAt(1)));
    if (address == "/layer/align") return count == 3 && recipeType(m, 1, OFXOSC_TYPE_INT32) &&
        recipeType(m, 2, OFXOSC_TYPE_INT32) && std::set<int>{0,1,2,4}.count(intAt(1)) &&
        std::set<int>{0,16,32,64}.count(intAt(2)) && addBinding(index, address);
    if (address == "/layer/blend") return count == 2 && recipeType(m, 1, OFXOSC_TYPE_INT32) &&
        intAt(1) >= 0 && intAt(1) < 6 && addBinding(index, address);
    return false;
}

bool CommandSystem::validateSceneRecipeCommands(const ofJson& commands, const ofJson& assets, std::string& reason) const {
    if (!commands.is_array() || commands.empty() || commands.size() > 2048) {
        reason = "recipe command array is outside native bounds";
        return false;
    }
    SceneReplay state;
    int sequence = 1;
    for (const auto& command : commands) {
        if (!command.is_object() || command.size() != 2 || !command.count("address") ||
            !command.count("args") || !command["address"].is_string() || !command["args"].is_array()) {
            reason = "recipe command descriptor shape is invalid";
            return false;
        }
        const std::string address = command["address"].get<std::string>();
        if (!recipeAddress(address)) { reason = "recipe address is outside the frozen catalog"; return false; }
        ofxOscMessage wrapped;
        wrapped.setAddress("/scene/command");
        wrapped.addStringArg("validation_token_000000000000");
        wrapped.addIntArg(sequence++);
        wrapped.addStringArg(address);
        for (const auto& arg : command["args"]) {
            if (!arg.is_object() || arg.size() != 2 || !arg.count("type") || !arg.count("value") ||
                !arg["type"].is_string()) { reason = "recipe argument descriptor shape is invalid"; return false; }
            const std::string type = arg["type"].get<std::string>();
            const auto& value = arg["value"];
            if (type == "asset" && value.is_string()) {
                std::string source = value.get<std::string>();
                {
                    auto asset = std::find_if(assets.begin(), assets.end(), [&](const ofJson& item) { return item.value("name", std::string()) == source; });
                    if (asset == assets.end()) { reason = "undeclared recipe asset"; return false; }
                    const auto kind = asset->value("kind", std::string());
                    if (kind == "feedback_fragment") {
                        if (address != "/tex" || wrapped.getNumArgs() != 4) { reason = "feedback asset kind mismatch"; return false; }
                        source = "shader_pp:" + source;
                    } else if ((address == "/shader" && kind != "fragment") || (address != "/shader" && kind != "image")) {
                        reason = "recipe asset kind mismatch"; return false;
                    }
                }
                wrapped.addStringArg(source);
            }
            else if (!scene::appendOscValue(wrapped, type, value)) {
                reason = "recipe argument type/value is invalid"; return false;
            }
        }
        if (!validateSceneCommand(wrapped, state, liveSceneUniforms(wrapped))) {
            reason = "native frozen command validation rejected " + address;
            return false;
        }
        ++state.received;
    }
    return validateFeedbackSettings(state, reason);
}

void CommandSystem::validateStartupScene(const scene::File& file, const ScenePrograms* programs) {
    if (file.version == 2) return; // Already validated by the native-only decoder.
    SceneReplay state;
    for (size_t i = 0; i < file.commands.size(); ++i) {
        const auto& message = file.commands[i].message;
        if (!recipeAddress(message.getArgAsString(2)) || !validateSceneCommand(message, state))
            throw std::runtime_error(scene::commandContext(i, message) +
                                     ": invalid argument types, values, target, or setup order");
        ++state.received;
    }
    std::string reason;
    if (!validateFeedbackSettings(state, reason)) throw std::runtime_error(reason);
    if (!programs) return;

    std::map<int, const Shader::PreparedDirectSource*> byLayer;
    std::set<std::pair<int, std::string>> explicitUniforms;
    std::map<std::string, int> feedbackControls;
    std::optional<size_t> feedbackCommand;
    for (size_t i = 0; i < file.commands.size(); ++i) {
        const auto& message = file.commands[i].message;
        const auto address = message.getArgAsString(2);
        if (address == "/shader") byLayer[message.getArgAsInt32(3)] = &programs->at(i);
        if (address == "/shader/var") explicitUniforms.emplace(message.getArgAsInt32(3), message.getArgAsString(4));
        if (file.commands[i].feedback) feedbackCommand = i;
        if (address == "/tex/var")
            feedbackControls.emplace(message.getArgAsString(4), message.getNumArgs() - 5);
    }
    for (size_t i = 0; i < file.commands.size(); ++i) {
        const auto& message = file.commands[i].message;
        const auto address = message.getArgAsString(2);
        if (address != "/shader/var" && address != "/shader/texture" && address != "/tex") continue;
        const int layer = message.getArgAsInt32(3);
        const auto source = byLayer.find(layer);
        if (source == byLayer.end()) continue;
        const auto& program = *source->second;
        const auto name = address == "/tex" ? DEFAULT_TEX : message.getArgAsString(4);
        auto fail = [&](const std::string& detail) {
            throw std::runtime_error(scene::commandContext(i, message) + ": " + detail);
        };
        if (address != "/shader/var") {
            const bool explicitSize = explicitUniforms.count({layer, name + "Size"});
            const auto error = Shader::sceneTextureError(program.uniforms, name, explicitSize);
            if (!error.empty()) fail(error);
            continue;
        }
        if (const char* error = Shader::sceneUniformError(program.uniforms, name))
            fail(std::string(error) + ": " + name);
        const auto type = program.uniforms.at(name).type;
        const int arity = uniformComponentCount(type);
        const bool integer = integerUniformType(type);
        if (arity == 0 || arity != message.getNumArgs() - 5)
            fail("shader uniform arity or shape mismatch: " + name);
        for (int arg = 5; arg < message.getNumArgs(); ++arg)
            if (message.getArgType(arg) != (integer ? OFXOSC_TYPE_INT32 : OFXOSC_TYPE_FLOAT) &&
                message.getArgType(arg) != OFXOSC_TYPE_STRING)
                fail("shader uniform " + name + (integer ? " requires int32" : " requires float32"));
    }
    if (feedbackCommand && !ShaderPingPongTex::validateUniforms(
            programs->at(*feedbackCommand).uniforms, feedbackControls, reason))
        throw std::runtime_error(scene::commandContext(*feedbackCommand, file.commands[*feedbackCommand].message) +
                                 ": " + reason);
}

#ifdef VOSC_RECOVERY_TESTING
void CommandSystem::traceStartupBoundary(const char* event) {
    if (std::getenv("VOSC_TEST_TRACE_SCENE"))
        std::cerr << "VOSC_TEST_SCENE " << event << std::endl;
}
#endif

CommandSystem::ScenePrograms CommandSystem::prepareStartupScene(const scene::File& file) {
#ifdef VOSC_RECOVERY_TESTING
    traceStartupBoundary("preflight");
#endif
    ScenePrograms programs;
    if (file.version == 2) {
        scene::verifyNativeAssets(file);
        return programs;
    }
    for (size_t i = 0; i < file.commands.size(); ++i) {
        const auto& command = file.commands[i];
        if (command.asset.empty()) continue;
        if (!scene::assetMatches(command))
            throw std::runtime_error(scene::commandContext(i, command.message) +
                                     ": asset changed since preflight: " + command.asset.string());
        if (command.message.getArgAsString(2) == "/shader" || command.feedback) {
            if (!Shader::prepareDirectSource(command.assetBytes, programs[i]))
                throw std::runtime_error(scene::commandContext(i, command.message) +
                                         ": shader compilation or linking failed: " + command.asset.string());
        }
    }
    validateStartupScene(file, &programs);
    return programs;
}

void CommandSystem::applyStartupScene(const scene::File& file, ScenePrograms& programs, SceneResult result) {
    if (file.version == 2) {
        startupSceneResult = std::move(result);
        nativeStartup.emplace(NativeStartup{file});
        auto& startup = *nativeStartup;
        startup.diagnosticStart = replayLogger() ? replayLogger()->latestSequence() : 0;
        resetScene(file.width, file.height);
        while (startup.nextCommand < file.nativeCommands.size()) {
            const auto& command = file.nativeCommands[startup.nextCommand];
            if (command.message.getAddress() == "/tex/set" && command.message.getArgAsString(1) == "numFrames") break;
            applyNativeCommand(command);
            ++startup.nextCommand;
        }
        return;
    }
    for (size_t i = 0; i < file.commands.size(); ++i) {
        const auto& command = file.commands[i];
        if (!command.asset.empty() && !scene::assetMatches(command))
            throw std::runtime_error(scene::commandContext(i, command.message) +
                                     ": asset changed since preflight: " + command.asset.string());
    }
    startupSceneResult = std::move(result);
    sceneReplay.emplace();
    auto& state = *sceneReplay;
    state.width = file.width;
    state.height = file.height;
    state.finalGate = file.onsetGate;
    state.deadline = replayNow() + 12.0;
    state.diagnosticStart = replayLogger() ? replayLogger()->latestSequence() : 0;
    resetScene(file.width, file.height);
    for (size_t i = 0; i < file.commands.size(); ++i) {
        const auto& message = file.commands[i].message;
        auto program = programs.find(i);
        try {
            const auto& command = file.commands[i];
            const auto address = message.getArgAsString(2);
            if (address == "/tex" || address == "/shader/texture") {
#ifdef VOSC_RECOVERY_TESTING
                traceStartupBoundary("command");
#endif
                auto& layer = *renderSystem.getLayers().at(message.getArgAsInt32(3));
                const auto name = address == "/tex" ? DEFAULT_TEX : message.getArgAsString(4);
                const auto size = address == "/tex" ? layer.data.getSize() :
                    glm::vec2(std::max(1, ofGetWidth()), std::max(1, ofGetHeight()));
                auto texture = layer.shader.loadSceneTexture(name, command.asset.string(), command.feedback, size);
                if (command.feedback) {
                    auto feedback = std::dynamic_pointer_cast<ShaderPingPongTex>(texture->tex);
                    if (!feedback) throw std::runtime_error("could not create feedback texture");
                    texture->feedbackSetupPending = true;
                    feedback->replayBounded = true;
                    feedback->installDirectSource(std::move(program->second), {});
                    if (!feedback->prepareFeedback()) throw std::runtime_error("could not prepare feedback texture");
                }
            } else if (!routeSceneCommand(message, program == programs.end() ? nullptr : &program->second)) {
                throw std::runtime_error("command routing failed");
            }
        } catch (const std::exception& error) {
            failSceneReplay(scene::commandContext(i, message) + ": " + error.what());
            return;
        }
    }
    // Apply holder settings before the first upload of a static image.
    for (size_t i = 0; i < file.commands.size(); ++i) {
        const auto& command = file.commands[i];
        if (command.asset.empty()) continue;
        const auto& message = command.message;
        try {
            if (command.feedback) {
                std::string reason;
                if (!finishFeedbackTexture(message.getArgAsInt32(3), reason))
                    throw std::runtime_error(reason);
            } else if (!programs.count(i)) {
                verifyStartupImage(message, command.asset.string());
            }
        } catch (const std::exception& error) {
            failSceneReplay(scene::commandContext(i, message) + ": " + error.what());
            return;
        }
    }
    state.ended = true;
    state.configurationSerial = currentConfigurationSerial ? currentConfigurationSerial() : 0;
}

void CommandSystem::verifyStartupImage(const ofxOscMessage& message, const std::string& path) {
    const auto& shader = renderSystem.getLayers().at(message.getArgAsInt32(3))->shader;
    const auto& bindings = shader.getTextures();
    const auto binding = bindings.find(message.getArgAsString(2) == "/tex" ? DEFAULT_TEX : message.getArgAsString(4));
    auto fail = [&](const std::string& detail) {
        throw std::runtime_error("image upload failed: " + path + ": " + detail);
    };
    if (binding == bindings.end() || !binding->second ||
        !std::dynamic_pointer_cast<ImageTex>(binding->second->tex))
        fail("image texture was not created");
    auto& image = *binding->second;
    const auto channel = replayLogger();
    const auto diagnosticStart = channel ? channel->latestSequence() : 0;
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i) {}
#ifdef VOSC_RECOVERY_TESTING
    ofBufferObject blockedUpload;
    GLint previousUnpack = 0;
    if (std::getenv("VOSC_TEST_FAIL_SCENE_UPLOAD")) {
        // A zero-byte unpack buffer forces the real texture transfer to fail.
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &previousUnpack);
        blockedUpload.allocate(0, GL_STATIC_DRAW);
        blockedUpload.bind(GL_PIXEL_UNPACK_BUFFER);
        if (!blockedUpload.isAllocated() || glGetError() != GL_NO_ERROR)
            throw std::runtime_error("could not arm test upload failure");
    }
#endif
    image.update({});
#ifdef VOSC_RECOVERY_TESTING
    if (blockedUpload.isAllocated()) glBindBuffer(GL_PIXEL_UNPACK_BUFFER, previousUnpack);
#endif
    const auto uploadError = glGetError();
    if (uploadError != GL_NO_ERROR) fail("OpenGL error " + std::to_string(uploadError));
    const bool usable = hasImageStorage(image, image.getPixels());
    if (glGetError() != GL_NO_ERROR || !usable) fail("no usable GPU texture storage");
    if (channel) {
        bool gap = false;
        uint64_t latest = 0;
        const auto entries = channel->entriesSince(diagnosticStart, 512, gap, &latest);
        if (gap || (!entries.empty() && entries.back().sequence != latest))
            fail("upload diagnostic scope overflowed");
        for (const auto& entry : entries)
            if (entry.level == OF_LOG_ERROR || entry.level == OF_LOG_FATAL_ERROR)
                fail(entry.message.substr(0, 440));
    }
}

bool CommandSystem::routeSceneCommand(const ofxOscMessage& wrapped, Shader::PreparedDirectSource* program) {
#ifdef VOSC_RECOVERY_TESTING
    traceStartupBoundary("command");
#endif
    ofxOscMessage ordinary;
    const std::string address = wrapped.getArgAsString(2);
    if (program && address == "/shader") {
        renderSystem.getLayers().at(wrapped.getArgAsInt32(3))->shader.installDirectSource(std::move(*program), {});
        return true;
    }
    if (address == "/tex/var") {
        ShaderPingPongTex::invalidateFeedbackGeneration();
        auto& holder = *renderSystem.getLayers().at(wrapped.getArgAsInt32(3))->shader.getDefaultTexture();
        const std::string name = wrapped.getArgAsString(4);
        const int count = wrapped.getNumArgs() - 5;
        if (count == 1) holder.setVar(name, wrapped.getArgAsFloat(5));
        else if (count == 2) {
            auto value = std::make_shared<Variable<glm::vec2>>();
            value->set(glm::vec2(wrapped.getArgAsFloat(5), wrapped.getArgAsFloat(6)));
            VariablePool::getPool(&holder)[name] = value;
            holder.vars[name] = value;
        }
        else if (count == 3) holder.setVar(name, glm::vec3(wrapped.getArgAsFloat(5), wrapped.getArgAsFloat(6), wrapped.getArgAsFloat(7)));
        else if (count == 4) holder.setVar(name, ofFloatColor(wrapped.getArgAsFloat(5), wrapped.getArgAsFloat(6), wrapped.getArgAsFloat(7), wrapped.getArgAsFloat(8)));
        else return false;
        return true;
    }
    if (address == "/shader/var" && wrapped.getNumArgs() > 5) {
        const int layer = wrapped.getArgAsInt32(3);
        const std::string name = wrapped.getArgAsString(4);
        auto& shader = renderSystem.getLayers().at(layer)->shader;
        if (const auto arity = shader.reflectedFloatVectorArity(name)) {
            vector<Shader::FloatComponent> components;
            components.reserve(arity);
            for (int index = 5; index < wrapped.getNumArgs(); ++index) {
                if (wrapped.getArgType(index) == OFXOSC_TYPE_FLOAT)
                    components.emplace_back(wrapped.getArgAsFloat(index));
                else if (wrapped.getArgType(index) == OFXOSC_TYPE_STRING)
                    components.emplace_back(wrapped.getArgAsString(index));
                else
                    return false;
            }
            try {
                shader.setFloatVector(name, arity, components);
            } catch (const std::invalid_argument& error) {
                ofLogError() << address << " rejected layer " << layer << " uniform " << name
                             << ": " << error.what();
                return false;
            }
            return true;
        }
        const auto& uniforms = shader.getUniforms();
        const auto reflected = uniforms.find(name);
        if (reflected != uniforms.end() &&
            (reflected->second.type == GL_FLOAT_VEC2 || reflected->second.type == GL_FLOAT_VEC3 ||
             reflected->second.type == GL_FLOAT_VEC4)) {
            const int components = uniformComponentCount(reflected->second.type);
            bool hasExpression = false;
            for (int index = 5; index < wrapped.getNumArgs(); ++index)
                hasExpression = hasExpression || wrapped.getArgType(index) == OFXOSC_TYPE_STRING;
            if (components == wrapped.getNumArgs() - 5 && hasExpression) {
                if (reflected->second.type == GL_FLOAT_VEC2)
                    setMixedComponents<glm::vec2>(shader, name, wrapped, 5, components);
                else if (reflected->second.type == GL_FLOAT_VEC3)
                    setMixedComponents<glm::vec3>(shader, name, wrapped, 5, components);
                else if (reflected->second.type == GL_FLOAT_VEC4)
                    setMixedComponents<ofFloatColor>(shader, name, wrapped, 5, components);
                return true;
            }
            if (reflected->second.type == GL_FLOAT_VEC4 && components == wrapped.getNumArgs() - 5) {
                auto variable = VariablePool::getOrCreate<ofFloatColor>(name, &shader);
                variable->set(ofFloatColor(wrapped.getArgAsFloat(5), wrapped.getArgAsFloat(6),
                                           wrapped.getArgAsFloat(7), wrapped.getArgAsFloat(8)));
                shader.vars[name] = variable;
                return true;
            }
        }
    }
    if (address == "/shader/var" && wrapped.getNumArgs() > 5) {
        const int layer = wrapped.getArgAsInt32(3);
        const std::string name = wrapped.getArgAsString(4);
        auto& shader = renderSystem.getLayers().at(layer)->shader;
        const auto& uniformTypes = shader.getUniforms();
        const auto reflected = uniformTypes.find(name);
        if (const auto arity = shader.reflectedIntegerVectorArity(name)) {
            vector<Shader::IntegerComponent> components;
            components.reserve(arity);
            for (int index = 5; index < wrapped.getNumArgs(); ++index) {
                if (wrapped.getArgType(index) == OFXOSC_TYPE_INT32)
                    components.emplace_back(wrapped.getArgAsInt32(index));
                else if (wrapped.getArgType(index) == OFXOSC_TYPE_STRING)
                    components.emplace_back(wrapped.getArgAsString(index));
                else
                    return false;
            }
            shader.setIntegerVector(name, arity, components);
            return true;
        }
        // Preserve the legacy reflected scalar and GLSL array applications.
        if (reflected != uniformTypes.end() && integerUniformType(reflected->second.type)) {
            bool hasExpression = false;
            for (int index = 5; index < wrapped.getNumArgs(); ++index)
                hasExpression = hasExpression || wrapped.getArgType(index) == OFXOSC_TYPE_STRING;
            if (hasExpression) {
                std::vector<std::string> values;
                std::vector<bool> expressionMask;
                for (int index = 5; index < wrapped.getNumArgs(); ++index) {
                    if (wrapped.getArgType(index) == OFXOSC_TYPE_STRING) {
                        values.push_back(wrapped.getArgAsString(index));
                        expressionMask.push_back(true);
                    } else if (wrapped.getArgType(index) == OFXOSC_TYPE_INT32) {
                        values.push_back(ofToString(wrapped.getArgAsInt32(index)));
                        expressionMask.push_back(false);
                    } else {
                        values.push_back(ofToString(static_cast<int>(wrapped.getArgAsFloat(index))));
                        expressionMask.push_back(false);
                    }
                }
                auto variable = VariablePool::getOrCreate<int>(name, &shader);
                variable->set(values, expressionMask);
                shader.vars[name] = variable;
            } else {
                std::vector<int> values;
                for (int index = 5; index < wrapped.getNumArgs(); ++index) {
                    if (wrapped.getArgType(index) == OFXOSC_TYPE_INT32)
                        values.push_back(wrapped.getArgAsInt32(index));
                    else
                        values.push_back(static_cast<int>(wrapped.getArgAsFloat(index)));
                }
                shader.setVar<int>(name, values);
            }
            return true;
        }
    }
    if (address == "/shader/var" && wrapped.getNumArgs() > 6) {
        bool allIntegers = true;
        for (int index = 5; index < wrapped.getNumArgs(); ++index)
            allIntegers = allIntegers && wrapped.getArgType(index) == OFXOSC_TYPE_INT32;
        if (allIntegers) {
            const int layer = wrapped.getArgAsInt32(3);
            std::vector<int> values;
            for (int index = 5; index < wrapped.getNumArgs(); ++index) values.push_back(wrapped.getArgAsInt32(index));
            renderSystem.getLayers().at(layer)->shader.setVar<int>(wrapped.getArgAsString(4), values);
            return true;
        }
    }
    if (address == "/layer/pos") {
        Layer::Position components;
        for (size_t index = 0; index < components.size(); ++index) {
            const auto argument = index + 4;
            if (wrapped.getArgType(argument) == OFXOSC_TYPE_STRING)
                components[index] = wrapped.getArgAsString(argument);
            else
                components[index] = wrapped.getArgAsFloat(argument);
        }
        renderSystem.getLayers().at(wrapped.getArgAsInt32(3))->setPosition(components);
        return true;
    }
    if (address == "/layer/tint" || address == "/layer/rot" ||
        address == "/layer/scale" || address == "/layer/pivot" || address == "/layer/size") {
        bool hasExpression = false;
        for (int index = 4; index < wrapped.getNumArgs(); ++index)
            hasExpression = hasExpression || wrapped.getArgType(index) == OFXOSC_TYPE_STRING;
        if (hasExpression) {
            auto& layer = *renderSystem.getLayers().at(wrapped.getArgAsInt32(3));
            if (address == "/layer/tint") {
                setMixedComponents<ofFloatColor>(layer, "tint", wrapped, 4, 4);
            } else {
                const std::map<std::string, std::string> names{{"/layer/rot", "rotation"}, {"/layer/scale", "scale"},
                    {"/layer/pivot", "pivot"}, {"/layer/size", "size"}};
                setMixedComponents<glm::vec3>(layer, names.at(address), wrapped, 4, 3);
            }
            return true;
        }
    }
    ordinary.setAddress(address);
    ordinary.setRemoteEndpoint(wrapped.getRemoteHost(), wrapped.getRemotePort());
    static const std::set<std::string> flattenedVectors = {
        "/layer/rot", "/layer/scale", "/layer/pivot", "/layer/size", "/layer/tint"};
    if (flattenedVectors.count(address)) {
        ordinary.addIntArg(wrapped.getArgAsInt32(3));
        ofJson components = ofJson::array();
        for (int index = 4; index < wrapped.getNumArgs(); ++index) {
            if (wrapped.getArgType(index) == OFXOSC_TYPE_FLOAT) components.push_back(wrapped.getArgAsFloat(index));
            else components.push_back(wrapped.getArgAsString(index));
        }
        ordinary.addStringArg(components.dump());
    } else if (address == "/shader/var" && wrapped.getNumArgs() > 6) {
        ordinary.addIntArg(wrapped.getArgAsInt32(3));
        ordinary.addStringArg(wrapped.getArgAsString(4));
        ofJson components = ofJson::array();
        for (int index = 5; index < wrapped.getNumArgs(); ++index) {
            if (wrapped.getArgType(index) == OFXOSC_TYPE_INT32) components.push_back(wrapped.getArgAsInt32(index));
            else if (wrapped.getArgType(index) == OFXOSC_TYPE_FLOAT) components.push_back(wrapped.getArgAsFloat(index));
            else components.push_back(wrapped.getArgAsString(index));
        }
        ordinary.addStringArg(components.dump());
    } else {
    for (int index = 3; index < wrapped.getNumArgs(); ++index) {
        switch (wrapped.getArgType(index)) {
            case OFXOSC_TYPE_INT32: ordinary.addIntArg(wrapped.getArgAsInt32(index)); break;
            case OFXOSC_TYPE_FLOAT: ordinary.addFloatArg(wrapped.getArgAsFloat(index)); break;
            case OFXOSC_TYPE_STRING: ordinary.addStringArg(wrapped.getArgAsString(index)); break;
            default: return false;
        }
    }
    }
    auto parsed = commandParser.parse(ordinary);
    if (!parsed.isOk()) return false;
    if (!commandRouter.route(parsed.value())) return false;
    if (address == "/tex" && wrapped.getArgAsString(4).rfind("shader_pp:", 0) == 0) {
        auto& texture = *renderSystem.getLayers().at(wrapped.getArgAsInt32(3))->shader.getDefaultTexture();
        texture.feedbackSetupPending = true;
        auto feedback = std::dynamic_pointer_cast<ShaderPingPongTex>(texture.tex);
        if (!feedback) return false;
        feedback->replayBounded = true;
        return feedback->prepareFeedback();
    }
    return true;
}

bool CommandSystem::validateFeedbackSettings(const SceneReplay& state, std::string& reason) {
    if (state.feedbackLayer < 0) return true;
    static const std::set<std::string> required = {"size", "speed", "aspectRatio", "blendMode",
        "internalformat", "textureTarget", "static"};
    bool complete = true;
    for (const auto& field : required) complete = complete && state.feedbackSettings.count(field);
    const bool filter = state.feedbackSettings.count("filter");
    const bool splitFilter = state.feedbackSettings.count("minFilter") && state.feedbackSettings.count("magFilter");
    if (!complete || (filter == splitFilter) ||
        (filter && (state.feedbackSettings.count("minFilter") || state.feedbackSettings.count("magFilter")))) {
        reason = "feedback setup requires every bounded storage and holder setting"; return false;
    }
    return true;
}

bool CommandSystem::finishFeedbackSetup(const SceneReplay& state, std::string& reason) const {
    return validateFeedbackSettings(state, reason) &&
           (state.feedbackLayer < 0 || finishFeedbackTexture(state.feedbackLayer, reason));
}

bool CommandSystem::finishFeedbackTexture(int layer, std::string& reason) const {
    auto& texture = *renderSystem.getLayers().at(layer)->shader.getDefaultTexture();
    auto feedback = std::dynamic_pointer_cast<ShaderPingPongTex>(texture.tex);
    if (!feedback || !feedback->prepareFeedback()) {
        reason = "feedback program could not be prepared";
        return false;
    }
    if (!feedback->validateConfiguration(texture.data, reason)) return false;
    if (texture.numFrames != 1 || !texture.passes.empty() || texture.hasLooper() ||
        !feedback->getTextures().empty() || !feedback->getBuffers().empty() || !feedback->vars.empty()) {
        reason = "feedback auxiliary resources are unsupported"; return false;
    }
    texture.feedbackSetupPending = false;
    texture.needsUpdate = true;
    return true;
}

void CommandSystem::resetScene(int width, int height) {
#ifdef VOSC_RECOVERY_TESTING
    traceStartupBoundary("reset");
#endif
    messageQueue.clear();
    runtime.tidal->notes.clear();
    osc::LayersPayload empty;
    empty.hasLayerCount = empty.hasLayout = true;
    empty.layerCount = 0;
    empty.layout = Layout::STACK;
    renderSystem.resetLayers(empty);
    renderSystem.resetScenePipeline();
    Lights::get().clear();
    if (auto* registry = ResourceRegistry::current()) {
        registry->shaderSharedPool.clear();
        registry->textureShaderPool.clear();
        registry->textureSharedPool.clear();
        registry->geomLayerPool.clear();
        registry->geomSharedPool.clear();
    }
    VariablePool::clearSceneShared();
    runtime.inputs.clear();
    runtime.camera.resetScene();
    waitOnset = 0;
    forceOnset = false;
    ofSetWindowShape(width, height);
}

void CommandSystem::sendSceneReply(const std::string& host, int port, const std::string& token,
                                   const std::string& status, int renderedFrame,
                                   const std::string& detail) const {
    size_t bytes = std::min<size_t>(detail.size(), 512);
    while (bytes > 0 && bytes < detail.size() &&
           (static_cast<unsigned char>(detail[bytes]) & 0xC0) == 0x80) --bytes;
    const std::string boundedDetail = detail.substr(0, bytes);

    in_addr numericAddress{};
    if (inet_pton(AF_INET, host.c_str(), &numericAddress) != 1) return;
    try {
        char buffer[2048];
        osc::OutboundPacketStream packet(buffer, sizeof(buffer));
        packet << osc::BeginMessage("/scene/replay/result")
               << token.c_str() << status.c_str() << static_cast<osc::int32>(renderedFrame)
               << boundedDetail.c_str() << osc::EndMessage;
        osc::UdpTransmitSocket socket(
            osc::IpEndpointName(ntohl(numericAddress.s_addr), port));
        socket.Send(packet.Data(), packet.Size());
    } catch (const std::exception&) {
        // Replay replies are best-effort UDP. Avoid an unscoped log that could poison
        // another token's diagnostic interval when a destination disappears.
    }
}

void CommandSystem::failSceneReplay(const std::string& detail) {
    if (!sceneReplay) return;
    completeSceneReplay("failed", -1, detail);
}

void CommandSystem::completeSceneReplay(const std::string& status, int frame, const std::string& detail) {
    const auto replay = std::move(*sceneReplay);
    sceneReplay.reset();
    if (startupSceneResult) {
        auto result = std::move(startupSceneResult);
        startupSceneResult = nullptr;
        result(frame, detail);
    } else {
        sendSceneReply(replay.host, replay.replyPort, replay.token, status, frame, detail);
    }
}

void CommandSystem::applyNativeCommand(const scene::NativeCommand& command) {
    auto& startup = *nativeStartup;
    const auto& message = command.message;
    const auto& address = message.getAddress();
    auto fail = [&](const std::string& reason) {
        throw std::runtime_error("command " + std::to_string(startup.nextCommand + 1) + " " + address + ": " + reason);
    };
    auto& layers = renderSystem.getLayers();
    auto targetTexture = [&]() {
        if (message.getArgType(0) == OFXOSC_TYPE_STRING)
            return TexturePool::getShared(message.getArgAsString(0));
        return layers.at(message.getArgAsInt32(0))->shader.getDefaultTexture();
    };
    auto stages = [&]() {
        ShaderPaths paths;
        paths.fragPath = command.stages.at("fragment").string();
        paths.vertPath = command.stages.count("vertex") ? command.stages.at("vertex").string() : "shaders/passthru.vert";
        if (command.stages.count("geometry")) paths.geomPath = command.stages.at("geometry").string();
        return paths;
    };
    if (address == "/shader") {
        auto& shader = layers.at(message.getArgAsInt32(0))->shader;
        if (!shader.loadNativeStages(stages())) fail("shader compilation or linking failed");
        return;
    }
    if (address == "/shader/set") {
        // Geometry shader topology is part of its GLSL layout. Validate the
        // explicit startup declaration against the linked program after setup.
        return;
    }
    if (address == "/shader/var") {
        const auto& shader = layers.at(message.getArgAsInt32(0))->shader;
        const auto name = message.getArgAsString(1);
        const auto& uniforms = shader.getUniforms();
        const auto found = uniforms.find(name);
        if (const char* error = Shader::sceneUniformError(uniforms, name))
            fail(std::string(error) + ": " + name);
        if (uniformComponentCount(found->second.type) != message.getNumArgs() - 2)
            fail("uniform type or width mismatch: " + name);
        const auto type = integerUniformType(found->second.type) ? OFXOSC_TYPE_INT32 : OFXOSC_TYPE_FLOAT;
        for (int i = 2; i < message.getNumArgs(); ++i)
            if (message.getArgType(i) != type) fail("uniform scalar type mismatch: " + name);
        ofxOscMessage wrapped;
        wrapped.addStringArg("");
        wrapped.addInt32Arg(0);
        wrapped.addStringArg(address);
        wrapped.addInt32Arg(message.getArgAsInt32(0));
        wrapped.addStringArg(name);
        for (int i = 2; i < message.getNumArgs(); ++i) {
            if (message.getArgType(i) == OFXOSC_TYPE_INT32) wrapped.addInt32Arg(message.getArgAsInt32(i));
            else wrapped.addFloatArg(message.getArgAsFloat(i));
        }
        if (!routeSceneCommand(wrapped)) fail("uniform routing failed");
        return;
    }
    if (address == "/shader/buffer") {
        const auto& shader = layers.at(message.getArgAsInt32(0))->shader;
        const auto name = message.getArgAsString(1);
        const auto& uniforms = shader.getUniforms();
        const auto binding = uniforms.find(name);
        if (binding == uniforms.end() || binding->second.type != GL_SAMPLER_BUFFER ||
            !VariablePool::getShared(message.getArgAsString(2)))
            fail("shader buffer binding mismatch: " + name);
    }
    if (address == "/tex/set" && message.getArgAsString(1) == "numFrames") {
        const auto texture = targetTexture();
        if (!texture || texture != startup.media) fail("history requires the ready shared camera");
        const auto& storage = texture->data.getFboSettings();
        // Bound conservatively at RGBA8 plus a depth/stencil reservation and
        // a 256-byte row alignment. The default history needs one color FBO.
        const size_t pitch = (static_cast<size_t>(std::max(storage.width, 0)) * 8 + 255) & ~size_t(255);
        if (storage.width <= 0 || storage.width > 640 || storage.height <= 0 || storage.height > 480 ||
            storage.numSamples != 0 || storage.numColorbuffers != 1 || !storage.colorFormats.empty() ||
            pitch * storage.height * 120 > 300u * 1024u * 1024u)
            fail("camera history exceeds bounded storage");
    }

    const auto parsed = commandParser.parse(message);
    if (!parsed.isOk() || !routeLegacyCommand(parsed.value())) fail("command routing failed");

    if (address == "/tex" && !command.sourceKind.empty()) {
        const auto texture = targetTexture();
        if (!texture || !texture->tex) fail("texture source could not be created");
        const bool isMedia = command.sourceKind == "video" || command.sourceKind == "camera" || command.sourceKind == "html";
        if (isMedia) {
            startup.media = texture;
#ifdef VOSC_RECOVERY_TESTING
            if (std::getenv("VOSC_TEST_NATIVE_STALL_OPEN")) {
                std::cerr << "VOSC_TEST_NATIVE stall-open" << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(30));
            }
#endif
        } else {
            if (auto* shader = dynamic_cast<Shader*>(texture->tex.get()); shader && !command.stages.empty()) {
                if (!shader->loadNativeStages(stages())) fail("texture shader compilation or linking failed");
            }
            startup.textures.push_back(texture);
        }
    } else if (address == "/geom") {
        if (message.getArgType(0) == OFXOSC_TYPE_INT32) {
            if (!layers.at(message.getArgAsInt32(0))->hasGeom()) fail("geometry could not be created");
        } else if (message.getArgAsString(0) == "*") {
            for (const auto& layer : layers)
                if (!layer->hasGeom()) fail("geometry could not be created");
        } else {
            auto geom = GeomPool::getShared(message.getArgAsString(0));
            if (!geom || !geom->isLoaded()) fail("model could not be loaded");
        }
    } else if (address == "/tex/set" && message.getArgAsString(1) == "wrapMode") {
        // Image storage has to exist before setting wrap. The OSC handler
        // updates both sampler state and the holder's creation settings.
        auto texture = targetTexture();
        if (texture && std::dynamic_pointer_cast<ImageTex>(texture->tex)) {
            texture->update({});
            if (!texture->hasTexture(0)) fail("image upload failed before configuring wrap");
            texture->getTexture().setTextureWrap(message.getArgAsInt32(2), message.getArgAsInt32(2));
        }
    }
}

void CommandSystem::finishNativeStartup(int frame, const std::string& detail) {
    if (frame >= 0) waitOnset = nativeStartup->file.onsetGate;
    nativeStartup.reset();
    auto callback = std::move(startupSceneResult);
    startupSceneResult = nullptr;
    callback(frame, detail);
}

void CommandSystem::afterNativeDraw() {
    auto& startup = *nativeStartup;
    try {
        if (startup.media) {
            const auto& media = *startup.media->tex;
            if (const auto error = media.startupFailure(); !error.empty())
                throw std::runtime_error("scene input " + startup.file.mediaInput + ": " + error);
            if (!media.startupFrameReady()) return;
#ifdef VOSC_RECOVERY_TESTING
            if (std::getenv("VOSC_TEST_NATIVE_NO_FRAME")) {
                if (!startup.suppressedMediaFrame) {
                    std::cerr << "VOSC_TEST_NATIVE no-frame" << std::endl;
                    startup.suppressedMediaFrame = true;
                }
                return;
            }
#endif
        }
        if (startup.nextCommand < startup.file.nativeCommands.size()) {
            while (startup.nextCommand < startup.file.nativeCommands.size()) {
                applyNativeCommand(startup.file.nativeCommands[startup.nextCommand]);
                ++startup.nextCommand;
            }
            return; // History and new bindings still need an update and draw.
        }
        if (const auto log = replayLogger()) {
            bool gap = false;
            uint64_t latest = 0;
            const auto entries = log->entriesSince(startup.diagnosticStart, 512, gap, &latest);
            if (gap || (!entries.empty() && entries.back().sequence != latest))
                throw std::runtime_error("native startup diagnostics overflowed before composition");
            for (const auto& entry : entries)
                if (entry.level == OF_LOG_ERROR || entry.level == OF_LOG_FATAL_ERROR)
                    throw std::runtime_error("renderer reported an error: " + entry.message.substr(0, 440));
        }
        for (const auto& texture : startup.textures)
            if (!texture->hasTexture(0)) throw std::runtime_error("native texture allocation failed");
        if (startup.media) {
            bool usedMedia = false;
            for (const auto& layer : renderSystem.getLayers()) {
                if (layer->shader.getDefaultTexture() != startup.media) continue;
                usedMedia = true;
                if (layer->delay && !startup.media->hasTexture(layer->delay)) return;
            }
            if (!usedMedia) throw std::runtime_error("scene input has no composed layer");
        }
        for (const auto& command : startup.file.nativeCommands) {
            const auto& message = command.message;
            if (message.getAddress() != "/shader/set") continue;
            auto& shader = renderSystem.getLayers().at(message.getArgAsInt32(0))->shader;
            if (!shader.isShaderLoaded()) throw std::runtime_error("geometry stage is not linked");
            const auto name = message.getArgAsString(1);
            const GLenum pname = name == "geomInputType" ? GL_GEOMETRY_INPUT_TYPE :
                name == "geomOutputType" ? GL_GEOMETRY_OUTPUT_TYPE : GL_GEOMETRY_VERTICES_OUT;
            GLint actual = -1;
            glGetProgramiv(shader.getShader().getProgram(), pname, &actual);
            if (actual != message.getArgAsInt32(2)) throw std::runtime_error("linked geometry topology differs from manifest");
        }
        const auto& file = startup.file;
        if (ofGetWidth() != file.width || ofGetHeight() != file.height ||
            replayFramebufferSize() != std::pair(file.width, file.height))
            throw std::runtime_error("window dimensions did not settle before draw");
        scene::verifyNativeAssets(file);
    } catch (const std::exception& error) {
        finishNativeStartup(-1, error.what());
        return;
    }
    finishNativeStartup(static_cast<int>(ofGetFrameNum()), "native v2 scene composed");
}

void CommandSystem::afterComposedDraw() {
    if (nativeStartup) {
        afterNativeDraw();
        return;
    }
    if (!sceneReplay || !sceneReplay->ended) return;
    const auto replay = *sceneReplay;
    const auto framebuffer = replayFramebufferSize();
    if (ofGetWidth() != replay.width || ofGetHeight() != replay.height ||
        framebuffer.first != replay.width || framebuffer.second != replay.height) {
        failSceneReplay("window dimensions did not settle before draw");
        return;
    }
    if (currentConfigurationSerial && currentConfigurationSerial() != replay.configurationSerial) {
        failSceneReplay("configuration was superseded before the qualifying draw");
        return;
    }
    for (const auto& item : runtime.inputs.all()) if (!item.second || !item.second->configuredResourceReady()) {
        failSceneReplay("input resource did not settle before draw");
        return;
    }
    const int maxImageSide = startupSceneResult ? scene::MaxImageSide : 2048;
    for (const auto& layer : renderSystem.getLayers()) {
        if (!layer) { failSceneReplay("layer resource is missing before draw"); return; }
        const glm::vec3 size = layer->getVarVec3("size");
        if (!std::isfinite(size.x) || !std::isfinite(size.y) || !std::isfinite(size.z) ||
            size.x <= 0 || size.x > 2048 || size.y <= 0 || size.y > 2048 || size.z != 0) {
            failSceneReplay("layer size expression did not settle to a valid 2D size"); return;
        }
        for (const auto& binding : layer->shader.getTextures()) {
            const auto& texture = binding.second;
            if (texture) if (auto feedback = std::dynamic_pointer_cast<ShaderPingPongTex>(texture->tex)) {
                if (texture->feedbackSetupPending || !feedback->firstUpdateComplete() || !feedback->fbo.isAllocated() ||
                    feedback->successfulUpdateGeneration() != ShaderPingPongTex::feedbackGeneration()) {
                    failSceneReplay("feedback allocation or first update did not settle before draw"); return;
                }
                continue;
            }
            if (!texture || !texture->isLoaded() || !texture->getPixels().isAllocated() ||
                texture->getPixels().getWidth() <= 0 || texture->getPixels().getWidth() > maxImageSide ||
                texture->getPixels().getHeight() <= 0 || texture->getPixels().getHeight() > maxImageSide ||
                texture->data.getSize().x <= 0 || texture->data.getSize().y <= 0 ||
                texture->data.getSize().x > maxImageSide || texture->data.getSize().y > maxImageSide) {
                failSceneReplay("image resource did not settle before draw"); return;
            }
        }
    }
    if (auto channel = replayLogger()) {
        bool gap = false;
        uint64_t latest = 0;
        const auto entries = channel->entriesSince(replay.diagnosticStart, 512, gap, &latest);
        if (gap || (!entries.empty() && entries.back().sequence != latest)) {
            failSceneReplay("diagnostic scope overflowed before draw");
            return;
        }
        for (const auto& entry : entries) if (entry.level == OF_LOG_ERROR || entry.level == OF_LOG_FATAL_ERROR) {
            failSceneReplay("renderer reported an error: " + entry.message.substr(0, 440));
            return;
        }
    }
    waitOnset = replay.finalGate;
    completeSceneReplay("applied", static_cast<int>(ofGetFrameNum()),
                        "setup processed and composed draw completed");
}

bool CommandSystem::handleReplayWindowResized(int width, int height) {
    if (nativeStartup) {
        if (width != nativeStartup->file.width || height != nativeStartup->file.height) return false;
        renderSystem.resizeBacking(width, height);
        return true;
    }
    if (!sceneReplay || width != sceneReplay->width || height != sceneReplay->height) return false;
    // A native callback caused by this replay's own reset must not run the
    // ordinary external-resize layout over the explicit transforms that may
    // already have been replayed.
    renderSystem.resizeBacking(width, height);
    return true;
}

void CommandSystem::processQueue() {
    while (!messageQueue.empty()) {
        const osc::Command& command = messageQueue.front();
#ifdef VOSC_MINIMAL_BUILD
        if (!excludedTextureBackend(command, false).empty()) {
            const auto feature = excludedTextureBackend(command, true);
            if (!feature.empty()) {
                logExcludedFeature(command.raw, feature);
                messageQueue.pop_front();
                continue;
            }
            if (sceneReplay) failSceneReplay("foreign control superseded replay before draw");
            if (onBeforeLegacyControl) onBeforeLegacyControl(false);
        }
#endif
        if (routeLegacyCommand(command) && onLegacyControl) onLegacyControl();
        messageQueue.pop_front();
    }
}

bool CommandSystem::hasCommandRegistry(const ofxOscMessage& message) const {
    if (ResourceRegistry::hasCurrent()) return true;
    ofLogError() << message.getAddress() << " rejected: no active ResourceRegistry";
    return false;
}

bool CommandSystem::routeLegacyCommand(const osc::Command& command) {
    if (!hasCommandRegistry(command.raw)) return false;
    try {
        if (commandRouter.route(command)) return true;
        invalidCommand(command.raw);
    } catch (const ResourceRegistry::Inactive& error) {
        ofLogError() << command.raw.getAddress() << " rejected: " << error.what();
    }
    return false;
}

void CommandSystem::handleMidi(const osc::Command& command) {
    if (command.midiAction == osc::MidiAction::LIST_PORTS) {
        inputSystem.listMidiPorts();
    }
}

void CommandSystem::routeTargetedResource(const osc::Command& command) {
    const ofxOscMessage& m = command.raw;
    const string& commandPath = command.resource.commandPath;
    const osc::TargetSelector& target = command.resource.target;

    if (target.kind == osc::TargetKind::ALL) {
        renderSystem.allLayersCommand(commandPath, m);
        return;
    }

    if (target.kind == osc::TargetKind::INDEX) {
        renderSystem.routeLayerByIndex(target.index, commandPath, m, command.resource.action);
        return;
    }

    if (target.kind == osc::TargetKind::NAME) {
        const string& which = target.name;
        switch (command.resource.domain) {
            case osc::ResourceDomain::TEX: {
                shared_ptr<Texture> tex = TexturePool::getShared(which, true);
                if (tex == nullptr) {
                    break;
                }
                tex->oscCommand(commandPath, m);
                const glm::vec2& size = tex->data.getSize();
                if (size.x == 0 && size.y == 0) {
                    tex->data.setSize(ofGetScreenWidth(), ofGetScreenHeight());
                }
                return;
            }
            case osc::ResourceDomain::VAR:
                VariablePool::createOrUpdateShared(which, m, 1);
                return;
            case osc::ResourceDomain::GEOM: {
                shared_ptr<Geom> geom = GeomPool::getShared(which, true);
                if (geom != nullptr) {
                    geom->oscCommand(commandPath, m);
                }
                return;
            }
            case osc::ResourceDomain::SHADER: {
                shared_ptr<Shader> shader = ShaderPool::getShared(which, true);
                if (shader != nullptr) {
                    shader->oscCommand(commandPath, m);
                }
                return;
            }
            default:
                break;
        }
    }

    invalidCommand(m);
}

void CommandSystem::invalidCommand(const ofxOscMessage& m) const {
    ofLogError() << "command not recognized: " << m;
    for (int i = 0; i < m.getNumArgs(); i++) {
        if (m.getArgType(i) == OFXOSC_TYPE_BLOB) {
            ofLog() << m.getArgAsBlob(i);
        }
    }
}

void CommandSystem::invalidCommand(const osc::ParseError& error) const {
    ofLogError() << "invalid command: " << error.address << " - " << error.message;
}
