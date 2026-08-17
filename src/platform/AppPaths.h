#pragma once

#include <filesystem>
#include <string_view>

namespace rillshot::platform {

// Returns the directory containing the running executable. The path is derived
// from the module, never from the current working directory.
[[nodiscard]] std::filesystem::path executableDirectory() noexcept;

// Creates and returns an app-owned data directory. Unpackaged/Portable builds
// keep data next to the executable; packaged/MSIX builds use the package's
// LocalState directory. Invalid names and unwritable locations fail closed.
[[nodiscard]] std::filesystem::path applicationDirectory(
    std::wstring_view directoryName) noexcept;

} // namespace rillshot::platform
