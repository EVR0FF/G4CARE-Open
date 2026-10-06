//==============================================================================
//
// G4CARE
//
// @file    SharedLibrary.cc
// @brief   Implementation of the shared-library RAII wrapper.
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

#include "SharedLibrary.hh"

#include <iostream>

#ifdef __linux__
#include <dlfcn.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

SharedLibrary::~SharedLibrary() {
    Close();
}

bool SharedLibrary::Open(const std::string& path) {
    Close();
    fPath = path;

#ifdef __linux__
    fHandle = dlopen(path.c_str(), RTLD_LAZY);
#elif defined(_WIN32)
    fHandle = static_cast<void*>(LoadLibraryA(path.c_str()));
#else
    fHandle = nullptr;
#endif

    if (!fHandle) {
#ifdef __linux__
        std::cerr << "SharedLibrary: failed to load '" << path << "': " << dlerror() << std::endl;
#else
        std::cerr << "SharedLibrary: failed to load '" << path << "'." << std::endl;
#endif
        fPath.clear();
        return false;
    }
    return true;
}

void* SharedLibrary::Symbol(const std::string& name) const {
    if (!fHandle) return nullptr;
#ifdef __linux__
    return dlsym(fHandle, name.c_str());
#elif defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(fHandle), name.c_str()));
#else
    return nullptr;
#endif
}

void SharedLibrary::Close() {
    if (fHandle) {
#ifdef __linux__
        dlclose(fHandle);
#elif defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(fHandle));
#endif
        fHandle = nullptr;
        fPath.clear();
    }
}
