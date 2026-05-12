// qrd_rls_cordic_pe_8x8.cpp
// Purpose: Paper-aligned fixed-point CORDIC QRD-RLS core with separated QR update and back-substitution.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#include "qrd_rls_cordic_pe_8x8.h"
#include "hls_math.h"

struct pe_fix_complex_t {
    fix18_t re;
    fix18_t im;
};

struct pe_cordic_rot_result_t {
    fix18_t x;
    fix18_t y;
};

struct pe_cordic_dir_result_t {
    fix18_t r;
    ap_uint<16> dirs;
    ap_uint<1> pre_negate;
};

static const fix18_t PE_R_INIT = (fix18_t)0.010009765625f;
static const fix18_t PE_R_MIN = (fix18_t)0.0001220703125f;
static const fix18_t PE_W_CLIP = (fix18_t)4.0f;

static fix18_t pe_cordic_gain_scale(acc_t x) {
#pragma HLS INLINE
    // CORDIC_GAIN_SHIFT_ADD: 0.607252935 ~= 1/2 + 1/8 - 1/64 - 1/512 - 1/8192.
    // The approximation error is below one fix18 fractional LSB.
    acc_t y = (x >> 1) + (x >> 3) - (x >> 6) - (x >> 9) - (x >> 13);
    return (fix18_t)y;
}

template<int ITER = CORDIC_ITER>
static pe_cordic_dir_result_t pe_cordic_vectoring_dirs(fix18_t x, fix18_t y) {
#pragma HLS INLINE
    acc_t x_acc = (acc_t)x;
    acc_t y_acc = (acc_t)y;
    ap_uint<16> dirs = 0;
    ap_uint<1> pre_negate = 0;

    if (x_acc < (acc_t)0.0f) {
        x_acc = -x_acc;
        y_acc = -y_acc;
        pre_negate = 1;
    }

pe_vec_dir_iter:
    for (int i = 0; i < ITER; i++) {
#if PARALLEL_K > 1
#pragma HLS UNROLL factor=PARALLEL_K
#endif
        acc_t x_shift = x_acc >> i;
        acc_t y_shift = y_acc >> i;
        acc_t x_next;
        acc_t y_next;
        if (y_acc >= (acc_t)0.0f) {
            x_next = x_acc + y_shift;
            y_next = y_acc - x_shift;
            dirs[i] = 1;
        } else {
            x_next = x_acc - y_shift;
            y_next = y_acc + x_shift;
            dirs[i] = 0;
        }
        x_acc = x_next;
        y_acc = y_next;
    }

    pe_cordic_dir_result_t out;
    out.r = pe_cordic_gain_scale(x_acc);
    out.dirs = dirs;
    out.pre_negate = pre_negate;
    return out;
}

template<int ITER = CORDIC_ITER>
static pe_cordic_rot_result_t pe_cordic_apply_dirs(
    fix18_t x,
    fix18_t y,
    ap_uint<16> dirs,
    ap_uint<1> pre_negate,
    ap_uint<1> inverse
) {
#pragma HLS INLINE
    acc_t x_acc = (acc_t)x;
    acc_t y_acc = (acc_t)y;

    if (pre_negate) {
        x_acc = -x_acc;
        y_acc = -y_acc;
    }

pe_apply_dir_iter:
    for (int i = 0; i < ITER; i++) {
#if PARALLEL_K > 1
#pragma HLS UNROLL factor=PARALLEL_K
#endif
        ap_uint<1> dir = dirs[i];
        if (inverse) {
            dir = !dir;
        }

        acc_t x_shift = x_acc >> i;
        acc_t y_shift = y_acc >> i;
        acc_t x_next;
        acc_t y_next;
        if (dir) {
            x_next = x_acc + y_shift;
            y_next = y_acc - x_shift;
        } else {
            x_next = x_acc - y_shift;
            y_next = y_acc + x_shift;
        }
        x_acc = x_next;
        y_acc = y_next;
    }

    pe_cordic_rot_result_t out;
    out.x = pe_cordic_gain_scale(x_acc);
    out.y = pe_cordic_gain_scale(y_acc);
    return out;
}

static pe_fix_complex_t pe_apply_phase_dirs(
    pe_fix_complex_t v,
    ap_uint<16> dirs,
    ap_uint<1> pre_negate,
    ap_uint<1> inverse
) {
#pragma HLS INLINE
    pe_cordic_rot_result_t rot = pe_cordic_apply_dirs<CORDIC_ITER>(v.re, v.im, dirs, pre_negate, inverse);
    pe_fix_complex_t out;
    out.re = rot.x;
    out.im = rot.y;
    return out;
}

