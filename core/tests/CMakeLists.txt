find_package(GTest REQUIRED)

file(GLOB_RECURSE VXL_TEST_SOURCES CONFIGURE_DEPENDS *.cpp)

if(VXL_TEST_SOURCES)
    add_executable(vxl_core_tests ${VXL_TEST_SOURCES})

    find_library(SQLITE3_LIB sqlite3)
    target_link_libraries(vxl_core_tests
        PRIVATE
            vxl_core
            GTest::gtest
            GTest::gtest_main
            ${SQLITE3_LIB}
    )

    # Provide the path to test data (recipes/) so test_recipe.cpp can locate JSON files.
    target_compile_definitions(vxl_core_tests PRIVATE
        VXL_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/recipes"
        VXL_TEST_PLUGIN_PATH="${CMAKE_BINARY_DIR}/plugins/vxl_sample_plugin${CMAKE_SHARED_MODULE_SUFFIX}"
    )

    # Ensure the sample plugin is built before the tests
    if(TARGET vxl_sample_plugin)
        add_dependencies(vxl_core_tests vxl_sample_plugin)
    endif()

    include(GoogleTest)
    gtest_discover_tests(vxl_core_tests)
endif()
