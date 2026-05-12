// qrd_fixed_pe_monitor_top.cpp
// Purpose: Thin engineering wrapper for the paper-aligned fixed-point CORDIC QRD-RLS path.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#include "qrd_fixed_pe_monitor_top.h"
#include "../FIXED/snapshot_normalizer.h"
#include "../FIXED/cond_estimator.h"
#include "qrd_rls_cordic_pe_8x8.h"

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
) {
#pragma HLS INTERFACE mode=axis port=snapshot_in
#pragma HLS INTERFACE mode=axis port=weights_out
#pragma HLS INTERFACE mode=axis port=delta_R_out
#pragma HLS INTERFACE mode=s_axilite port=lambda bundle=control
#pragma HLS INTERFACE mode=s_axilite port=cond_threshold bundle=control
#pragma HLS INTERFACE mode=s_axilite port=reset bundle=control
#pragma HLS INTERFACE mode=s_axilite port=enable_norm bundle=control
#pragma HLS INTERFACE mode=s_axilite port=norm_eps bundle=control
#pragma HLS INTERFACE mode=s_axilite port=scale_out bundle=control
#pragma HLS INTERFACE mode=s_axilite port=cond_estimate_out bundle=control
#pragma HLS INTERFACE mode=s_axilite port=delta_R_norm_out bundle=control
#pragma HLS INTERFACE mode=s_axilite port=switch_recommend bundle=control
#pragma HLS INTERFACE mode=s_axilite port=overflow_flag bundle=control
#pragma HLS INTERFACE mode=s_axilite port=frame_error bundle=control
#pragma HLS INTERFACE mode=s_axilite port=return bundle=control

    float re_buf[QRD_FRAME_BEATS];
    float im_buf[QRD_FRAME_BEATS];
    hls::stream<fix18_t> x_re_s;
    hls::stream<fix18_t> x_im_s;
    hls::stream<fix18_t> d_re_s;
    hls::stream<fix18_t> d_im_s;
    hls::stream<fix18_t> w_re_s;
    hls::stream<fix18_t> w_im_s;
    hls::stream<fix18_t> diag_fix_s;
    hls::stream<float> diag_float_s;
    hls::stream<float> delta_float_s;
#pragma HLS ARRAY_PARTITION variable=re_buf complete dim=1
#pragma HLS ARRAY_PARTITION variable=im_buf complete dim=1
#pragma HLS STREAM variable=x_re_s depth=QRD_RLS_N
#pragma HLS STREAM variable=x_im_s depth=QRD_RLS_N
#pragma HLS STREAM variable=d_re_s depth=1
#pragma HLS STREAM variable=d_im_s depth=1
#pragma HLS STREAM variable=w_re_s depth=QRD_RLS_N
#pragma HLS STREAM variable=w_im_s depth=QRD_RLS_N
#pragma HLS STREAM variable=diag_fix_s depth=QRD_RLS_N
#pragma HLS STREAM variable=diag_float_s depth=QRD_RLS_N
#pragma HLS STREAM variable=delta_float_s depth=QRD_RLS_N

    static fix18_t R_state[QRD_RLS_N][QRD_RLS_N][2];
    static fix18_t z_state[QRD_RLS_N][2];
#pragma HLS ARRAY_PARTITION variable=R_state complete dim=2
#pragma HLS ARRAY_PARTITION variable=R_state complete dim=3
#pragma HLS ARRAY_PARTITION variable=z_state complete dim=1
#pragma HLS ARRAY_PARTITION variable=z_state complete dim=2

    ap_uint<1> norm_error = 0;
    ap_uint<1> overflow_local = 0;

    float max_abs = 0.0f;
read_frame:
    for (int i = 0; i < QRD_FRAME_BEATS; i++) {
#pragma HLS PIPELINE II=1
        axis_cpx64_t pkt = snapshot_in.read();
        float re;
        float im;
        qrd_unpack_cpx(pkt, re, im);
        re_buf[i] = re;
        im_buf[i] = im;

        float abs_re = (re < 0.0f) ? -re : re;
        float abs_im = (im < 0.0f) ? -im : im;
        float local_max = (abs_re > abs_im) ? abs_re : abs_im;
        if (local_max > max_abs) {
            max_abs = local_max;
        }

        if ((i < QRD_FRAME_BEATS - 1 && pkt.last) || (i == QRD_FRAME_BEATS - 1 && !pkt.last)) {
            norm_error = 1;
        }
    }

    float safe_eps = (norm_eps > 0.0f) ? norm_eps : 1.0e-6f;
    float target_peak = (QRD_NORMALIZER_TARGET_PEAK > 0.0f) ? QRD_NORMALIZER_TARGET_PEAK : 1.0f;
    float scale_local = (enable_norm && max_abs > safe_eps) ? (max_abs / target_peak) : 1.0f;
    float inv_scale = 1.0f / scale_local;

write_core_inputs:
    for (int i = 0; i < QRD_FRAME_BEATS; i++) {
#pragma HLS PIPELINE II=1
        fix18_t re_norm = (fix18_t)(re_buf[i] * inv_scale);
        fix18_t im_norm = (fix18_t)(im_buf[i] * inv_scale);
        if (i < QRD_RLS_N) {
            x_re_s.write(re_norm);
            x_im_s.write(im_norm);
        } else {
            d_re_s.write(re_norm);
            d_im_s.write(im_norm);
        }
    }

    qrd_rls_cordic_pe_8x8_update_state_stream(
        x_re_s,
        x_im_s,
        d_re_s,
        d_im_s,
        R_state,
        z_state,
        diag_fix_s,
        (fix18_t)lambda,
        reset,
        overflow_local
    );

    qrd_rls_cordic_pe_8x8_backsub_state_stream(
        R_state,
        z_state,
        w_re_s,
        w_im_s
    );

write_weights:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        float w_re = (float)w_re_s.read();
        float w_im = (float)w_im_s.read();
        ap_uint<1> last = (i == QRD_RLS_N - 1) ? ap_uint<1>(1) : ap_uint<1>(0);
        weights_out.write(qrd_pack_cpx(w_re, w_im, last, 0));
    }

diag_to_float:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        diag_float_s.write((float)diag_fix_s.read());
    }

    float cond_local = 0.0f;
    float delta_norm_local = 0.0f;
    ap_uint<1> switch_local = 0;
    cond_estimator(
        diag_float_s,
        cond_threshold,
        cond_local,
        delta_norm_local,
        delta_float_s,
        switch_local,
        reset
    );

write_delta:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        float delta = delta_float_s.read();
        ap_uint<1> last = (i == QRD_RLS_N - 1) ? ap_uint<1>(1) : ap_uint<1>(0);
        delta_R_out.write(qrd_pack_float(delta, last, 0));
    }

    scale_out = scale_local;
    cond_estimate_out = cond_local;
    delta_R_norm_out = delta_norm_local;
    switch_recommend = switch_local;
    overflow_flag = overflow_local;
    frame_error = norm_error;
}
