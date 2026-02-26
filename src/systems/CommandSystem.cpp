#include "CommandSystem.hpp"
#include "../GeomPool.h"
#include "../light/Lights.h"
#include "../TexturePool.h"
#include "../shader/ShaderPool.h"
#include "../var/VariablePool.h"

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
    inputSystem.forEachOscMessage([this](const ofxOscMessage& m) {
        parseMessage(m);
    });

    if (waitOnset < 1 || forceOnset || checkOnset()) {
        processQueue();
        forceOnset = false;
    }
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
    auto parsed = commandParser.parse(m);
    if (!parsed.isOk()) {
        invalidCommand(parsed.error());
        return;
    }

    const osc::Command& command = parsed.value();
    if (isQueuedCommand(command.type)) {
        messageQueue.push_back(command);
        return;
    }

    if (!commandRouter.route(command)) {
        invalidCommand(command.raw);
    }
}

void CommandSystem::processQueue() {
    while (!messageQueue.empty()) {
        const osc::Command& command = messageQueue.front();
        if (!commandRouter.route(command)) {
            invalidCommand(command.raw);
        }
        messageQueue.pop_front();
    }
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
