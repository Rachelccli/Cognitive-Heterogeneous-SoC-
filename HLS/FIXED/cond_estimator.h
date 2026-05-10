// cond_estimator.h
// Purpose: Diagonal condition estimator and Delta R_ii feature extractor.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#ifndef COND_ESTIMATOR_H
#define COND_ESTIMATOR_H

#include <ap_int.h>
#include <hls_stream.h>

#ifndef QRD_RLS_N
#define QRD_RLS_N 8
#endif

void cond_estimator(
    hls::stream<float> &R_diag_in,
    float cond_threshold,
    float &cond_estimate_out,
    float &delta_R_norm_out,
    hls::stream<float> &delta_R_out,
    ap_uint<1> &switch_recommend,
    ap_uint<1> reset
);

#endif
