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
