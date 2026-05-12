// dual_precision_qrd_top.cpp
// Purpose: Minimal dual-precision QRD-RLS top-level IP with fixed/float mux.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#include "dual_precision_qrd_top.h"
#include "cond_estimator.h"
#include "../FIXED/qrd_rls_cordic_folded_8x8.h"
#include "../FP32/qrd_rls_fp32_8x8.h"
#include "snapshot_normalizer.h"
#include "state_bridge.h"

static float dual_absf(float x) {
#pragma HLS INLINE
    return (x < 0.0f) ? -x : x;
}

void dual_precision_qrd_top(
    hls::stream<axis_cpx64_t> &snapshot_in,
    hls::stream<axis_cpx64_t> &weights_out,
    hls::stream<axis_float32_t> &delta_R_out,
    ap_uint<2> mode,
    float lambda,
    float cond_threshold,
    ap_uint<16> auto_hold_window,
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
) {
#pragma HLS INTERFACE mode=axis port=snapshot_in
#pragma HLS INTERFACE mode=axis port=weights_out
#pragma HLS INTERFACE mode=axis port=delta_R_out
#pragma HLS INTERFACE mode=s_axilite port=mode bundle=control
#pragma HLS INTERFACE mode=s_axilite port=lambda bundle=control
#pragma HLS INTERFACE mode=s_axilite port=cond_threshold bundle=control
#pragma HLS INTERFACE mode=s_axilite port=auto_hold_window bundle=control
#pragma HLS INTERFACE mode=s_axilite port=reset bundle=control
#pragma HLS INTERFACE mode=s_axilite port=enable_norm bundle=control
#pragma HLS INTERFACE mode=s_axilite port=norm_eps bundle=control
#pragma HLS INTERFACE mode=s_axilite port=scale_out bundle=control
#pragma HLS INTERFACE mode=s_axilite port=cond_estimate_out bundle=control
#pragma HLS INTERFACE mode=s_axilite port=delta_R_norm_out bundle=control
#pragma HLS INTERFACE mode=s_axilite port=switch_recommend bundle=control
#pragma HLS INTERFACE mode=s_axilite port=overflow_flag bundle=control
#pragma HLS INTERFACE mode=s_axilite port=frame_error bundle=control
#pragma HLS INTERFACE mode=s_axilite port=selected_float_out bundle=control
#pragma HLS INTERFACE mode=s_axilite port=migration_done bundle=control
#pragma HLS INTERFACE mode=s_axilite port=migration_direction bundle=control
#pragma HLS INTERFACE mode=s_axilite port=active_float_out bundle=control
#pragma HLS INTERFACE mode=s_axilite port=lambda_transit_out bundle=control
#pragma HLS INTERFACE mode=s_axilite port=return bundle=control

    float re_buf[QRD_FRAME_BEATS];
    float im_buf[QRD_FRAME_BEATS];
#pragma HLS ARRAY_PARTITION variable=re_buf complete dim=1
#pragma HLS ARRAY_PARTITION variable=im_buf complete dim=1

    static float fp_R_state[QRD_RLS_N][QRD_RLS_N][2];
    static float fp_z_state[QRD_RLS_N][2];
    static fix18_t fx_R_state[QRD_RLS_N][QRD_RLS_N][2];
    static fix18_t fx_z_state[QRD_RLS_N][2];
    static ap_uint<1> active_float_state = 0;
    static ap_uint<16> auto_hold_count = 0;
#pragma HLS ARRAY_PARTITION variable=fp_R_state complete dim=2
#pragma HLS ARRAY_PARTITION variable=fp_R_state complete dim=3
#pragma HLS ARRAY_PARTITION variable=fp_z_state complete dim=1
#pragma HLS ARRAY_PARTITION variable=fp_z_state complete dim=2

    hls::stream<fix18_t> fx_x_re_s;
    hls::stream<fix18_t> fx_x_im_s;
    hls::stream<fix18_t> fx_d_re_s;
    hls::stream<fix18_t> fx_d_im_s;
    hls::stream<fix18_t> fx_w_re_s;
    hls::stream<fix18_t> fx_w_im_s;
    hls::stream<fix18_t> fx_diag_s;
    hls::stream<float> fp_x_re_s;
    hls::stream<float> fp_x_im_s;
    hls::stream<float> fp_d_re_s;
    hls::stream<float> fp_d_im_s;
    hls::stream<float> fp_w_re_s;
    hls::stream<float> fp_w_im_s;
    hls::stream<float> fp_diag_s;
    hls::stream<float> diag_for_est_s;
    hls::stream<float> delta_internal_s;
#pragma HLS STREAM variable=fx_x_re_s depth=8
#pragma HLS STREAM variable=fx_x_im_s depth=8
#pragma HLS STREAM variable=fx_d_re_s depth=1
#pragma HLS STREAM variable=fx_d_im_s depth=1
#pragma HLS STREAM variable=fx_w_re_s depth=8
#pragma HLS STREAM variable=fx_w_im_s depth=8
#pragma HLS STREAM variable=fx_diag_s depth=8
#pragma HLS STREAM variable=fp_x_re_s depth=8
#pragma HLS STREAM variable=fp_x_im_s depth=8
#pragma HLS STREAM variable=fp_d_re_s depth=1
#pragma HLS STREAM variable=fp_d_im_s depth=1
#pragma HLS STREAM variable=fp_w_re_s depth=8
#pragma HLS STREAM variable=fp_w_im_s depth=8
#pragma HLS STREAM variable=fp_diag_s depth=8
#pragma HLS STREAM variable=diag_for_est_s depth=8
#pragma HLS STREAM variable=delta_internal_s depth=8

    ap_uint<1> requested_float = 0;
    if (mode == 1) {
        requested_float = 1;
    }
    if (reset) {
        active_float_state = requested_float;
        auto_hold_count = 0;
    }
    ap_uint<1> selected_float = active_float_state;

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

        float abs_re = dual_absf(re);
        float abs_im = dual_absf(im);
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

    write_inputs:
    for (int i = 0; i < QRD_FRAME_BEATS; i++) {
#pragma HLS PIPELINE II=1
        float re_norm = re_buf[i] * inv_scale;
        float im_norm = im_buf[i] * inv_scale;
        if (i < QRD_RLS_N) {
            fx_x_re_s.write((fix18_t)re_norm);
            fx_x_im_s.write((fix18_t)im_norm);
            fp_x_re_s.write(re_norm);
            fp_x_im_s.write(im_norm);
        } else {
            fx_d_re_s.write((fix18_t)re_norm);
            fx_d_im_s.write((fix18_t)im_norm);
            fp_d_re_s.write(re_norm);
            fp_d_im_s.write(im_norm);
        }
    }

    qrd_rls_cordic_folded_8x8_core_state_stream(
        fx_x_re_s,
        fx_x_im_s,
        fx_d_re_s,
        fx_d_im_s,
        fx_w_re_s,
        fx_w_im_s,
        fx_diag_s,
        fx_R_state,
        fx_z_state,
        (fix18_t)lambda,
        reset,
        overflow_local
    );

    qrd_rls_fp32_8x8_core_state_stream(
        fp_x_re_s,
        fp_x_im_s,
        fp_d_re_s,
        fp_d_im_s,
        fp_w_re_s,
        fp_w_im_s,
        fp_diag_s,
        fp_R_state,
        fp_z_state,
        lambda,
        reset
    );

    read_select_outputs:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        float fx_w_re = (float)fx_w_re_s.read();
        float fx_w_im = (float)fx_w_im_s.read();
        float fp_w_re = fp_w_re_s.read();
        float fp_w_im = fp_w_im_s.read();
        float fx_diag = (float)fx_diag_s.read();
        (void)fp_diag_s.read();

        diag_for_est_s.write(fx_diag);
        float w_re = selected_float ? fp_w_re : fx_w_re;
        float w_im = selected_float ? fp_w_im : fx_w_im;
        ap_uint<1> last = (i == QRD_RLS_N - 1) ? ap_uint<1>(1) : ap_uint<1>(0);
        weights_out.write(qrd_pack_cpx(w_re, w_im, last, 0));
    }

    float cond_local = 0.0f;
    float delta_norm_local = 0.0f;
    ap_uint<1> switch_local = 0;
    cond_estimator(
        diag_for_est_s,
        cond_threshold,
        cond_local,
        delta_norm_local,
        delta_internal_s,
        switch_local,
        reset
    );

    // mode: 0=fixed, 1=float, 2=auto with hold-window hysteresis
    ap_uint<1> target_float = 0;
    if (mode == 1) {
        target_float = 1;
        auto_hold_count = 0;
    } else if (mode == 2) {
        ap_uint<1> auto_trigger = (switch_local || overflow_local) ? ap_uint<1>(1) : ap_uint<1>(0);
        if (auto_trigger) {
            target_float = 1;
            auto_hold_count = auto_hold_window;
        } else if (active_float_state) {
            if (auto_hold_count > 0) {
                target_float = 1;
                auto_hold_count = auto_hold_count - 1;
            } else {
                target_float = 0;
            }
        } else {
            target_float = 0;
            auto_hold_count = 0;
        }
    } else {
        target_float = 0;
        auto_hold_count = 0;
    }

    write_delta:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        float delta = delta_internal_s.read();
        ap_uint<1> last = (i == QRD_RLS_N - 1) ? ap_uint<1>(1) : ap_uint<1>(0);
        delta_R_out.write(qrd_pack_float(delta, last, 0));
    }

    ap_uint<1> migrate_done_local = 0;
    ap_uint<1> migrate_dir_local = 0;
    float lambda_transit_local = lambda;
    if (!reset && target_float != active_float_state) {
        migrate_dir_local = active_float_state ? ap_uint<1>(0) : ap_uint<1>(1);
        if (migrate_dir_local == 0) {
            state_bridge_f2c_serial(
                migrate_done_local,
                fp_R_state,
                fp_z_state,
                fx_R_state,
                fx_z_state,
                lambda,
                lambda_transit_local
            );
        } else {
            state_bridge_c2f_serial(
                migrate_done_local,
                fp_R_state,
                fp_z_state,
                fx_R_state,
                fx_z_state,
                lambda,
                lambda_transit_local
            );
        }
        if (migrate_done_local) {
            active_float_state = target_float;
        }
    }

    scale_out = scale_local;
    cond_estimate_out = cond_local;
    delta_R_norm_out = delta_norm_local;
    switch_recommend = switch_local;
    overflow_flag = overflow_local;
    frame_error = norm_error;
    selected_float_out = selected_float;
    migration_done = migrate_done_local;
    migration_direction = migrate_dir_local;
    active_float_out = active_float_state;
    lambda_transit_out = lambda_transit_local;
}
