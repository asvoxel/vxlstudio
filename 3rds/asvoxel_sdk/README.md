# AsVoxel Camera SDK

Place the AsVoxel structured light camera SDK files here:

```
asvoxel_sdk/
├── include/    # SDK header files
│   └── asvoxel_sdk.h
├── lib/        # SDK libraries (.dylib/.so/.dll)
│   ├── libasvoxel_sdk.dylib  (macOS)
│   ├── libasvoxel_sdk.so     (Linux)
│   └── asvoxel_sdk.dll       (Windows)
└── README.md   # This file
```

After placing SDK files, update core/CMakeLists.txt to find and link the SDK.
Then implement the TODO items in core/src/camera/asvoxel_camera.cpp.