static pe_fix_complex_t pe_cmul(pe_fix_complex_t a, pe_fix_complex_t b) {
#pragma HLS INLINE
    acc_t re = (acc_t)a.re * (acc_t)b.re - (acc_t)a.im * (acc_t)b.im;
    acc_t im = (acc_t)a.re * (acc_t)b.im + (acc_t)a.im * (acc_t)b.re;
    pe_fix_complex_t y;
    y.re = (fix18_t)re;
    y.im = (fix18_t)im;
    return y;
}

static pe_fix_complex_t pe_cscale(pe_fix_complex_t a, fix18_t s) {
#pragma HLS INLINE
    pe_fix_complex_t y;
    y.re = (fix18_t)((acc_t)a.re * (acc_t)s);
    y.im = (fix18_t)((acc_t)a.im * (acc_t)s);
    return y;
}

static fix18_t pe_sqrt_lambda(fix18_t lambda) {
#pragma HLS INLINE
    if (lambda <= (fix18_t)0.0f) {
        return (fix18_t)0.0f;
    }
    if (lambda >= (fix18_t)1.0f) {
        return (fix18_t)1.0f;
    }
    acc_t e = (acc_t)1.0f - (acc_t)lambda;
    acc_t e2 = e * e;
    acc_t e3 = e2 * e;
    acc_t y = (acc_t)1.0f - e * (acc_t)0.5f - e2 * (acc_t)0.125f - e3 * (acc_t)0.0625f;
    return (fix18_t)y;
}

static fix18_t pe_abs(fix18_t x) {
#pragma HLS INLINE
    return (x < (fix18_t)0.0f) ? (fix18_t)(-(acc_t)x) : x;
}

static pe_fix_complex_t pe_clip_weight(pe_fix_complex_t a) {
#pragma HLS INLINE
    if (pe_abs(a.re) > PE_W_CLIP) {
        a.re = (a.re > (fix18_t)0.0f) ? PE_W_CLIP : (fix18_t)(-(acc_t)PE_W_CLIP);
    }
    if (pe_abs(a.im) > PE_W_CLIP) {
        a.im = (a.im > (fix18_t)0.0f) ? PE_W_CLIP : (fix18_t)(-(acc_t)PE_W_CLIP);
    }
    return a;
}

static pe_fix_complex_t pe_div_real_diag(pe_fix_complex_t a, fix18_t diag) {
#pragma HLS INLINE
    acc_t den = (acc_t)diag;
    acc_t min_den = (acc_t)PE_R_MIN;
    if (den < min_den && den > -min_den) {
        if (den < (acc_t)0.0f) {
            den = (acc_t)(-min_den);
        } else {
            den = min_den;
        }
    }
    acc_t inv_den = (acc_t)1.0f / den;
    pe_fix_complex_t y;
    y.re = (fix18_t)((acc_t)a.re * inv_den);
    y.im = (fix18_t)((acc_t)a.im * inv_den);
    return y;
}

static void pe_backsub_from_state(
    fix18_t R_state[QRD_RLS_N][QRD_RLS_N][2],
    fix18_t z_state[QRD_RLS_N][2],
    hls::stream<fix18_t> &w_real_out,
    hls::stream<fix18_t> &w_imag_out
) {
    pe_fix_complex_t w[QRD_RLS_N];
#pragma HLS ARRAY_PARTITION variable=w complete dim=1

init_w:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        w[i].re = (fix18_t)0.0f;
        w[i].im = (fix18_t)0.0f;
    }

back_sub_i:
    for (int i = QRD_RLS_N - 1; i >= 0; i--) {
#pragma HLS PIPELINE II=1
        acc_t sum_re = (acc_t)0.0f;
        acc_t sum_im = (acc_t)0.0f;
    back_sub_j:
        for (int j = QRD_RLS_N - 1; j > i; j--) {
#if PARALLEL_K > 1
#pragma HLS UNROLL factor=PARALLEL_K
#endif
            pe_fix_complex_t rij;
            rij.re = R_state[i][j][0];
            rij.im = R_state[i][j][1];
            pe_fix_complex_t wj;
            wj.re = w[j].re;
            wj.im = w[j].im;
            pe_fix_complex_t prod = pe_cmul(rij, wj);
            sum_re += (acc_t)prod.re;
            sum_im += (acc_t)prod.im;
        }

        pe_fix_complex_t rhs;
        rhs.re = (fix18_t)((acc_t)z_state[i][0] - sum_re);
        rhs.im = (fix18_t)((acc_t)z_state[i][1] - sum_im);
        w[i] = pe_clip_weight(pe_div_real_diag(rhs, R_state[i][i][0]));
    }

