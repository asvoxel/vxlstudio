#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #ifdef VXL_BUILDING_SHARED
        #define VXL_EXPORT __declspec(dllexport)
    #else
        #define VXL_EXPORT __declspec(dllimport)
    #endif
#else
    #define VXL_EXPORT __attribute__((visibility("default")))
#endif
