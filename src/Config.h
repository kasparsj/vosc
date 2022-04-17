#pragma once

#include "ofMain.h"

#define MAX_SOUNDS 2
#define MAX_VISUALS 8
#define MAX_NOTES 10

enum class Behaviour {
    RANDOM_SHADER = 1,
};

enum class Layout {
    COLUMN,
    ROW,
    GRID,
    STACK,
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
