#pragma once

#include "../Config.h"

#if VOSC_OPERATOR_UI && defined(__APPLE__)
#include <filesystem>
#include <functional>

namespace vosc::operator_ui {

enum class ClipPickResult { Chosen, Cancelled, Unavailable };
void chooseExampleClip(std::function<void(ClipPickResult, std::filesystem::path)> completion);
bool revealExampleFile(const std::filesystem::path& path);
bool openDataFolder(const std::filesystem::path& path);

} // namespace vosc::operator_ui
#endif
