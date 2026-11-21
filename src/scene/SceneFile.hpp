#pragma once

#include "ofJson.h"
#include "ofxOscMessage.h"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace scene {

inline constexpr int MaxImageSide = 4096;
// The export exchange is below 8 MiB. Allow 1 MiB for JSON number spellings,
// the escaped title, and the native manifest header.
inline constexpr size_t MaxManifestBytes = 9 * 1024 * 1024;
inline constexpr size_t MaxDecodedImageBytes = 64 * 1024 * 1024;
inline constexpr size_t MaxImagePixels = 10'000'000;

struct Command {
    ofxOscMessage message;
    std::filesystem::path asset;
    std::string assetBytes;
    bool feedback = false;
};

struct NativeCommand {
    ofxOscMessage message;
    std::string sourceKind;
    std::map<std::string, std::filesystem::path> stages;
};

struct File {
    std::filesystem::path path;
    int version = 1;
    std::string title;
    int width = 0;
    int height = 0;
    int onsetGate = -1;
    std::vector<Command> commands;
    std::vector<NativeCommand> nativeCommands;
    std::map<std::filesystem::path, std::string> nativeAssets;
    std::string mediaInput;
};

using Inputs = std::map<std::string, std::string>;
File load(const std::filesystem::path& path, const Inputs& inputs = {});
void verifyNativeAssets(const File& file);
std::string commandContext(size_t index, const ofxOscMessage& message);
bool appendOscValue(ofxOscMessage& message, const std::string& type, const ofJson& value);
bool assetMatches(const Command& command);

} // namespace scene
