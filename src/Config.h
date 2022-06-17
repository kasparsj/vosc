#pragma once

#include "ofMain.h"

#define INITIAL_SOUNDS 0
#define INITIAL_VISUALS 9
#define MAX_NOTES 10

enum class Behaviour {
    RANDOM_SHADER = 1,
};

enum class Layout {
    COLUMN = 1,
    ROW = 2,
    GRID = 3,
    STACK = 4,
};

static std::unordered_map<std::string, Layout> const LayoutMap = {
    {"col", Layout::COLUMN},
    {"row", Layout::ROW},
    {"grid", Layout::GRID},
    {"stack", Layout::STACK},
};

enum class Source {
    VIDEO,
    HPV,
    SHADER,
    SKETCH,
    IMAGE,
    THREE,
    LAST = THREE,
};

static std::unordered_map<std::string, Source> const SourceMap = {
    {"video", Source::VIDEO},
    {"hpv", Source::HPV},
    {"shader", Source::SHADER},
    {"sketch", Source::SKETCH},
    {"image", Source::IMAGE},
    {"3d", Source::THREE},
};

enum class DataSource {
    CONST,
    RAND,
    NOISE,
    SINE,
    AMP,
    LOUD,
    ONSET,
    TIDAL,
    LAST = TIDAL,
};

static std::unordered_map<std::string, DataSource> const DataSourceMap = {
    {"const", DataSource::CONST},
    {"rand", DataSource::RAND},
    {"noise", DataSource::NOISE},
    {"sin", DataSource::SINE},
    {"amp", DataSource::AMP},
    {"loud", DataSource::LOUD},
    {"onset", DataSource::ONSET},
    {"tidal", DataSource::TIDAL},
};
