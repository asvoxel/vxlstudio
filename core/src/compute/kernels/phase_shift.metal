#include <metal_stdlib>
using namespace metal;

// Metal compute kernel for N-step phase shift computation
//
// Computes wrapped phase and modulation from N fringe images:
//   phase(x,y) = atan2(-sum(I_k * sin(2*pi*k/N)), sum(I_k * cos(2*pi*k/N)))
//   modulation(x,y) = (2/N) * sqrt(sum_sin^2 + sum_cos^2)
//
// Buffer layout:
//   frames: all N frames concatenated contiguously, each frame_stride bytes apart
//   frame_stride = width * height (for GRAY8 images)

kernel void compute_phase(
    device const uchar* frames [[buffer(0)]],
    device float* phase_out [[buffer(1)]],
    device float* modulation_out [[buffer(2)]],
    constant int& width [[buffer(3)]],
    constant int& height [[buffer(4)]],
    constant int& num_steps [[buffer(5)]],
    constant int& frame_stride [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= uint(width) || gid.y >= uint(height)) return;

    int idx = gid.y * width + gid.x;
    float sum_sin = 0.0f, sum_cos = 0.0f;
    float pi2_over_n = 2.0f * M_PI_F / float(num_steps);

    for (int k = 0; k < num_steps; ++k) {
        float intensity = float(frames[k * frame_stride + idx]);
        float delta = pi2_over_n * float(k);
        sum_sin += intensity * sin(delta);
        sum_cos += intensity * cos(delta);
    }

    phase_out[idx] = atan2(-sum_sin, sum_cos);
    modulation_out[idx] = (2.0f / float(num_steps)) * sqrt(sum_sin * sum_sin + sum_cos * sum_cos);
}
