#pragma once

#include "ofMain.h"

#define INITIAL_VISUALS 9
#define MAX_NOTES 10

enum class Behaviour {
    RANDOM_SHADER = 1,
};

enum class Layout {
    STACK = 1,
    GRID = 2,
    COLUMN = 3,
    ROW = 4,
};

static std::unordered_map<std::string, Layout> const LayoutMap = {
    {"stack", Layout::STACK},
    {"grid", Layout::GRID},
    {"col", Layout::COLUMN},
    {"row", Layout::ROW},
};

enum class Source {
    COLOR,
    VIDEO,
    HPV,
    GV,
    SHADER,
    SHADER_PP,
    SKETCH,
    IMAGE,
    HTML,
    WEBCAM,
    DRAW,
    LAST = DRAW,
};

static std::unordered_map<std::string, Source> const SourceMap = {
    {"color", Source::COLOR},
    {"video", Source::VIDEO},
    {"hpv", Source::HPV},
    {"gv", Source::GV},
    {"shader", Source::SHADER},
    {"shader_pp", Source::SHADER_PP},
    {"sketch", Source::SKETCH},
    {"image", Source::IMAGE},
    {"html", Source::HTML},
    {"webcam", Source::WEBCAM},
    {"2d", Source::DRAW},
};

enum class DataSource {
    TIME,
    CONST,
    RAND,
    NOISE,
    SINE,
    COSINE,
    MIC,
    AMP,
    LOUD,
    ONSET,
    TIDAL,
    LAST = TIDAL,
};

static std::unordered_map<std::string, DataSource> const DataSourceMap = {
    {"time", DataSource::TIME},
    {"const", DataSource::CONST},
    {"rand", DataSource::RAND},
    {"noise", DataSource::NOISE},
    {"sin", DataSource::SINE},
    {"cos", DataSource::COSINE},
    {"mic", DataSource::MIC},
    {"amp", DataSource::AMP},
    {"loud", DataSource::LOUD},
    {"onset", DataSource::ONSET},
    {"tidal", DataSource::TIDAL},
};

enum class PostPass {
    BLOOM,
    CONVOLUTION,
    DOF,
    DOFALT,
    EDGE,
    FXAA,
    KALEIDOSCOPE,
    NOISEWARP,
    PIXELATE,
    LUT,
    CONTRAST,
    SSAO,
    HTILTSHIFT,
    VTILTSHIFT,
    RGBSHIFT,
    FAKESSS,
    ZOOMBLUR,
    BLEACHBYPASS,
    TOON,
    GODRAYS,
    RIMHIGHLIGHTING,
    LIMBDARKENING,
    INVERT,
    GLITCH,
    ROTATE,
    PIXELSORT,
    BEYOON,
    REFLECTX,
    REFLECTY,
    SPLIT,
    
    BG,
    SHADOWLIGHT,
    POINTLIGHT,
    FOG,
    
    LAST = FOG,
};

static std::unordered_map<std::string, PostPass> const PostPassMap = {
    {"bloom", PostPass::BLOOM},
    {"convolution", PostPass::CONVOLUTION},
    {"dof", PostPass::DOF},
    {"dofalt", PostPass::DOFALT},
    {"edge", PostPass::EDGE},
    {"fxaa", PostPass::FXAA},
    {"kaleidoscope", PostPass::KALEIDOSCOPE},
    {"noisewarp", PostPass::NOISEWARP},
    {"pixelate", PostPass::PIXELATE},
    {"lut", PostPass::LUT},
    {"contrast", PostPass::CONTRAST},
    {"ssao", PostPass::SSAO},
    {"htiltshift", PostPass::HTILTSHIFT},
    {"vtiltshift", PostPass::VTILTSHIFT},
    {"rgbshift", PostPass::RGBSHIFT},
    {"fakesss", PostPass::FAKESSS},
    {"zoomblur", PostPass::ZOOMBLUR},
    {"bleachbypass", PostPass::BLEACHBYPASS},
    {"toon", PostPass::TOON},
    {"godrays", PostPass::GODRAYS},
    {"rimhighlighting", PostPass::RIMHIGHLIGHTING},
    {"limbdarkening", PostPass::LIMBDARKENING},
    {"invert", PostPass::INVERT},
    {"glitch", PostPass::GLITCH},
    {"rotate", PostPass::ROTATE},
    {"pixelsort", PostPass::PIXELSORT},
    {"beyoon", PostPass::BEYOON},
    {"reflectx", PostPass::REFLECTX},
    {"reflecty", PostPass::REFLECTY},
    {"split", PostPass::SPLIT},
    
    {"bg", PostPass::BG},
    {"shadowlight", PostPass::SHADOWLIGHT},
    {"pointlight", PostPass::POINTLIGHT},
    {"fog", PostPass::FOG},
};
