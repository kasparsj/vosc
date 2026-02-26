#pragma once

#include "ofxOsc.h"
#include "Config.h"
#include <vector>

namespace osc {

enum class CommandType {
    UNKNOWN = 0,
    INPUT,
    MIDI,
    DIRT_PLAY,
    ONSET,
    ONSET_FORCE,
    LAYERS,
    LAYERS_RESET,
    LAYERS_LAYOUT,
    CAMERA,
    LIGHT,
    LIGHT_REMOVE,
    SHADING_MODE,
    SHADING_PASSES,
    TARGETED_RESOURCE,
};

enum class TargetKind {
    NONE = 0,
    ALL,
    INDEX,
    NAME,
};

enum class ResourceDomain {
    UNKNOWN = 0,
    TEX,
    VAR,
    GEOM,
    SHADER,
    LAYER,
};

enum class ResourceAction {
    FORWARD = 0,
    LAYER_SOLO,
};

enum class MidiAction {
    FORWARD = 0,
    LIST_PORTS,
};

enum class InputAction {
    FORWARD = 0,
    DATA,
    LIST,
};

struct TargetSelector {
    TargetKind kind;
    int index;
    string name;

    TargetSelector()
        : kind(TargetKind::NONE), index(-1), name("") {}
};

struct LayersPayload {
    bool hasLayerCount;
    int layerCount;
    bool hasLayout;
    Layout layout;

    LayersPayload()
        : hasLayerCount(false), layerCount(0), hasLayout(false), layout(Layout::STACK) {}
};

struct ShadingPassSpec {
    bool byName;
    string name;
    int id;

    ShadingPassSpec()
        : byName(false), name(""), id(0) {}
};

struct TargetedResourcePayload {
    ResourceDomain domain;
    ResourceAction action;
    string commandPath;
    TargetSelector target;

    TargetedResourcePayload()
        : domain(ResourceDomain::UNKNOWN), action(ResourceAction::FORWARD), commandPath(""), target() {}
};

struct InputPayload {
    InputAction action;
    string commandPath;

    InputPayload()
        : action(InputAction::FORWARD), commandPath("") {}
};

struct CameraPayload {
    string commandPath;

    CameraPayload()
        : commandPath("") {}
};

struct Command {
    CommandType type;
    string address;
    ofxOscMessage raw;
    bool hasOnsetValue;
    bool onsetValue;
    MidiAction midiAction;
    LayersPayload layers;
    string shadingMode;
    vector<ShadingPassSpec> shadingPasses;
    InputPayload input;
    CameraPayload camera;
    TargetedResourcePayload resource;

    Command()
        : type(CommandType::UNKNOWN)
        , address("")
        , raw()
        , hasOnsetValue(false)
        , onsetValue(false)
        , midiAction(MidiAction::FORWARD)
        , layers()
        , shadingMode("")
        , shadingPasses()
        , input()
        , camera()
        , resource() {}
};

} // namespace osc
