// VxlStudio -- C ABI plugin loader / manager
// SPDX-License-Identifier: MIT

#include "vxl/plugin_api.h"
#include "vxl/reconstruct.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Platform-specific dynamic-library helpers
// ---------------------------------------------------------------------------
#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>

using LibHandle = HMODULE;

static LibHandle lib_open(const char* path) {
    return LoadLibraryA(path);
}

static void* lib_sym(LibHandle h, const char* sym) {
    return reinterpret_cast<void*>(GetProcAddress(h, sym));
}

static void lib_close(LibHandle h) {
    FreeLibrary(h);
}

static std::string lib_error() {
    DWORD err = GetLastError();
    char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err,
                   0, buf, sizeof(buf), nullptr);
    return std::string(buf);
}

static const char* lib_extension() { return ".dll"; }

#else // POSIX (macOS, Linux)
#   include <dlfcn.h>
#   include <dirent.h>

using LibHandle = void*;

static LibHandle lib_open(const char* path) {
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static void* lib_sym(LibHandle h, const char* sym) {
    return dlsym(h, sym);
}

static void lib_close(LibHandle h) {
    dlclose(h);
}

static std::string lib_error() {
    const char* e = dlerror();
    return e ? std::string(e) : "unknown error";
}

#   ifdef __APPLE__
static const char* lib_extension() { return ".dylib"; }
#   else
static const char* lib_extension() { return ".so"; }
#   endif

#endif // _WIN32

// ---------------------------------------------------------------------------
// Helper: check if a filename ends with a given suffix
// ---------------------------------------------------------------------------
static bool ends_with(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// ---------------------------------------------------------------------------
// Internal representation of a loaded plugin
// ---------------------------------------------------------------------------
namespace vxl {

struct LoadedPlugin {
    std::string   path;
    PluginInfo    info;
    LibHandle     handle   = nullptr;
    void*         object   = nullptr;

    using DestroyFn = void(*)(void*);
    DestroyFn     destroy_fn = nullptr;
};

// ---------------------------------------------------------------------------
// PluginManager::Impl
// ---------------------------------------------------------------------------
struct PluginManager::Impl {
    std::unordered_map<std::string, LoadedPlugin> plugins;
};

PluginManager::PluginManager()  : impl_(std::make_unique<Impl>()) {}
PluginManager::~PluginManager() {
    // Unload all plugins in deterministic order
    for (auto& [name, lp] : impl_->plugins) {
        if (lp.object && lp.destroy_fn) {
            lp.destroy_fn(lp.object);
            lp.object = nullptr;
        }
        if (lp.handle) {
            lib_close(lp.handle);
            lp.handle = nullptr;
        }
    }
}

// ---------------------------------------------------------------------------
// load()
// ---------------------------------------------------------------------------
Result<PluginInfo> PluginManager::load(const std::string& path) {
    // Open the shared library
    LibHandle handle = lib_open(path.c_str());
    if (!handle) {
        return Result<PluginInfo>::failure(
            ErrorCode::FILE_NOT_FOUND,
            "Failed to load plugin '" + path + "': " + lib_error());
    }

    // Resolve required C ABI symbols
    using NameFn    = const char*(*)();
    using VersionFn = const char*(*)();
    using TypeFn    = const char*(*)();
    using CreateFn  = void*(*)();
    using DestroyFn = void(*)(void*);

    auto name_fn    = reinterpret_cast<NameFn>(lib_sym(handle, "vxl_plugin_name"));
    auto version_fn = reinterpret_cast<VersionFn>(lib_sym(handle, "vxl_plugin_version"));
    auto type_fn    = reinterpret_cast<TypeFn>(lib_sym(handle, "vxl_plugin_type"));
    auto create_fn  = reinterpret_cast<CreateFn>(lib_sym(handle, "vxl_plugin_create"));
    auto destroy_fn = reinterpret_cast<DestroyFn>(lib_sym(handle, "vxl_plugin_destroy"));

    if (!name_fn || !version_fn || !type_fn || !create_fn || !destroy_fn) {
        lib_close(handle);
        return Result<PluginInfo>::failure(
            ErrorCode::INTERNAL_ERROR,
            "Plugin '" + path + "' is missing required C ABI symbols "
            "(vxl_plugin_name / vxl_plugin_version / vxl_plugin_type / "
            "vxl_plugin_create / vxl_plugin_destroy)");
    }

    // Collect metadata
    PluginInfo info;
    info.name    = name_fn();
    info.version = version_fn();
    info.type    = type_fn();

    // Optionally resolve vxl_plugin_author / vxl_plugin_description
    using StrFn = const char*(*)();
    auto author_fn = reinterpret_cast<StrFn>(lib_sym(handle, "vxl_plugin_author"));
    auto desc_fn   = reinterpret_cast<StrFn>(lib_sym(handle, "vxl_plugin_description"));
    if (author_fn) info.author      = author_fn();
    if (desc_fn)   info.description = desc_fn();

    // Check for duplicate name
    if (impl_->plugins.count(info.name)) {
        lib_close(handle);
        return Result<PluginInfo>::failure(
            ErrorCode::INVALID_PARAMETER,
            "Plugin '" + info.name + "' is already loaded");
    }

    // Create the plugin object
    void* object = create_fn();
    if (!object) {
        lib_close(handle);
        return Result<PluginInfo>::failure(
            ErrorCode::INTERNAL_ERROR,
            "vxl_plugin_create() returned nullptr for plugin '" + info.name + "'");
    }

    // Store
    LoadedPlugin lp;
    lp.path       = path;
    lp.info       = info;
    lp.handle     = handle;
    lp.object     = object;
    lp.destroy_fn = destroy_fn;

    impl_->plugins[info.name] = std::move(lp);

    return Result<PluginInfo>::success(info);
}

// ---------------------------------------------------------------------------
// load_directory()
// ---------------------------------------------------------------------------
Result<int> PluginManager::load_directory(const std::string& dir_path) {
    const std::string ext = lib_extension();
    int loaded_count = 0;

#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    std::string pattern = dir_path + "\\*" + ext;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return Result<int>::failure(
            ErrorCode::FILE_NOT_FOUND,
            "Cannot open plugin directory '" + dir_path + "'");
    }
    do {
        std::string full = dir_path + "\\" + fd.cFileName;
        auto r = load(full);
        if (r.ok()) ++loaded_count;
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    DIR* dir = opendir(dir_path.c_str());
    if (!dir) {
        return Result<int>::failure(
            ErrorCode::FILE_NOT_FOUND,
            "Cannot open plugin directory '" + dir_path + "'");
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string fname = entry->d_name;
        if (!ends_with(fname, ext)) continue;
        std::string full = dir_path + "/" + fname;
        auto r = load(full);
        if (r.ok()) ++loaded_count;
    }
    closedir(dir);
#endif

    return Result<int>::success(loaded_count);
}

// ---------------------------------------------------------------------------
// unload()
// ---------------------------------------------------------------------------
Result<void> PluginManager::unload(const std::string& name) {
    auto it = impl_->plugins.find(name);
    if (it == impl_->plugins.end()) {
        return Result<void>::failure(
            ErrorCode::INVALID_PARAMETER,
            "Plugin '" + name + "' is not loaded");
    }

    auto& lp = it->second;
    if (lp.object && lp.destroy_fn) {
        lp.destroy_fn(lp.object);
    }
    if (lp.handle) {
        lib_close(lp.handle);
    }
    impl_->plugins.erase(it);
    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// Query helpers
// ---------------------------------------------------------------------------
std::vector<PluginInfo> PluginManager::loaded_plugins() const {
    std::vector<PluginInfo> out;
    out.reserve(impl_->plugins.size());
    for (const auto& [name, lp] : impl_->plugins) {
        out.push_back(lp.info);
    }
    return out;
}

bool PluginManager::is_loaded(const std::string& name) const {
    return impl_->plugins.count(name) != 0;
}

void* PluginManager::get_plugin_object(const std::string& name) {
    auto it = impl_->plugins.find(name);
    if (it == impl_->plugins.end()) return nullptr;
    return it->second.object;
}

IDepthProvider* PluginManager::get_depth_provider(const std::string& name) {
    auto it = impl_->plugins.find(name);
    if (it == impl_->plugins.end()) return nullptr;
    if (it->second.info.type != "depth_provider") return nullptr;
    return static_cast<IDepthProvider*>(it->second.object);
}

// ---------------------------------------------------------------------------
// Global singleton
// ---------------------------------------------------------------------------
PluginManager& plugin_manager() {
    static PluginManager instance;
    return instance;
}

} // namespace vxl
