#pragma once

#include <filesystem>

namespace vosc::macos {

// Returns false for an ordinary macOS build. Throws if managed data cannot be prepared.
bool initializeManagedData(bool interactive);
const std::filesystem::path& managedDataRoot();

} // namespace vosc::macos
