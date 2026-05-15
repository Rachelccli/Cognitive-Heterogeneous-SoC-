// qrd_rls_cordic_8x8.h
// Purpose: 8x8 fixed-point CORDIC QRD-RLS adaptive beamforming HLS interface.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#ifndef QRD_RLS_CORDIC_8X8_H
#define QRD_RLS_CORDIC_8X8_H

#include <ap_fixed.h>
#include <ap_int.h>
#include <hls_stream.h>

#ifndef QRD_RLS_N
#define QRD_RLS_N 8
#endif

typedef ap_fixed<18, 4, AP_RND, AP_SAT_SYM> fix18_t;
typedef ap_fixed<20, 3, AP_RND, AP_SAT_SYM> angle_t;
typedef ap_fixed<36, 8, AP_TRN, AP_SAT_SYM> acc_t;

void qrd_rls_cordic_8x8(
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

void qrd_rls_cordic_8x8_core_stream(
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
