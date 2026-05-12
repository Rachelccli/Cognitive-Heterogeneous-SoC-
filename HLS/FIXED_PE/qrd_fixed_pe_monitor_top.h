// qrd_fixed_pe_monitor_top.h
// Purpose: Thin engineering wrapper for the paper-aligned fixed-point CORDIC QRD-RLS path.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#ifndef QRD_FIXED_PE_MONITOR_TOP_H
#define QRD_FIXED_PE_MONITOR_TOP_H

#include "../../qrd_axis_types.h"
#include <ap_int.h>
#include <hls_stream.h>

void qrd_fixed_pe_monitor_top(
    hls::stream<axis_cpx64_t> &snapshot_in,
    hls::stream<axis_cpx64_t> &weights_out,
    hls::stream<axis_float32_t> &delta_R_out,
    float lambda,
    float cond_threshold,
    ap_uint<1> reset,
    ap_uint<1> enable_norm,
    float norm_eps,
    float &scale_out,
    float &cond_estimate_out,
    float &delta_R_norm_out,
    ap_uint<1> &switch_recommend,
    ap_uint<1> &overflow_flag,
    ap_uint<1> &frame_error
);

#endif
