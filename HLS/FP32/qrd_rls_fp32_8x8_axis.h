// qrd_rls_fp32_8x8_axis.h
// Purpose: Engineering AXI4-Stream snapshot wrapper for the 8x8 FP32 QRD-RLS kernel.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#ifndef QRD_RLS_FP32_8X8_AXIS_H
#define QRD_RLS_FP32_8X8_AXIS_H

#include "qrd_axis_types.h"
#include <ap_int.h>
#include <hls_stream.h>

void qrd_rls_fp32_8x8_axis(
    hls::stream<axis_cpx64_t> &snapshot_in,
    hls::stream<axis_cpx64_t> &weights_out,
    hls::stream<axis_float32_t> &diag_out,
    float lambda,
    ap_uint<1> reset,
    ap_uint<1> &frame_error
);

#endif
