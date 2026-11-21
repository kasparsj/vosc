#pragma once

#include "ofJson.h"
#include "ofxOscMessage.h"
#include <filesystem>
#include <string>
#include <vector>

namespace scene {

struct Command {
    ofxOscMessage message;
    std::filesystem::path asset;
    std::string assetBytes;
};

struct File {
    std::filesystem::path path;
    std::string title;
    int width = 0;
    int height = 0;
    std::vector<Command> commands;
};

File load(const std::filesystem::path& path);
std::string commandContext(size_t index, const ofxOscMessage& message);
bool appendOscValue(ofxOscMessage& message, const std::string& type, const ofJson& value);
bool assetMatches(const Command& command);

} // namespace scene
