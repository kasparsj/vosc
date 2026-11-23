#pragma once

#include "../Config.h"

#if VOSC_OPERATOR_UI
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vosc::operator_ui {

struct ExampleDirectories {
    std::filesystem::path examples, data;
};

struct ExampleEntry {
    std::string id, title, category, summary, cues, requirements;
    // All three file paths are relative to ExampleDirectories::examples.
    std::string manifest, preview, companion;
    std::string inputName, inputKind, inputDefault, blocked, attach;
};

ExampleDirectories exampleDirectories();
std::vector<ExampleEntry> readExamples(const ExampleDirectories& roots, std::string& error);
std::optional<std::filesystem::path> resolveExampleFile(const std::filesystem::path& root,
                                                        const std::filesystem::path& relative);

} // namespace vosc::operator_ui
#endif
