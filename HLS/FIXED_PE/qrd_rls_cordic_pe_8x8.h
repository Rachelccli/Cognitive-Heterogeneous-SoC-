// qrd_rls_cordic_pe_8x8.h
// Purpose: Paper-aligned fixed-point CORDIC QRD-RLS interface with separated QR update and back-substitution.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#ifndef QRD_RLS_CORDIC_PE_8X8_H
#define QRD_RLS_CORDIC_PE_8X8_H

#include "../../qrd_rls_cordic_8x8.h"

#ifndef MATRIX_SIZE
#define MATRIX_SIZE QRD_RLS_N
#endif

#ifndef CORDIC_ITER
#define CORDIC_ITER 16
#endif

#ifndef PARALLEL_K
#define PARALLEL_K 1
#endif

#ifndef DATA_WIDTH
#define DATA_WIDTH 18
#endif

#ifndef PE_ROW_PIPELINE
#define PE_ROW_PIPELINE 0
#endif

void qrd_rls_cordic_pe_8x8_update_state_stream(
    hls::stream<fix18_t> &x_real_in,
    hls::stream<fix18_t> &x_imag_in,
    hls::stream<fix18_t> &d_real_in,
    hls::stream<fix18_t> &d_imag_in,
    fix18_t R_state[QRD_RLS_N][QRD_RLS_N][2],
    fix18_t z_state[QRD_RLS_N][2],
    hls::stream<fix18_t> &r_diag_out,
    fix18_t lambda,
    ap_uint<1> reset,
    ap_uint<1> &overflow_flag
);

void qrd_rls_cordic_pe_8x8_backsub_state_stream(
    fix18_t R_state[QRD_RLS_N][QRD_RLS_N][2],
    fix18_t z_state[QRD_RLS_N][2],
    hls::stream<fix18_t> &w_real_out,
    hls::stream<fix18_t> &w_imag_out
);

void qrd_rls_cordic_pe_8x8_core_stream(
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

void qrd_rls_cordic_pe_8x8_update_top(
    hls::stream<fix18_t> &x_real_in,
    hls::stream<fix18_t> &x_imag_in,
    hls::stream<fix18_t> &d_real_in,
    hls::stream<fix18_t> &d_imag_in,
    hls::stream<fix18_t> &r_diag_out,
    fix18_t lambda,
    ap_uint<1> reset,
    ap_uint<1> &overflow_flag
);

void qrd_rls_cordic_pe_8x8(
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

#endif
