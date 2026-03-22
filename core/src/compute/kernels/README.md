# GPU Compute Kernels

## CUDA Kernels
- `phase_shift.cu` - N-step phase shift computation
- `phase_unwrap.cu` - Multi-frequency temporal phase unwrapping

### Building CUDA kernels
Requires NVIDIA CUDA Toolkit (>= 11.0):
```bash
nvcc -c phase_shift.cu -o phase_shift.o
nvcc -c phase_unwrap.cu -o phase_unwrap.o
```

## Metal Shaders (macOS)
- `phase_shift.metal` - N-step phase shift computation for Apple GPU

### Building Metal shaders
```bash
xcrun -sdk macosx metal -c phase_shift.metal -o phase_shift.air
xcrun -sdk macosx metallib phase_shift.air -o phase_shift.metallib
```

### Integration
When compiled, link the kernel objects into libvxl_core and
define VXL_HAS_CUDA to enable the CUDA compute backend.

For Metal on macOS, the .metallib file should be loaded at runtime
via MTLDevice::newLibraryWithURL or embedded as a default library.
