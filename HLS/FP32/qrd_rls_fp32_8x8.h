// qrd_rls_fp32_8x8.h
// Purpose: 8x8 FP32 feedforward QRD-RLS adaptive beamforming HLS interface.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#ifndef QRD_RLS_FP32_8X8_H
#define QRD_RLS_FP32_8X8_H

#include <ap_int.h>
#include <hls_stream.h>

#define QRD_RLS_N 8

void qrd_rls_fp32_8x8(
    hls::stream<float> &x_real_in,
    hls::stream<float> &x_imag_in,
    hls::stream<float> &d_real_in,
    hls::stream<float> &d_imag_in,
    hls::stream<float> &w_real_out,
    hls::stream<float> &w_imag_out,
    hls::stream<float> &r_diag_out,
    float lambda,
    ap_uint<1> reset
);

void qrd_rls_fp32_8x8_core_stream(
    hls::stream<float> &x_real_in,
    hls::stream<float> &x_imag_in,
    hls::stream<float> &d_real_in,
    hls::stream<float> &d_imag_in,
    hls::stream<float> &w_real_out,
    hls::stream<float> &w_imag_out,
    hls::stream<float> &r_diag_out,
    float lambda,
    ap_uint<1> reset
);

void qrd_rls_fp32_8x8_core_state_stream(
    hls::stream<float> &x_real_in,
    hls::stream<float> &x_imag_in,
    hls::stream<float> &d_real_in,
    hls::stream<float> &d_imag_in,
    hls::stream<float> &w_real_out,
    hls::stream<float> &w_imag_out,
    hls::stream<float> &r_diag_out,
    float R_state[QRD_RLS_N][QRD_RLS_N][2],
    float z_state[QRD_RLS_N][2],
    float lambda,
    ap_uint<1> reset
);

#endif
