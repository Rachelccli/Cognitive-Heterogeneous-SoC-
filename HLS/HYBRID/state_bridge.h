// state_bridge.h
// Purpose: BRAM-backed R-matrix precision migration interface.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#ifndef STATE_BRIDGE_H
#define STATE_BRIDGE_H

#include "../../qrd_rls_cordic_8x8.h"
#include <ap_int.h>

void state_bridge(
    ap_uint<2> mode,
    ap_uint<1> direction,
    ap_uint<1> &done,
    float R_fp32_in[QRD_RLS_N][QRD_RLS_N][2],
    float R_fp32_out[QRD_RLS_N][QRD_RLS_N][2],
    float z_fp32_in[QRD_RLS_N][2],
    float z_fp32_out[QRD_RLS_N][2],
    fix18_t R_cordic_in[QRD_RLS_N][QRD_RLS_N][2],
    fix18_t R_cordic_out[QRD_RLS_N][QRD_RLS_N][2],
    fix18_t z_cordic_in[QRD_RLS_N][2],
    fix18_t z_cordic_out[QRD_RLS_N][2],
    float lambda_in,
    float &lambda_transit_out
);

void state_bridge_f2c_serial(
    ap_uint<1> &done,
    float R_fp32_state[QRD_RLS_N][QRD_RLS_N][2],
    float z_fp32_state[QRD_RLS_N][2],
    fix18_t R_cordic_state[QRD_RLS_N][QRD_RLS_N][2],
    fix18_t z_cordic_state[QRD_RLS_N][2],
    float lambda_in,
    float &lambda_transit_out
);

void state_bridge_c2f_serial(
    ap_uint<1> &done,
    float R_fp32_state[QRD_RLS_N][QRD_RLS_N][2],
    float z_fp32_state[QRD_RLS_N][2],
    fix18_t R_cordic_state[QRD_RLS_N][QRD_RLS_N][2],
    fix18_t z_cordic_state[QRD_RLS_N][2],
    float lambda_in,
    float &lambda_transit_out
);

#endif
