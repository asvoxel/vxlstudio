#pragma once

#include "vxl/export.h"
#include "vxl/error.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vxl {

// Forward declaration -- defined in reconstruct.h
class IDepthProvider;

// ---------------------------------------------------------------------------
// Plugin metadata
// ---------------------------------------------------------------------------
struct VXL_EXPORT PluginInfo {
    std::string name;
    std::string version;
    std::string author;
    std::string type;        // "depth_provider", "inspector", "device_driver", "io_driver"
    std::string description;
};

// ---------------------------------------------------------------------------
// C ABI entry points that every plugin .so/.dll/.dylib must export:
//
//   extern "C" {
//       const char* vxl_plugin_name();
//       const char* vxl_plugin_version();
//       const char* vxl_plugin_type();
//       void*       vxl_plugin_create();       // returns heap-allocated plugin object
//       void        vxl_plugin_destroy(void*); // destroys that object
//   }
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// PluginManager -- loads / unloads / queries plugin shared libraries
// ---------------------------------------------------------------------------
class VXL_EXPORT PluginManager {
public:
    PluginManager();
    ~PluginManager();

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    /// Load a single plugin from a shared-library path (.so / .dylib / .dll).
    Result<PluginInfo> load(const std::string& path);

    /// Load every plugin found in a directory (files matching the platform
    /// shared-library extension).  Returns the number of plugins loaded.
    Result<int> load_directory(const std::string& dir_path);

    /// Unload a previously loaded plugin by name.
    Result<void> unload(const std::string& name);

    /// Return metadata for all currently loaded plugins.
    std::vector<PluginInfo> loaded_plugins() const;

    /// Check whether a plugin with the given name is loaded.
    bool is_loaded(const std::string& name) const;

    /// Return the raw plugin object pointer for the named plugin.
    /// The caller is responsible for casting to the correct interface.
    void* get_plugin_object(const std::string& name);

    /// Convenience: return the plugin object as IDepthProvider* if the plugin's
    /// type is "depth_provider".  Returns nullptr if not found or wrong type.
    IDepthProvider* get_depth_provider(const std::string& name);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Global singleton plugin manager.
VXL_EXPORT PluginManager& plugin_manager();

} // namespace vxl
