find_package(OpenCV REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)

# --- SQLite3 (system library) ------------------------------------------------
find_library(SQLITE3_LIBRARY sqlite3)
if(NOT SQLITE3_LIBRARY)
    message(FATAL_ERROR "SQLite3 library not found")
endif()
message(STATUS "SQLite3 found: ${SQLITE3_LIBRARY}")

# --- CUDA (optional) ---------------------------------------------------------
include(CheckLanguage)
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    message(STATUS "CUDA found -- enabling GPU compute backend")
    set(VXL_HAS_CUDA TRUE)
else()
    message(STATUS "CUDA NOT found -- CUDA compute backend disabled")
    set(VXL_HAS_CUDA FALSE)
endif()

# --- Metal (optional, macOS only) -------------------------------------------
if(APPLE)
    find_library(METAL_FRAMEWORK Metal)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    if(METAL_FRAMEWORK AND FOUNDATION_FRAMEWORK)
        message(STATUS "Metal framework found -- enabling Metal compute backend")
        set(VXL_HAS_METAL TRUE)
    else()
        message(STATUS "Metal framework NOT found -- Metal compute backend disabled")
        set(VXL_HAS_METAL FALSE)
    endif()
else()
    set(VXL_HAS_METAL FALSE)
endif()

# --- ONNX Runtime (optional) ------------------------------------------------
find_library(ONNXRUNTIME_LIB onnxruntime PATHS /opt/homebrew/lib NO_DEFAULT_PATH)
if(NOT ONNXRUNTIME_LIB)
    find_library(ONNXRUNTIME_LIB onnxruntime)
endif()

set(ONNXRUNTIME_INCLUDE_DIR "/opt/homebrew/include/onnxruntime"
    CACHE PATH "ONNX Runtime header directory")

if(ONNXRUNTIME_LIB AND EXISTS "${ONNXRUNTIME_INCLUDE_DIR}/onnxruntime_cxx_api.h")
    message(STATUS "ONNX Runtime found: ${ONNXRUNTIME_LIB}")
    set(VXL_HAS_ONNXRUNTIME TRUE)
else()
    message(STATUS "ONNX Runtime NOT found -- inference will be stubbed out")
    set(VXL_HAS_ONNXRUNTIME FALSE)
endif()
# -----------------------------------------------------------------------------

file(GLOB_RECURSE VXL_CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(vxl_core SHARED ${VXL_CORE_SOURCES})

target_compile_definitions(vxl_core
    PRIVATE VXL_BUILDING_SHARED
)

if(NOT VXL_HAS_ONNXRUNTIME)
    target_compile_definitions(vxl_core PUBLIC VXL_NO_ONNXRUNTIME)
endif()

if(VXL_HAS_CUDA)
    target_compile_definitions(vxl_core PUBLIC VXL_HAS_CUDA)
endif()

if(VXL_HAS_METAL)
    target_compile_definitions(vxl_core PUBLIC VXL_HAS_METAL)
endif()

target_include_directories(vxl_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(VXL_HAS_ONNXRUNTIME)
    target_include_directories(vxl_core PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
endif()

target_link_libraries(vxl_core
    PUBLIC
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
        opencv_calib3d
        spdlog::spdlog
    PRIVATE
        nlohmann_json::nlohmann_json
        ${SQLITE3_LIBRARY}
)

if(VXL_HAS_ONNXRUNTIME)
    target_link_libraries(vxl_core PRIVATE ${ONNXRUNTIME_LIB})
endif()

if(VXL_HAS_METAL)
    target_link_libraries(vxl_core PRIVATE ${METAL_FRAMEWORK} ${FOUNDATION_FRAMEWORK})
endif()

# Install rules
include(GNUInstallDirs)

install(TARGETS vxl_core
    EXPORT VxlStudioTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Tests
enable_testing()
add_subdirectory(tests)
