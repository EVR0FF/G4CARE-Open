//==============================================================================
//
// G4CARE
//
// @file    SharedLibrary.hh
// @brief   Minimal RAII wrapper around dynamic shared-library loading.
//
// @details
//   Thin cross-platform wrapper over dlopen/dlsym/dlclose (Linux) and
//   LoadLibrary/GetProcAddress/FreeLibrary (Windows). Used to load custom
//   physics constructors (PhysicsManager) and custom geometry
//   (GeometryManager::LoadCustomGeometry) from .so/.dll plugins.
//
//   Configuration keys read: none.
//
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
//
// @date    2026-09-30
// @version 0.9.0
//
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0 License (see LICENSE)
//
//==============================================================================

#ifndef SHARED_LIBRARY_HH
#define SHARED_LIBRARY_HH

#include <string>

/// @brief RAII wrapper for a dynamically loaded shared library.
class SharedLibrary {
public:
    SharedLibrary() = default;
    ~SharedLibrary();

    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;

    /// @brief Load a shared library, closing any previously opened handle.
    /// @param path Path to the library (.so / .dll).
    /// @return true on success.
    bool Open(const std::string& path);

    /// @brief Look up a symbol by name.
    /// @param name Symbol name.
    /// @return Symbol address, or nullptr if not found / not open.
    void* Symbol(const std::string& name) const;

    /// @brief Close the library handle (if open).
    void Close();

    /// @brief Whether a library is currently loaded.
    bool IsOpen() const { return fHandle != nullptr; }

    /// @brief Path of the currently loaded library (empty if none).
    const std::string& Path() const { return fPath; }

private:
    void* fHandle = nullptr;
    std::string fPath;
};

#endif // SHARED_LIBRARY_HH
