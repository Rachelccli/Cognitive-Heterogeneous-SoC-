// dual_precision_qrd_top.cpp
// Purpose: Low-power dual-precision QRD-RLS top-level IP with one active precision path.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#include "dual_precision_qrd_top.h"
#include "../COMMON/cond_estimator.h"
#include "../FIXED_PE/qrd_rls_cordic_pe_8x8.h"
#include "../FP32/qrd_rls_fp32_8x8.h"
#include "../COMMON/snapshot_normalizer.h"
#include "state_bridge.h"

static float dual_absf(float x) {
#pragma HLS INLINE
    return (x < 0.0f) ? -x : x;
}

static float dual_maxf(float a, float b) {
#pragma HLS INLINE
    return (a > b) ? a : b;
}

static float dual_clampf(float x, float lo, float hi) {
#pragma HLS INLINE
    float y = (x < lo) ? lo : x;
    return (y > hi) ? hi : y;
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
    static ap_uint<2> trigger_streak = 0;
    static ap_uint<3> calm_streak = 0;
    static ap_uint<6> fixed_reentry_cooldown_count = 0;
    static ap_uint<6> lambda_fast_hold_count = 0;
    static ap_uint<4> steady_highcond_streak = 0;
    static ap_uint<4> extreme_highcond_streak = 0;
    static float lambda_state = 0.955f;
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
        trigger_streak = 0;
        calm_streak = 0;
        fixed_reentry_cooldown_count = 0;
        lambda_fast_hold_count = 0;
        steady_highcond_streak = 0;
        extreme_highcond_streak = 0;
        lambda_state = dual_clampf(lambda, 0.87f, 0.995f);
    }
    ap_uint<1> selected_float = active_float_state;
    float lambda_eff = (mode == 2) ? dual_clampf(lambda_state, 0.87f, 0.995f) : dual_clampf(lambda, 0.87f, 0.995f);

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
        if (selected_float) {
            if (i < QRD_RLS_N) {
                fp_x_re_s.write(re_norm);
                fp_x_im_s.write(im_norm);
            } else {
                fp_d_re_s.write(re_norm);
                fp_d_im_s.write(im_norm);
            }
        } else {
            if (i < QRD_RLS_N) {
                fx_x_re_s.write((fix18_t)re_norm);
                fx_x_im_s.write((fix18_t)im_norm);
            } else {
                fx_d_re_s.write((fix18_t)re_norm);
                fx_d_im_s.write((fix18_t)im_norm);
            }
        }
    }

    if (selected_float) {
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
            lambda_eff,
            reset
        );
    } else {
        qrd_rls_cordic_pe_8x8_update_state_stream(
            fx_x_re_s,
            fx_x_im_s,
            fx_d_re_s,
            fx_d_im_s,
            fx_R_state,
            fx_z_state,
            fx_diag_s,
            (fix18_t)lambda_eff,
            reset,
            overflow_local
        );

        qrd_rls_cordic_pe_8x8_backsub_state_stream(
            fx_R_state,
            fx_z_state,
            fx_w_re_s,
            fx_w_im_s
        );
    }

    read_active_outputs:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        float w_re = 0.0f;
        float w_im = 0.0f;
        float diag = 1.0f;
        if (selected_float) {
            w_re = fp_w_re_s.read();
            w_im = fp_w_im_s.read();
            diag = fp_diag_s.read();
        } else {
            w_re = (float)fx_w_re_s.read();
            w_im = (float)fx_w_im_s.read();
            diag = (float)fx_diag_s.read();
        }
        diag_for_est_s.write(diag);
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

    // mode: 0=fixed, 1=float, 2=HYBRID_LP + rule-based VFF, 3=HYBRID_LP fixed-lambda
    ap_uint<1> target_float = 0;
    ap_uint<1> strong_trigger_for_vff = 0;
    ap_uint<1> calm_now_for_vff = 0;
    if (mode == 1) {
        target_float = 1;
        auto_hold_count = 0;
        trigger_streak = 0;
        calm_streak = 0;
    } else if (mode == 2 || mode == 3) {
        const float threshold = (cond_threshold > 0.0f) ? cond_threshold : 1000.0f;
        const float enter_cond = dual_maxf(threshold * 0.92f, 220.0f);
        const float exit_cond = threshold * 0.72f;
        const float enter_delta = 0.34f;
        const float exit_delta = 0.16f;
        const ap_uint<2> enter_streak_req = 2;
        const ap_uint<3> calm_streak_req = 5;
        const ap_uint<6> fixed_reentry_cooldown_window = 32;
        ap_uint<1> strong_trigger = 0;
        ap_uint<1> calm_now = 0;

        if (fixed_reentry_cooldown_count > 0) {
            fixed_reentry_cooldown_count = fixed_reentry_cooldown_count - 1;
        }

        if (overflow_local) {
            strong_trigger = 1;
        } else if (cond_local > enter_cond) {
            strong_trigger = 1;
        } else if (cond_local > (threshold * 0.75f) && delta_norm_local > enter_delta) {
            strong_trigger = 1;
        } else if (!active_float_state &&
                   fixed_reentry_cooldown_count == 0 &&
                   switch_local &&
                   scale_local > 1.00f &&
                   cond_local > (threshold * 0.50f)) {
            // HYBRID_LP_DYNAMIC_RANGE_TRIGGER:
            // In multi-jammer / bursty scenes the diagonal-only condition proxy can stay moderate
            // while the input dynamic range is already much larger than steady tracking cases.
            strong_trigger = 1;
        }

        if (cond_local < exit_cond &&
            delta_norm_local < exit_delta &&
            scale_local < 1.45f &&
            !overflow_local) {
            calm_now = 1;
        }
        strong_trigger_for_vff = strong_trigger;
        calm_now_for_vff = calm_now;

        if (strong_trigger) {
            if (trigger_streak < enter_streak_req) {
                trigger_streak = trigger_streak + 1;
            }
            calm_streak = 0;
        } else if (calm_now) {
            trigger_streak = 0;
            if (calm_streak < calm_streak_req) {
                calm_streak = calm_streak + 1;
            }
        } else {
            trigger_streak = 0;
            calm_streak = 0;
        }

        if (!active_float_state) {
            if (overflow_local || trigger_streak >= enter_streak_req) {
                target_float = 1;
                auto_hold_count = auto_hold_window;
            } else {
                target_float = 0;
            }
        } else {
            target_float = 1;
            if (overflow_local || strong_trigger) {
                auto_hold_count = auto_hold_window;
            } else if (auto_hold_count > 0) {
                auto_hold_count = auto_hold_count - 1;
            } else if (calm_streak >= calm_streak_req) {
                target_float = 0;
                fixed_reentry_cooldown_count = fixed_reentry_cooldown_window;
            }
        }
    } else {
        target_float = 0;
        auto_hold_count = 0;
        trigger_streak = 0;
        calm_streak = 0;
        fixed_reentry_cooldown_count = 0;
    }

    float lambda_next = dual_clampf(lambda, 0.87f, 0.995f);
    if (mode == 2) {
        const float threshold = (cond_threshold > 0.0f) ? cond_threshold : 1000.0f;
        const float lambda_base = dual_clampf(lambda, 0.90f, 0.995f);
        const float lambda_mid = dual_clampf(lambda_base - 0.025f, 0.90f, lambda_base);
        const float lambda_fast = dual_clampf(lambda_base - 0.055f, 0.87f, lambda_base);
        const ap_uint<6> lambda_fast_hold_window = 40;

        ap_uint<1> burst_like_event = 0;
        if (delta_norm_local > 1.00f) {
            burst_like_event = 1;
        } else if (scale_local > 8.00f &&
                   delta_norm_local > 0.20f &&
                   (target_float || auto_hold_count > 0 || switch_local)) {
            burst_like_event = 1;
        } else if (cond_local > threshold * 1.50f && delta_norm_local > 0.20f) {
            burst_like_event = 1;
        }

        if (burst_like_event) {
            lambda_fast_hold_count = lambda_fast_hold_window;
        } else if (lambda_fast_hold_count > 0) {
            lambda_fast_hold_count = lambda_fast_hold_count - 1;
        }

        ap_uint<1> steady_highcond = 0;
        if (active_float_state &&
            cond_local > threshold * 4.0f &&
            delta_norm_local < 0.05f &&
            scale_local > 8.0f) {
            steady_highcond = 1;
        }
        if (steady_highcond) {
            if (steady_highcond_streak < 15) {
                steady_highcond_streak = steady_highcond_streak + 1;
            }
        } else {
            steady_highcond_streak = 0;
        }

        ap_uint<1> extreme_highcond = 0;
        if (active_float_state &&
            cond_local > threshold * 24.0f &&
            delta_norm_local < 0.05f &&
            scale_local > 20.0f) {
            extreme_highcond = 1;
        }
        if (extreme_highcond) {
            if (extreme_highcond_streak < 15) {
                extreme_highcond_streak = extreme_highcond_streak + 1;
            }
        } else {
            extreme_highcond_streak = 0;
        }

        if (overflow_local) {
            lambda_next = lambda_fast;
        } else if (extreme_highcond_streak >= 6) {
            lambda_next = lambda_base;
        } else if (steady_highcond_streak >= 6) {
            lambda_next = lambda_mid;
        } else if (lambda_fast_hold_count > 0) {
            lambda_next = lambda_fast;
        } else if (target_float ||
                   strong_trigger_for_vff ||
                   switch_local ||
                   cond_local > threshold * 0.80f ||
                   scale_local > 1.40f) {
            lambda_next = lambda_mid;
        } else if (calm_now_for_vff) {
            lambda_next = lambda_base;
        } else {
            lambda_next = lambda_eff;
        }
    } else if (mode == 3) {
        lambda_next = dual_clampf(lambda, 0.87f, 0.995f);
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
    float lambda_transit_local = lambda_eff;
    if (!reset && target_float != active_float_state) {
        migrate_dir_local = active_float_state ? ap_uint<1>(0) : ap_uint<1>(1);
        if (migrate_dir_local == 0) {
            state_bridge_f2c_serial(
                migrate_done_local,
                fp_R_state,
                fp_z_state,
                fx_R_state,
                fx_z_state,
                lambda_eff,
                lambda_transit_local
            );
        } else {
            state_bridge_c2f_serial(
                migrate_done_local,
                fp_R_state,
                fp_z_state,
                fx_R_state,
                fx_z_state,
                lambda_eff,
                lambda_transit_local
            );
        }
        if (migrate_done_local) {
            active_float_state = target_float;
            if (migrate_dir_local == 0) {
                lambda_next = dual_clampf(lambda_transit_local, 0.87f, 0.995f);
            }
        }
    }
    lambda_state = ((mode == 2) || (mode == 3)) ? dual_clampf(lambda_next, 0.87f, 0.995f) : dual_clampf(lambda, 0.87f, 0.995f);

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
    lambda_transit_out = lambda_state;
}
