// state_bridge.cpp
// Purpose: BRAM-backed state bridge for FP32/CORDIC R-matrix migration.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#include "state_bridge.h"

static fix18_t fp32_to_fix18_sat(float x) {
#pragma HLS INLINE
    float y = x;
    if (y > 7.9999f) {
        y = 7.9999f;
    }
    if (y < -8.0f) {
        y = -8.0f;
    }
    return (fix18_t)y;
}

static float fix18_to_fp32(fix18_t x) {
#pragma HLS INLINE
    return (float)x;
}

void state_bridge_f2c_serial(
    ap_uint<1> &done,
    float R_fp32_state[QRD_RLS_N][QRD_RLS_N][2],
    float z_fp32_state[QRD_RLS_N][2],
    fix18_t R_cordic_state[QRD_RLS_N][QRD_RLS_N][2],
    fix18_t z_cordic_state[QRD_RLS_N][2],
    float lambda_in,
    float &lambda_transit_out
) {
#pragma HLS INLINE off
    static float f2c_bram[QRD_RLS_N * QRD_RLS_N + QRD_RLS_N][2];
#pragma HLS BIND_STORAGE variable=f2c_bram type=RAM_2P impl=BRAM

    done = 0;
    float reduced_lambda = lambda_in - 0.05f;
    if (reduced_lambda < 0.87f) {
        reduced_lambda = 0.87f;
    }
    lambda_transit_out = reduced_lambda;

    f2c_save_R:
    for (int idx = 0; idx < QRD_RLS_N * QRD_RLS_N; idx++) {
#pragma HLS PIPELINE II=1
        int i = idx >> 3;
        int j = idx & 7;
        float re = 0.0f;
        float im = 0.0f;
        if (i <= j) {
            re = R_fp32_state[i][j][0];
            im = R_fp32_state[i][j][1];
        }
        f2c_bram[idx][0] = re;
        f2c_bram[idx][1] = im;
    }

    f2c_save_z:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        int addr = QRD_RLS_N * QRD_RLS_N + i;
        f2c_bram[addr][0] = z_fp32_state[i][0];
        f2c_bram[addr][1] = z_fp32_state[i][1];
    }

    f2c_restore_R:
    for (int idx = 0; idx < QRD_RLS_N * QRD_RLS_N; idx++) {
#pragma HLS PIPELINE II=1
        int i = idx >> 3;
        int j = idx & 7;
        R_cordic_state[i][j][0] = fp32_to_fix18_sat(f2c_bram[idx][0]);
        R_cordic_state[i][j][1] = fp32_to_fix18_sat(f2c_bram[idx][1]);
    }

    f2c_restore_z:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        int addr = QRD_RLS_N * QRD_RLS_N + i;
        z_cordic_state[i][0] = fp32_to_fix18_sat(f2c_bram[addr][0]);
        z_cordic_state[i][1] = fp32_to_fix18_sat(f2c_bram[addr][1]);
    }

    done = 1;
}

