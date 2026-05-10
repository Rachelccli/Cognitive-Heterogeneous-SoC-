// dual_precision_qrd_top.h
// Purpose: Minimal dual-precision QRD-RLS top-level IP with fixed/float mux.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#ifndef DUAL_PRECISION_QRD_TOP_H
#define DUAL_PRECISION_QRD_TOP_H

#include "qrd_axis_types.h"
#include <ap_int.h>
#include <hls_stream.h>

void dual_precision_qrd_top(
    hls::stream<axis_cpx64_t> &snapshot_in,
    hls::stream<axis_cpx64_t> &weights_out,
    hls::stream<axis_float32_t> &delta_R_out,
    ap_uint<2> mode,
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
    ap_uint<1> &frame_error,
    ap_uint<1> &selected_float_out,
    ap_uint<1> &migration_done,
    ap_uint<1> &migration_direction,
    ap_uint<1> &active_float_out,
    float &lambda_transit_out
);

#endif
