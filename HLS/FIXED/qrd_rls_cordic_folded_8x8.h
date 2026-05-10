// qrd_rls_cordic_folded_8x8.h
// Purpose: Folded 8x8 fixed-point CORDIC QRD-RLS HLS interface.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#ifndef QRD_RLS_CORDIC_FOLDED_8X8_H
#define QRD_RLS_CORDIC_FOLDED_8X8_H

#include "qrd_rls_cordic_8x8.h"

void qrd_rls_cordic_folded_8x8(
    hls::stream<fix18_t> &x_real_in,
    hls::stream<fix18_t> &x_imag_in,
    hls::stream<fix18_t> &d_real_in,
    hls::stream<fix18_t> &d_imag_in,
    hls::stream<fix18_t> &w_real_out,
    hls::stream<fix18_t> &w_imag_out,
    hls::stream<fix18_t> &r_diag_out,
    fix18_t lambda,
    ap_uint<1> reset,
    ap_uint<1> &overflow_flag
);

void qrd_rls_cordic_folded_8x8_core_stream(
    hls::stream<fix18_t> &x_real_in,
    hls::stream<fix18_t> &x_imag_in,
    hls::stream<fix18_t> &d_real_in,
    hls::stream<fix18_t> &d_imag_in,
    hls::stream<fix18_t> &w_real_out,
    hls::stream<fix18_t> &w_imag_out,
    hls::stream<fix18_t> &r_diag_out,
    fix18_t lambda,
    ap_uint<1> reset,
    ap_uint<1> &overflow_flag
);

void qrd_rls_cordic_folded_8x8_core_state_stream(
    hls::stream<fix18_t> &x_real_in,
    hls::stream<fix18_t> &x_imag_in,
    hls::stream<fix18_t> &d_real_in,
    hls::stream<fix18_t> &d_imag_in,
    hls::stream<fix18_t> &w_real_out,
    hls::stream<fix18_t> &w_imag_out,
    hls::stream<fix18_t> &r_diag_out,
    fix18_t R_state[QRD_RLS_N][QRD_RLS_N][2],
    fix18_t z_state[QRD_RLS_N][2],
    fix18_t lambda,
    ap_uint<1> reset,
    ap_uint<1> &overflow_flag
);

#endif