void state_bridge_c2f_serial(
    ap_uint<1> &done,
    float R_fp32_state[QRD_RLS_N][QRD_RLS_N][2],
    float z_fp32_state[QRD_RLS_N][2],
    fix18_t R_cordic_state[QRD_RLS_N][QRD_RLS_N][2],
    fix18_t z_cordic_state[QRD_RLS_N][2],
    float lambda_in,
    float &lambda_transit_out
) {
#pragma HLS INLINE off
    static float c2f_bram[QRD_RLS_N * QRD_RLS_N + QRD_RLS_N][2];
#pragma HLS BIND_STORAGE variable=c2f_bram type=RAM_2P impl=BRAM

    done = 0;
    lambda_transit_out = lambda_in;

    c2f_save_R:
    for (int idx = 0; idx < QRD_RLS_N * QRD_RLS_N; idx++) {
#pragma HLS PIPELINE II=1
        int i = idx >> 3;
        int j = idx & 7;
        float re = 0.0f;
        float im = 0.0f;
        if (i <= j) {
            re = fix18_to_fp32(R_cordic_state[i][j][0]);
            im = fix18_to_fp32(R_cordic_state[i][j][1]);
        }
        c2f_bram[idx][0] = re;
        c2f_bram[idx][1] = im;
    }

    c2f_save_z:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        int addr = QRD_RLS_N * QRD_RLS_N + i;
        c2f_bram[addr][0] = fix18_to_fp32(z_cordic_state[i][0]);
        c2f_bram[addr][1] = fix18_to_fp32(z_cordic_state[i][1]);
    }

    c2f_restore_R:
    for (int idx = 0; idx < QRD_RLS_N * QRD_RLS_N; idx++) {
#pragma HLS PIPELINE II=1
        int i = idx >> 3;
        int j = idx & 7;
        R_fp32_state[i][j][0] = c2f_bram[idx][0];
        R_fp32_state[i][j][1] = c2f_bram[idx][1];
    }

    c2f_restore_z:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        int addr = QRD_RLS_N * QRD_RLS_N + i;
        z_fp32_state[i][0] = c2f_bram[addr][0];
        z_fp32_state[i][1] = c2f_bram[addr][1];
    }

    done = 1;
}

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
) {
#pragma HLS INTERFACE mode=s_axilite port=mode bundle=control
#pragma HLS INTERFACE mode=s_axilite port=direction bundle=control
#pragma HLS INTERFACE mode=s_axilite port=done bundle=control
#pragma HLS INTERFACE mode=bram port=R_fp32_in
#pragma HLS INTERFACE mode=bram port=R_fp32_out
#pragma HLS INTERFACE mode=bram port=z_fp32_in
#pragma HLS INTERFACE mode=bram port=z_fp32_out
#pragma HLS INTERFACE mode=bram port=R_cordic_in
#pragma HLS INTERFACE mode=bram port=R_cordic_out
#pragma HLS INTERFACE mode=bram port=z_cordic_in
#pragma HLS INTERFACE mode=bram port=z_cordic_out
#pragma HLS INTERFACE mode=s_axilite port=lambda_in bundle=control
#pragma HLS INTERFACE mode=s_axilite port=lambda_transit_out bundle=control
#pragma HLS INTERFACE mode=s_axilite port=return bundle=control

    static float bram[QRD_RLS_N * QRD_RLS_N + QRD_RLS_N][2];
#pragma HLS BIND_STORAGE variable=bram type=RAM_2P impl=BRAM

    done = 0;
    lambda_transit_out = lambda_in;

    if (mode == 0) {
        return;
    }

    if (mode == 1) {
        save_fp32_i:
        for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
            save_fp32_j:
            for (int j = 0; j < QRD_RLS_N; j++) {
                int addr = i * QRD_RLS_N + j;
                if (i <= j) {
                    bram[addr][0] = R_fp32_in[i][j][0];
                    bram[addr][1] = R_fp32_in[i][j][1];
                }
            }
            int z_addr = QRD_RLS_N * QRD_RLS_N + i;
            bram[z_addr][0] = z_fp32_in[i][0];
            bram[z_addr][1] = z_fp32_in[i][1];
        }
        done = 1;
        return;
    }

    if (mode == 2) {
        save_cordic_i:
        for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
            save_cordic_j:
            for (int j = 0; j < QRD_RLS_N; j++) {
                int addr = i * QRD_RLS_N + j;
                if (i <= j) {
                    bram[addr][0] = fix18_to_fp32(R_cordic_in[i][j][0]);
                    bram[addr][1] = fix18_to_fp32(R_cordic_in[i][j][1]);
                }
            }
            int z_addr = QRD_RLS_N * QRD_RLS_N + i;
            bram[z_addr][0] = fix18_to_fp32(z_cordic_in[i][0]);
            bram[z_addr][1] = fix18_to_fp32(z_cordic_in[i][1]);
        }
        done = 1;
        return;
    }

    if (mode == 3 && direction == 0) {
        state_bridge_f2c_serial(
            done,
            R_fp32_in,
            z_fp32_in,
            R_cordic_out,
            z_cordic_out,
            lambda_in,
            lambda_transit_out
        );
        return;
    }

    if (mode == 3 && direction == 1) {
        state_bridge_c2f_serial(
            done,
            R_fp32_out,
            z_fp32_out,
            R_cordic_in,
            z_cordic_in,
            lambda_in,
            lambda_transit_out
        );
        return;
    }
}
