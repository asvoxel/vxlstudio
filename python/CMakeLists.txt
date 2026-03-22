find_package(pybind11 REQUIRED)

file(GLOB_RECURSE VXL_PYTHON_SOURCES CONFIGURE_DEPENDS bindings/*.cpp)

if(VXL_PYTHON_SOURCES)
    pybind11_add_module(_vxl_core ${VXL_PYTHON_SOURCES})

    target_link_libraries(_vxl_core
        PRIVATE
            vxl_core
    )
endif()
