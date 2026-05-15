// snapshot_normalizer.h
// Purpose: Shared AXI4-Stream snapshot normalizer for 8-channel complex QRD-RLS frames.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#ifndef SNAPSHOT_NORMALIZER_H
#define SNAPSHOT_NORMALIZER_H

#include "../../qrd_axis_types.h"
#include <ap_int.h>
#include <hls_stream.h>

#ifndef QRD_NORMALIZER_TARGET_PEAK
#define QRD_NORMALIZER_TARGET_PEAK 0.5f
#endif

void snapshot_normalizer(
    hls::stream<axis_cpx64_t> &snapshot_in,
    hls::stream<axis_cpx64_t> &snapshot_out,
    ap_uint<1> enable_norm,
    float eps,
    float &scale_out,
    ap_uint<1> &frame_error
);

#endif
