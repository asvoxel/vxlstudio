// CUDA kernel for multi-frequency temporal phase unwrapping
// TODO: Compile with nvcc when CUDA toolkit is available
//
// This kernel implements temporal phase unwrapping across multiple frequencies.
// Given wrapped phase maps at frequencies f0, f1, f2, ... (ascending order),
// it progressively unwraps from the lowest frequency (coarsest) to the highest
// (finest), using each level to disambiguate the next.
//
// Algorithm:
//   For the lowest frequency f0, the unwrapped phase equals the wrapped phase.
//   For each subsequent frequency f_k:
//     predicted_phase = unwrapped_prev * (f_k / f_{k-1})
//     n = round((predicted_phase - wrapped_k) / (2*pi))
//     unwrapped_k = wrapped_k + n * 2*pi

#include <cuda_runtime.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Kernel: unwrap one frequency level using the previously unwrapped result
// ---------------------------------------------------------------------------
__global__ void unwrap_level_kernel(
    const float* wrapped_phase,      // wrapped phase at current frequency
    const float* prev_unwrapped,     // unwrapped phase from previous (coarser) level
    const float* modulation,         // modulation map for quality masking
    float* unwrapped_out,            // output unwrapped phase
    int width, int height,
    float freq_ratio,                // current_freq / previous_freq
    float min_modulation)            // pixels below this become NaN
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    int idx = y * width + x;

    // Mask low-modulation pixels
    if (modulation[idx] < min_modulation) {
        unwrapped_out[idx] = nanf("");
        return;
    }

    // Predict phase at current frequency from the coarser unwrapped phase
    float predicted = prev_unwrapped[idx] * freq_ratio;
    float wrapped   = wrapped_phase[idx];

    // Find the integer number of 2*pi wraps
    float two_pi = 2.0f * M_PI;
    float n = roundf((predicted - wrapped) / two_pi);

    unwrapped_out[idx] = wrapped + n * two_pi;
}

// ---------------------------------------------------------------------------
// Kernel: initialize the coarsest level (just copy + modulation mask)
// ---------------------------------------------------------------------------
__global__ void init_coarsest_kernel(
    const float* wrapped_phase,
    const float* modulation,
    float* unwrapped_out,
    int width, int height,
    float min_modulation)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    int idx = y * width + x;

    if (modulation[idx] < min_modulation) {
        unwrapped_out[idx] = nanf("");
    } else {
        unwrapped_out[idx] = wrapped_phase[idx];
    }
}

// ---------------------------------------------------------------------------
// Host wrapper: full multi-frequency temporal unwrapping
// ---------------------------------------------------------------------------
extern "C" void launch_phase_unwrap(
    const float** d_wrapped_phases,   // array of wrapped phase maps (device ptrs)
    const float** d_modulations,      // array of modulation maps (device ptrs)
    const int* frequencies,           // frequency values (host array)
    float* d_unwrapped_out,           // final unwrapped phase (device ptr)
    float* d_temp_buffer,             // temporary buffer, same size as one phase map
    int num_levels,
    int width, int height,
    float min_modulation)
{
    dim3 block(16, 16);
    dim3 grid((width + 15) / 16, (height + 15) / 16);

    // Level 0: coarsest frequency -- unwrapped = wrapped (with masking)
    init_coarsest_kernel<<<grid, block>>>(
        d_wrapped_phases[0], d_modulations[0],
        d_unwrapped_out, width, height, min_modulation);
    cudaDeviceSynchronize();

    // Levels 1..N-1: progressively unwrap using the previous level
    for (int level = 1; level < num_levels; ++level) {
        float freq_ratio = (float)frequencies[level] / (float)frequencies[level - 1];

        // Read from d_unwrapped_out (previous result), write to d_temp_buffer
        float* src = (level % 2 == 1) ? d_unwrapped_out : d_temp_buffer;
        float* dst = (level % 2 == 1) ? d_temp_buffer   : d_unwrapped_out;

        // For level 1, src is d_unwrapped_out (from init), which is correct.
        // We need to alternate buffers to avoid read/write conflicts.
        unwrap_level_kernel<<<grid, block>>>(
            d_wrapped_phases[level], src, d_modulations[level],
            dst, width, height, freq_ratio, min_modulation);
        cudaDeviceSynchronize();
    }

    // If the final result ended up in d_temp_buffer, copy it back
    if (num_levels > 1 && (num_levels % 2 == 1)) {
        cudaMemcpy(d_unwrapped_out, d_temp_buffer,
                   width * height * sizeof(float), cudaMemcpyDeviceToDevice);
    }
}
