add_library(vxl_sample_plugin MODULE
    sample_plugin.cpp
)

target_include_directories(vxl_sample_plugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(vxl_sample_plugin
    PRIVATE
        vxl_core
)

# On macOS modules use .dylib, on Linux .so; MODULE handles this automatically.
# Place the built plugin in a predictable location.
set_target_properties(vxl_sample_plugin PROPERTIES
    PREFIX ""                                         # no "lib" prefix
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)