write_w:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        w_real_out.write(w[i].re);
        w_imag_out.write(w[i].im);
    }
}

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
) {
#pragma HLS BIND_STORAGE variable=R_state type=RAM_2P impl=BRAM
#pragma HLS BIND_STORAGE variable=z_state type=RAM_2P impl=BRAM

    if (reset) {
    reset_i:
        for (int i = 0; i < QRD_RLS_N; i++) {
            reset_j:
            for (int j = 0; j < QRD_RLS_N; j++) {
                R_state[i][j][0] = (fix18_t)0.0f;
                R_state[i][j][1] = (fix18_t)0.0f;
            }
            R_state[i][i][0] = PE_R_INIT;
            z_state[i][0] = (fix18_t)0.0f;
            z_state[i][1] = (fix18_t)0.0f;
        }
    }

    pe_fix_complex_t x[QRD_RLS_N];
    pe_fix_complex_t d;
#pragma HLS ARRAY_PARTITION variable=x complete dim=1

read_x:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        x[i].re = x_real_in.read();
        x[i].im = x_imag_in.read();
    }
    d.re = d_real_in.read();
    d.im = d_imag_in.read();

    ap_uint<1> overflow_local = 0;
    fix18_t sqrt_lambda = pe_sqrt_lambda(lambda);

qrd_row_loop:
    for (int i = 0; i < QRD_RLS_N; i++) {
#if PE_ROW_PIPELINE
#pragma HLS PIPELINE II=1
#endif
        pe_cordic_dir_result_t xi_polar = pe_cordic_vectoring_dirs<CORDIC_ITER>(x[i].re, x[i].im);
        fix18_t mag = xi_polar.r;
        pe_cordic_dir_result_t givens = pe_cordic_vectoring_dirs<CORDIC_ITER>((fix18_t)((acc_t)R_state[i][i][0] * (acc_t)sqrt_lambda), mag);
        fix18_t r_new = givens.r;
        if (r_new < PE_R_MIN) {
            r_new = PE_R_MIN;
        }

        R_state[i][i][0] = r_new;
        R_state[i][i][1] = (fix18_t)0.0f;
        if (r_new > (fix18_t)7.0f) {
            overflow_local = 1;
        }

    update_r:
        for (int j = i + 1; j < QRD_RLS_N; j++) {
#if PARALLEL_K > 1
#pragma HLS UNROLL factor=PARALLEL_K
#endif
            pe_fix_complex_t r_ij;
            r_ij.re = R_state[i][j][0];
            r_ij.im = R_state[i][j][1];
            pe_fix_complex_t r_scaled = pe_cscale(r_ij, sqrt_lambda);

            // DIRECTION_CONTROL_CORDIC:
            // BC emits micro-rotation directions. IC applies them directly to
            // the complex pair instead of reconstructing c/s and doing complex
            // multiply-add rotation.
            pe_fix_complex_t x_phase = pe_apply_phase_dirs(x[j], xi_polar.dirs, xi_polar.pre_negate, 0);
            pe_cordic_rot_result_t rot_re = pe_cordic_apply_dirs<CORDIC_ITER>(
                r_scaled.re,
                x_phase.re,
                givens.dirs,
                givens.pre_negate,
                0
            );
            pe_cordic_rot_result_t rot_im = pe_cordic_apply_dirs<CORDIC_ITER>(
                r_scaled.im,
                x_phase.im,
                givens.dirs,
                givens.pre_negate,
                0
            );

            pe_fix_complex_t r_next;
            r_next.re = rot_re.x;
            r_next.im = rot_im.x;
            pe_fix_complex_t x_phase_next;
            x_phase_next.re = rot_re.y;
            x_phase_next.im = rot_im.y;
            pe_fix_complex_t x_next = pe_apply_phase_dirs(x_phase_next, xi_polar.dirs, xi_polar.pre_negate, 1);

            R_state[i][j][0] = r_next.re;
            R_state[i][j][1] = r_next.im;
            x[j] = x_next;
        }

        pe_fix_complex_t z_i;
        z_i.re = z_state[i][0];
        z_i.im = z_state[i][1];
        pe_fix_complex_t z_scaled = pe_cscale(z_i, sqrt_lambda);
        pe_fix_complex_t d_phase = pe_apply_phase_dirs(d, xi_polar.dirs, xi_polar.pre_negate, 0);
        pe_cordic_rot_result_t z_rot_re = pe_cordic_apply_dirs<CORDIC_ITER>(
            z_scaled.re,
            d_phase.re,
            givens.dirs,
            givens.pre_negate,
            0
        );
        pe_cordic_rot_result_t z_rot_im = pe_cordic_apply_dirs<CORDIC_ITER>(
            z_scaled.im,
            d_phase.im,
            givens.dirs,
            givens.pre_negate,
            0
        );

        pe_fix_complex_t z_next;
        z_next.re = z_rot_re.x;
        z_next.im = z_rot_im.x;
        pe_fix_complex_t d_phase_next;
        d_phase_next.re = z_rot_re.y;
        d_phase_next.im = z_rot_im.y;
        pe_fix_complex_t d_next = pe_apply_phase_dirs(d_phase_next, xi_polar.dirs, xi_polar.pre_negate, 1);
        z_state[i][0] = z_next.re;
        z_state[i][1] = z_next.im;
        d = d_next;
    }

