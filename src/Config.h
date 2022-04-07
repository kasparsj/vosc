#pragma once

#include "ofMain.h"

#define MAX_SOUNDS 2
#define MAX_VISUALS 8
#define MAX_NOTES 10

enum Behaviour {
    B_RANDOM_SHADER = 1,
};

enum Layout {
    L_COLUMN,
    L_ROW,
    L_GRID,
    L_STACK,
};

enum Source {
    S_VIDEO,
    S_SHADER,
    S_SKETCH,
    S_LAST = S_SKETCH,
};

static std::unordered_map<std::string, Source> const SourceMap = {
    {"video", Source::S_VIDEO},
    {"shader", Source::S_SHADER},
    {"sketch", Source::S_SKETCH},
};

enum DataSource {
    DS_CONST,
    DS_RAND,
    DS_NOISE,
    DS_AMP,
    DS_LOUD,
    DS_ONSET,
    DS_TIDAL,
    DS_LAST = DS_TIDAL,
};

static std::unordered_map<std::string, DataSource> const DataSourceMap = {
    {"const", DataSource::DS_CONST},
    {"rand", DataSource::DS_RAND},
    {"noise", DataSource::DS_NOISE},
    {"amp", DataSource::DS_AMP},
    {"loud", DataSource::DS_LOUD},
    {"onset", DataSource::DS_ONSET},
    {"tidal", DataSource::DS_TIDAL},
};
