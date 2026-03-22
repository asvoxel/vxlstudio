cmake_minimum_required(VERSION 3.21)

project(VxlStudio
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Put all binaries in predictable locations
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# macOS: use @rpath for shared libraries
set(CMAKE_MACOSX_RPATH ON)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

add_subdirectory(core)
add_subdirectory(python)
add_subdirectory(plugins/builtin/sample_plugin)

# ---------------------------------------------------------------------------
# Install targets
# ---------------------------------------------------------------------------

# Headers
install(DIRECTORY core/include/vxl/
    DESTINATION include/vxl
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Python package (vxl/)
install(DIRECTORY python/vxl/
    DESTINATION lib/python/vxl
    FILES_MATCHING PATTERN "*.py"
)

# VxlApp runtime
install(DIRECTORY vxlapp/
    DESTINATION lib/python/vxlapp
    FILES_MATCHING PATTERN "*.py" PATTERN "*.json"
)

# Demo application
install(DIRECTORY demo/
    DESTINATION share/vxlstudio/demo
    FILES_MATCHING PATTERN "*.py" PATTERN "*.json" PATTERN "*.ui"
)

# CMake package config (for find_package support)
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/VxlStudioConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_file(
    cmake/VxlStudioConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/VxlStudioConfig.cmake"
    @ONLY
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/VxlStudioConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/VxlStudioConfigVersion.cmake"
    DESTINATION lib/cmake/VxlStudio
)

# ---------------------------------------------------------------------------
# CPack configuration
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "VxlStudio")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "asVoxel")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "3D+2D defect inspection platform for structured-light cameras")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/asVoxel/VxlStudio")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# Per-platform generator defaults
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "VxlStudio ${PROJECT_VERSION}")
    set(CPACK_DMG_FORMAT "UDBZ")
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "VxlStudio ${PROJECT_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "VxlStudio")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
elseif(UNIX)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "asVoxel Team")
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libopencv-dev (>= 4.0)")
endif()

include(CPack)
