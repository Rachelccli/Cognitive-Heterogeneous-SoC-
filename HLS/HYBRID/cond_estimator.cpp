// cond_estimator.cpp
// Purpose: Low-cost diagonal condition estimator and Delta R_ii feature stream.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#include "cond_estimator.h"
#include "hls_math.h"

static float abs_float_hls(float x) {
#pragma HLS INLINE
    return (x < 0.0f) ? -x : x;
}

static float max2_float(float a, float b) {
#pragma HLS INLINE
    return (a > b) ? a : b;
}

static float min2_float(float a, float b) {
#pragma HLS INLINE
    return (a < b) ? a : b;
}

void cond_estimator(
    hls::stream<float> &R_diag_in,
    float cond_threshold,
    float &cond_estimate_out,
    float &delta_R_norm_out,
    hls::stream<float> &delta_R_out,
    ap_uint<1> &switch_recommend,
    ap_uint<1> reset
) {
#pragma HLS INTERFACE mode=axis port=R_diag_in
#pragma HLS INTERFACE mode=axis port=delta_R_out
#pragma HLS INTERFACE mode=s_axilite port=cond_threshold bundle=control
#pragma HLS INTERFACE mode=s_axilite port=cond_estimate_out bundle=control
#pragma HLS INTERFACE mode=s_axilite port=delta_R_norm_out bundle=control
#pragma HLS INTERFACE mode=s_axilite port=switch_recommend bundle=control
#pragma HLS INTERFACE mode=s_axilite port=reset bundle=control
#pragma HLS INTERFACE mode=s_axilite port=return bundle=control

    static float R_diag_prev[QRD_RLS_N] = {
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
    };
    static float sigma_sq[QRD_RLS_N] = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
    };
#pragma HLS ARRAY_PARTITION variable=R_diag_prev complete dim=1
#pragma HLS ARRAY_PARTITION variable=sigma_sq complete dim=1

    if (reset) {
        reset_loop:
        for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
            R_diag_prev[i] = 1.0f;
            sigma_sq[i] = 0.0f;
        }
    }

    float mag[QRD_RLS_N];
    float delta_sq[QRD_RLS_N];
    float sigma_root[QRD_RLS_N];
#pragma HLS ARRAY_PARTITION variable=mag complete dim=1
#pragma HLS ARRAY_PARTITION variable=delta_sq complete dim=1
#pragma HLS ARRAY_PARTITION variable=sigma_root complete dim=1

    feature_loop:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        float curr = R_diag_in.read();
        mag[i] = abs_float_hls(curr);
        if (mag[i] < 1.0e-9f) {
            mag[i] = 1.0e-9f;
        }
        float delta = curr - R_diag_prev[i];
        delta_R_out.write(delta);
        delta_sq[i] = delta * delta;
        sigma_sq[i] = 0.95f * sigma_sq[i] + 0.05f * delta * delta;
        sigma_root[i] = hls::sqrt(sigma_sq[i]);
        R_diag_prev[i] = curr;
    }

    float max01 = max2_float(mag[0], mag[1]);
    float max23 = max2_float(mag[2], mag[3]);
    float max45 = max2_float(mag[4], mag[5]);
    float max67 = max2_float(mag[6], mag[7]);
    float max03 = max2_float(max01, max23);
    float max47 = max2_float(max45, max67);
    float max_diag = max2_float(max03, max47);

    float min01 = min2_float(mag[0], mag[1]);
    float min23 = min2_float(mag[2], mag[3]);
    float min45 = min2_float(mag[4], mag[5]);
    float min67 = min2_float(mag[6], mag[7]);
    float min03 = min2_float(min01, min23);
    float min47 = min2_float(min45, min67);
    float min_diag = min2_float(min03, min47);

    float ds01 = delta_sq[0] + delta_sq[1];
    float ds23 = delta_sq[2] + delta_sq[3];
    float ds45 = delta_sq[4] + delta_sq[5];
    float ds67 = delta_sq[6] + delta_sq[7];
    float delta_norm_sq = (ds01 + ds23) + (ds45 + ds67);

    float ss01 = sigma_root[0] + sigma_root[1];
    float ss23 = sigma_root[2] + sigma_root[3];
    float ss45 = sigma_root[4] + sigma_root[5];
    float ss67 = sigma_root[6] + sigma_root[7];
    float sigma_sum = (ss01 + ss23) + (ss45 + ss67);

    if (min_diag < 1.0e-9f) {
        min_diag = 1.0e-9f;
    }
    float threshold = cond_threshold;
    if (threshold <= 0.0f) {
        threshold = 1000.0f;
    }

    float cond_est = max_diag / min_diag;
    float delta_norm = hls::sqrt(delta_norm_sq);
    float mean_sigma = sigma_sum * 0.125f;
    ap_uint<1> trigger = 0;
    if (cond_est > threshold || delta_norm > (3.0f * mean_sigma)) {
        trigger = 1;
    }

    cond_estimate_out = cond_est;
    delta_R_norm_out = delta_norm;
    switch_recommend = trigger;
}
