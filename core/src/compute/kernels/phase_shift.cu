// CUDA kernel for N-step phase shift computation
// TODO: Compile with nvcc when CUDA toolkit is available
//
// This kernel computes phase and modulation from N fringe images:
//   phase(x,y) = atan2(-sum(I_k * sin(2*pi*k/N)), sum(I_k * cos(2*pi*k/N)))
//   modulation(x,y) = (2/N) * sqrt(sum_sin^2 + sum_cos^2)

#include <cuda_runtime.h>
#include <math.h>

__global__ void compute_phase_kernel(
    const unsigned char** frames,  // N input frames (device pointers)
    float* phase_out,              // output phase map
    float* modulation_out,         // output modulation map
    int width, int height,
    int num_steps)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    int idx = y * width + x;
    float sum_sin = 0.0f, sum_cos = 0.0f;
    float pi2_over_n = 2.0f * M_PI / (float)num_steps;

    for (int k = 0; k < num_steps; ++k) {
        float intensity = (float)frames[k][idx];
        float delta = pi2_over_n * k;
        sum_sin += intensity * sinf(delta);
        sum_cos += intensity * cosf(delta);
    }

    phase_out[idx] = atan2f(-sum_sin, sum_cos);
    modulation_out[idx] = (2.0f / num_steps) * sqrtf(sum_sin * sum_sin + sum_cos * sum_cos);
}

// Host wrapper function (called from cuda_engine.cpp)
extern "C" void launch_phase_shift_kernel(
    const unsigned char** d_frames, float* d_phase, float* d_modulation,
    int width, int height, int num_steps)
{
    dim3 block(16, 16);
    dim3 grid((width + 15) / 16, (height + 15) / 16);
    compute_phase_kernel<<<grid, block>>>(d_frames, d_phase, d_modulation, width, height, num_steps);
    cudaDeviceSynchronize();
}