write_diag:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        r_diag_out.write(R_state[i][i][0]);
    }

    overflow_flag = overflow_local;
}

void qrd_rls_cordic_pe_8x8_backsub_state_stream(
    fix18_t R_state[QRD_RLS_N][QRD_RLS_N][2],
    fix18_t z_state[QRD_RLS_N][2],
    hls::stream<fix18_t> &w_real_out,
    hls::stream<fix18_t> &w_imag_out
) {
    pe_backsub_from_state(R_state, z_state, w_real_out, w_imag_out);
}

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
) {
    static fix18_t R_state[QRD_RLS_N][QRD_RLS_N][2];
    static fix18_t z_state[QRD_RLS_N][2];
#pragma HLS ARRAY_PARTITION variable=R_state complete dim=2
#pragma HLS ARRAY_PARTITION variable=R_state complete dim=3
#pragma HLS ARRAY_PARTITION variable=z_state complete dim=1
#pragma HLS ARRAY_PARTITION variable=z_state complete dim=2

    qrd_rls_cordic_pe_8x8_update_state_stream(
        x_real_in,
        x_imag_in,
        d_real_in,
        d_imag_in,
        R_state,
        z_state,
        r_diag_out,
        lambda,
        reset,
        overflow_flag
    );

    qrd_rls_cordic_pe_8x8_backsub_state_stream(
        R_state,
        z_state,
        w_real_out,
        w_imag_out
    );
}

void qrd_rls_cordic_pe_8x8_update_top(
    hls::stream<fix18_t> &x_real_in,
    hls::stream<fix18_t> &x_imag_in,
    hls::stream<fix18_t> &d_real_in,
    hls::stream<fix18_t> &d_imag_in,
    hls::stream<fix18_t> &r_diag_out,
    fix18_t lambda,
    ap_uint<1> reset,
    ap_uint<1> &overflow_flag
) {
#pragma HLS INTERFACE mode=axis port=x_real_in
#pragma HLS INTERFACE mode=axis port=x_imag_in
#pragma HLS INTERFACE mode=axis port=d_real_in
#pragma HLS INTERFACE mode=axis port=d_imag_in
#pragma HLS INTERFACE mode=axis port=r_diag_out
#pragma HLS INTERFACE mode=s_axilite port=lambda bundle=control
#pragma HLS INTERFACE mode=s_axilite port=reset bundle=control
#pragma HLS INTERFACE mode=s_axilite port=overflow_flag bundle=control
#pragma HLS INTERFACE mode=s_axilite port=return bundle=control

    static fix18_t R_state[QRD_RLS_N][QRD_RLS_N][2];
    static fix18_t z_state[QRD_RLS_N][2];
#pragma HLS ARRAY_PARTITION variable=R_state complete dim=2
#pragma HLS ARRAY_PARTITION variable=R_state complete dim=3
#pragma HLS ARRAY_PARTITION variable=z_state complete dim=1
#pragma HLS ARRAY_PARTITION variable=z_state complete dim=2

    qrd_rls_cordic_pe_8x8_update_state_stream(
        x_real_in,
        x_imag_in,
        d_real_in,
        d_imag_in,
        R_state,
        z_state,
        r_diag_out,
        lambda,
        reset,
        overflow_flag
    );
}

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
) {
#pragma HLS INTERFACE mode=axis port=x_real_in
#pragma HLS INTERFACE mode=axis port=x_imag_in
#pragma HLS INTERFACE mode=axis port=d_real_in
#pragma HLS INTERFACE mode=axis port=d_imag_in
#pragma HLS INTERFACE mode=axis port=w_real_out
#pragma HLS INTERFACE mode=axis port=w_imag_out
#pragma HLS INTERFACE mode=axis port=r_diag_out
#pragma HLS INTERFACE mode=s_axilite port=lambda bundle=control
#pragma HLS INTERFACE mode=s_axilite port=reset bundle=control
#pragma HLS INTERFACE mode=s_axilite port=overflow_flag bundle=control
#pragma HLS INTERFACE mode=s_axilite port=return bundle=control

    qrd_rls_cordic_pe_8x8_core_stream(
        x_real_in,
        x_imag_in,
        d_real_in,
        d_imag_in,
        w_real_out,
        w_imag_out,
        r_diag_out,
        lambda,
        reset,
        overflow_flag
    );
}
