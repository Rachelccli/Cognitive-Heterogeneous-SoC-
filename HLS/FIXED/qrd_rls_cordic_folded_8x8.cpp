// qrd_rls_cordic_folded_8x8.cpp
// Purpose: Folded 8x8 fixed-point CORDIC QRD-RLS adaptive beamforming HLS kernel.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#include "qrd_rls_cordic_folded_8x8.h"

struct folded_cpx_t {
    fix18_t re;
    fix18_t im;
};

struct folded_bc_result_t {
    fix18_t r_new;
    fix18_t c;
    folded_cpx_t s;
    ap_uint<1> overflow;
};

typedef ap_fixed<22, 8, AP_TRN, AP_SAT_SYM> folded_recip_t;
typedef ap_fixed<26, 9, AP_TRN, AP_WRAP> folded_pe_acc_t;

static const fix18_t FOLDED_R_INIT = (fix18_t)0.010009765625f;
static const fix18_t FOLDED_R_MIN = (fix18_t)0.0001220703125f;
static const fix18_t FOLDED_W_CLIP = (fix18_t)4.0f;
static const folded_recip_t FOLDED_RECIP_SAT = (folded_recip_t)127.99609375f;
static const folded_recip_t FOLDED_RECIP_TINY = (folded_recip_t)0.0078125f;

static const folded_recip_t folded_recip_seed_lut[16] = {
    (folded_recip_t)1.9393939394f,
    (folded_recip_t)1.8285714286f,
    (folded_recip_t)1.7297297297f,
    (folded_recip_t)1.6410256410f,
    (folded_recip_t)1.5609756098f,
    (folded_recip_t)1.4883720930f,
    (folded_recip_t)1.4222222222f,
    (folded_recip_t)1.3617021277f,
    (folded_recip_t)1.3061224490f,
    (folded_recip_t)1.2549019608f,
    (folded_recip_t)1.2075471698f,
    (folded_recip_t)1.1636363636f,
    (folded_recip_t)1.1228070175f,
    (folded_recip_t)1.0847457627f,
    (folded_recip_t)1.0491803279f,
    (folded_recip_t)1.0158730159f
};

static acc_t folded_reciprocal(acc_t den);

static fix18_t folded_cordic_magnitude(fix18_t x, fix18_t y) {
#pragma HLS INLINE off
    const acc_t k16 = (acc_t)0.6072529350f;
    acc_t x_acc = (acc_t)x;
    acc_t y_acc = (acc_t)y;

    if (x_acc < (acc_t)0.0f) {
        x_acc = -x_acc;
        y_acc = -y_acc;
    }

    folded_mag_iter:
    for (int i = 0; i < 16; i++) {
#pragma HLS PIPELINE II=1
        acc_t x_shift = x_acc >> i;
        acc_t y_shift = y_acc >> i;
        if (y_acc >= (acc_t)0.0f) {
            x_acc = x_acc + y_shift;
            y_acc = y_acc - x_shift;
        } else {
            x_acc = x_acc - y_shift;
            y_acc = y_acc + x_shift;
        }
    }

    return (fix18_t)(x_acc * k16);
}

static fix18_t folded_sqrt_lambda(fix18_t lambda) {
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

static folded_cpx_t folded_cmul(folded_cpx_t a, folded_cpx_t b) {
#pragma HLS INLINE
    acc_t re = (acc_t)a.re * (acc_t)b.re - (acc_t)a.im * (acc_t)b.im;
    acc_t im = (acc_t)a.re * (acc_t)b.im + (acc_t)a.im * (acc_t)b.re;
    folded_cpx_t y;
    y.re = (fix18_t)re;
    y.im = (fix18_t)im;
    return y;
}

static folded_cpx_t folded_cadd(folded_cpx_t a, folded_cpx_t b) {
#pragma HLS INLINE
    folded_cpx_t y;
    y.re = (fix18_t)((acc_t)a.re + (acc_t)b.re);
    y.im = (fix18_t)((acc_t)a.im + (acc_t)b.im);
    return y;
}

static folded_cpx_t folded_csub(folded_cpx_t a, folded_cpx_t b) {
#pragma HLS INLINE
    folded_cpx_t y;
    y.re = (fix18_t)((acc_t)a.re - (acc_t)b.re);
    y.im = (fix18_t)((acc_t)a.im - (acc_t)b.im);
    return y;
}

static folded_cpx_t folded_cscale(folded_cpx_t a, fix18_t s) {
#pragma HLS INLINE
    folded_cpx_t y;
    y.re = (fix18_t)((acc_t)a.re * (acc_t)s);
    y.im = (fix18_t)((acc_t)a.im * (acc_t)s);
    return y;
}

static folded_pe_acc_t folded_pe_mul(fix18_t a, fix18_t b) {
#pragma HLS INLINE
    return (folded_pe_acc_t)((acc_t)a * (acc_t)b);
}

static folded_cpx_t folded_conj(folded_cpx_t a) {
#pragma HLS INLINE
    folded_cpx_t y;
    y.re = a.re;
    y.im = (fix18_t)(-(acc_t)a.im);
    return y;
}

static fix18_t folded_abs(fix18_t x) {
#pragma HLS INLINE
    return (x < (fix18_t)0.0f) ? (fix18_t)(-(acc_t)x) : x;
}

static folded_cpx_t folded_clip_weight(folded_cpx_t a) {
#pragma HLS INLINE
    if (folded_abs(a.re) > FOLDED_W_CLIP) {
        a.re = (a.re > (fix18_t)0.0f) ? FOLDED_W_CLIP : (fix18_t)(-(acc_t)FOLDED_W_CLIP);
    }
    if (folded_abs(a.im) > FOLDED_W_CLIP) {
        a.im = (a.im > (fix18_t)0.0f) ? FOLDED_W_CLIP : (fix18_t)(-(acc_t)FOLDED_W_CLIP);
    }
    return a;
}

static folded_bc_result_t folded_bc_pe(folded_cpx_t x_i, fix18_t r_ii, fix18_t sqrt_lambda) {
#pragma HLS INLINE off
#pragma HLS ALLOCATION operation instances=mul limit=2
    fix18_t mag = folded_cordic_magnitude(x_i.re, x_i.im);
    acc_t r_scaled = (acc_t)r_ii * (acc_t)sqrt_lambda;
    fix18_t r_new = folded_cordic_magnitude((fix18_t)r_scaled, mag);
    if (r_new < FOLDED_R_MIN) {
        r_new = FOLDED_R_MIN;
    }

    folded_bc_result_t out;
    out.r_new = r_new;
    acc_t inv_r = folded_reciprocal((acc_t)r_new);
    out.c = (fix18_t)(r_scaled * inv_r);
    out.s.re = (fix18_t)((acc_t)x_i.re * inv_r);
    out.s.im = (fix18_t)((acc_t)(-x_i.im) * inv_r);
    out.overflow = (r_new > (fix18_t)7.0f) ? ap_uint<1>(1) : ap_uint<1>(0);
    return out;
}

static void folded_ic_pe(
    fix18_t r_in_re,
    fix18_t r_in_im,
    folded_cpx_t x_in,
    fix18_t c,
    folded_cpx_t s,
    fix18_t sqrt_lambda,
    fix18_t &r_out_re,
    fix18_t &r_out_im,
    folded_cpx_t &x_out
) {
#pragma HLS INLINE off
#pragma HLS ALLOCATION operation instances=mul limit=2
    // FUSED_COMPLEX_GIVENS_PE: keep the full IC rotation inside one
    // narrow accumulator domain to avoid repeated AP_SAT conversion logic.
    fix18_t r_scaled_re = (fix18_t)folded_pe_mul(r_in_re, sqrt_lambda);
    fix18_t r_scaled_im = (fix18_t)folded_pe_mul(r_in_im, sqrt_lambda);

    folded_pe_acc_t sx_re = folded_pe_mul(s.re, x_in.re) - folded_pe_mul(s.im, x_in.im);
    folded_pe_acc_t sx_im = folded_pe_mul(s.re, x_in.im) + folded_pe_mul(s.im, x_in.re);
    folded_pe_acc_t cr_re = folded_pe_mul(c, r_scaled_re);
    folded_pe_acc_t cr_im = folded_pe_mul(c, r_scaled_im);

    folded_pe_acc_t conj_sr_re = folded_pe_mul(s.re, r_scaled_re) + folded_pe_mul(s.im, r_scaled_im);
    folded_pe_acc_t conj_sr_im = folded_pe_mul(s.re, r_scaled_im) - folded_pe_mul(s.im, r_scaled_re);
    folded_pe_acc_t cx_re = folded_pe_mul(c, x_in.re);
    folded_pe_acc_t cx_im = folded_pe_mul(c, x_in.im);

    r_out_re = (fix18_t)(cr_re + sx_re);
    r_out_im = (fix18_t)(cr_im + sx_im);
    x_out.re = (fix18_t)(cx_re - conj_sr_re);
    x_out.im = (fix18_t)(cx_im - conj_sr_im);
}

static void folded_zic_pe(
    folded_cpx_t z_in,
    folded_cpx_t d_in,
    fix18_t c,
    folded_cpx_t s,
    fix18_t sqrt_lambda,
    folded_cpx_t &z_out,
    folded_cpx_t &d_out
) {
#pragma HLS INLINE off
#pragma HLS ALLOCATION operation instances=mul limit=2
    // FUSED_COMPLEX_GIVENS_PE: same datapath shape as IC, applied to z/d.
    fix18_t z_scaled_re = (fix18_t)folded_pe_mul(z_in.re, sqrt_lambda);
    fix18_t z_scaled_im = (fix18_t)folded_pe_mul(z_in.im, sqrt_lambda);

    folded_pe_acc_t sd_re = folded_pe_mul(s.re, d_in.re) - folded_pe_mul(s.im, d_in.im);
    folded_pe_acc_t sd_im = folded_pe_mul(s.re, d_in.im) + folded_pe_mul(s.im, d_in.re);
    folded_pe_acc_t cz_re = folded_pe_mul(c, z_scaled_re);
    folded_pe_acc_t cz_im = folded_pe_mul(c, z_scaled_im);

    folded_pe_acc_t conj_sz_re = folded_pe_mul(s.re, z_scaled_re) + folded_pe_mul(s.im, z_scaled_im);
    folded_pe_acc_t conj_sz_im = folded_pe_mul(s.re, z_scaled_im) - folded_pe_mul(s.im, z_scaled_re);
    folded_pe_acc_t cd_re = folded_pe_mul(c, d_in.re);
    folded_pe_acc_t cd_im = folded_pe_mul(c, d_in.im);

    z_out.re = (fix18_t)(cz_re + sd_re);
    z_out.im = (fix18_t)(cz_im + sd_im);
    d_out.re = (fix18_t)(cd_re - conj_sz_re);
    d_out.im = (fix18_t)(cd_im - conj_sz_im);
}

static acc_t folded_reciprocal(acc_t den) {
#pragma HLS INLINE off
    // LUT_SEED_NEWTON_RECIP: normalize den to [0.5, 1), seed 1/x from a
    // 16-entry midpoint LUT, then apply one Newton-Raphson refinement.
    folded_recip_t x = (folded_recip_t)den;
    ap_uint<1> neg = 0;

    if (x < (folded_recip_t)0.0f) {
        x = -x;
        neg = 1;
    }
    if (x < (folded_recip_t)FOLDED_R_MIN) {
        x = (folded_recip_t)FOLDED_R_MIN;
    }

    if (x <= FOLDED_RECIP_TINY) {
        folded_recip_t saturated = FOLDED_RECIP_SAT;
        if (neg) {
            saturated = -saturated;
        }
        return (acc_t)saturated;
    }

    int scale_exp = 0;

    recip_norm_up:
    for (int i = 0; i < 6; i++) {
#pragma HLS PIPELINE II=1
        if (x < (folded_recip_t)0.5f) {
            x = x << 1;
            scale_exp++;
        }
    }

    recip_norm_down:
    for (int i = 0; i < 6; i++) {
#pragma HLS PIPELINE II=1
        if (x >= (folded_recip_t)1.0f) {
            x = x >> 1;
            scale_exp--;
        }
    }

    folded_recip_t lut_pos = (x - (folded_recip_t)0.5f) * (folded_recip_t)32.0f;
    ap_uint<5> raw_idx = (ap_uint<5>)lut_pos;
    ap_uint<4> idx = (raw_idx > 15) ? ap_uint<4>(15) : (ap_uint<4>)raw_idx;
    folded_recip_t y = folded_recip_seed_lut[idx];

    recip_newton:
    for (int i = 0; i < 1; i++) {
#pragma HLS PIPELINE II=1
        folded_recip_t xy;
#pragma HLS BIND_OP variable=xy op=mul impl=dsp latency=2
        xy = (folded_recip_t)(x * y);
        folded_recip_t correction = (folded_recip_t)2.0f - xy;
        folded_recip_t y_next;
#pragma HLS BIND_OP variable=y_next op=mul impl=dsp latency=2
        y_next = (folded_recip_t)(y * correction);
        y = y_next;
    }

    recip_scale_up:
    for (int i = 0; i < 6; i++) {
#pragma HLS PIPELINE II=1
        if (scale_exp > 0) {
            y = y << 1;
            scale_exp--;
        }
    }

    recip_scale_down:
    for (int i = 0; i < 6; i++) {
#pragma HLS PIPELINE II=1
        if (scale_exp < 0) {
            y = y >> 1;
            scale_exp++;
        }
    }

    if (y > FOLDED_RECIP_SAT) {
        y = FOLDED_RECIP_SAT;
    }

    if (neg) {
        y = -y;
    }
    return (acc_t)y;
}

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
) {
#pragma HLS ALLOCATION function instances=folded_cordic_magnitude limit=1
#pragma HLS ALLOCATION function instances=folded_ic_pe limit=1
#pragma HLS ALLOCATION function instances=folded_zic_pe limit=1
#pragma HLS ALLOCATION function instances=folded_reciprocal limit=1
#pragma HLS ALLOCATION operation instances=mul limit=4

    static fix18_t R_re[QRD_RLS_N][QRD_RLS_N];
    static fix18_t R_im[QRD_RLS_N][QRD_RLS_N];
    static folded_cpx_t z[QRD_RLS_N];
#pragma HLS BIND_STORAGE variable=R_re type=RAM_2P impl=BRAM
#pragma HLS BIND_STORAGE variable=R_im type=RAM_2P impl=BRAM
#pragma HLS BIND_STORAGE variable=z type=RAM_2P impl=BRAM

    if (reset) {
        reset_i:
        for (int i = 0; i < QRD_RLS_N; i++) {
            reset_j:
            for (int j = 0; j < QRD_RLS_N; j++) {
                R_re[i][j] = (fix18_t)0.0f;
                R_im[i][j] = (fix18_t)0.0f;
            }
            R_re[i][i] = FOLDED_R_INIT;
            z[i].re = (fix18_t)0.0f;
            z[i].im = (fix18_t)0.0f;
        }
    }

    folded_cpx_t x[QRD_RLS_N];
    folded_cpx_t w[QRD_RLS_N];
    folded_cpx_t d;

    read_x:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        x[i].re = x_real_in.read();
        x[i].im = x_imag_in.read();
    }
    d.re = d_real_in.read();
    d.im = d_imag_in.read();

    ap_uint<1> overflow_local = 0;
    fix18_t sqrt_lambda = folded_sqrt_lambda(lambda);

    qrd_row_loop:
    for (int i = 0; i < QRD_RLS_N; i++) {
        folded_cpx_t xi;
        xi.re = x[i].re;
        xi.im = x[i].im;
        folded_bc_result_t bc = folded_bc_pe(xi, R_re[i][i], sqrt_lambda);

        R_re[i][i] = bc.r_new;
        R_im[i][i] = (fix18_t)0.0f;
        overflow_local = overflow_local | bc.overflow;

        update_r:
        for (int j = i + 1; j < QRD_RLS_N; j++) {
            folded_cpx_t x_next;
            fix18_t r_next_re;
            fix18_t r_next_im;
            folded_ic_pe(
                R_re[i][j],
                R_im[i][j],
                x[j],
                bc.c,
                bc.s,
                sqrt_lambda,
                r_next_re,
                r_next_im,
                x_next
            );
            R_re[i][j] = r_next_re;
            R_im[i][j] = r_next_im;
            x[j] = x_next;
        }

        folded_cpx_t z_next;
        folded_cpx_t d_next;
        folded_zic_pe(z[i], d, bc.c, bc.s, sqrt_lambda, z_next, d_next);
        z[i] = z_next;
        d = d_next;
    }

    back_sub_i:
    for (int i = QRD_RLS_N - 1; i >= 0; i--) {
        acc_t sum_re = (acc_t)0.0f;
        acc_t sum_im = (acc_t)0.0f;
        back_sub_j:
        for (int j = QRD_RLS_N - 1; j > i; j--) {
            folded_cpx_t rij;
            rij.re = R_re[i][j];
            rij.im = R_im[i][j];
            folded_cpx_t prod = folded_cmul(rij, w[j]);
            sum_re += (acc_t)prod.re;
            sum_im += (acc_t)prod.im;
        }
        acc_t den = (acc_t)R_re[i][i];
        acc_t min_den = (acc_t)FOLDED_R_MIN;
        if (den < min_den && den > -min_den) {
            den = min_den;
        }
        acc_t inv_den = folded_reciprocal(den);
        w[i].re = (fix18_t)(((acc_t)z[i].re - sum_re) * inv_den);
        w[i].im = (fix18_t)(((acc_t)z[i].im - sum_im) * inv_den);
        w[i] = folded_clip_weight(w[i]);
    }

    write_w:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        w_real_out.write(w[i].re);
        w_imag_out.write(w[i].im);
    }

    write_diag:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        r_diag_out.write(R_re[i][i]);
    }

    overflow_flag = overflow_local;
}

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

    qrd_rls_cordic_folded_8x8_core_stream(
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
) {
#pragma HLS ALLOCATION function instances=folded_cordic_magnitude limit=1
#pragma HLS ALLOCATION function instances=folded_ic_pe limit=1
#pragma HLS ALLOCATION function instances=folded_zic_pe limit=1
#pragma HLS ALLOCATION function instances=folded_reciprocal limit=1
#pragma HLS ALLOCATION operation instances=mul limit=4

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
            R_state[i][i][0] = FOLDED_R_INIT;
            z_state[i][0] = (fix18_t)0.0f;
            z_state[i][1] = (fix18_t)0.0f;
        }
    }

    folded_cpx_t x[QRD_RLS_N];
    folded_cpx_t w[QRD_RLS_N];
    folded_cpx_t d;

    read_x:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        x[i].re = x_real_in.read();
        x[i].im = x_imag_in.read();
    }
    d.re = d_real_in.read();
    d.im = d_imag_in.read();

    ap_uint<1> overflow_local = 0;
    fix18_t sqrt_lambda = folded_sqrt_lambda(lambda);

    qrd_row_loop:
    for (int i = 0; i < QRD_RLS_N; i++) {
        folded_cpx_t xi;
        xi.re = x[i].re;
        xi.im = x[i].im;
        folded_bc_result_t bc = folded_bc_pe(xi, R_state[i][i][0], sqrt_lambda);

        R_state[i][i][0] = bc.r_new;
        R_state[i][i][1] = (fix18_t)0.0f;
        overflow_local = overflow_local | bc.overflow;

        update_r:
        for (int j = i + 1; j < QRD_RLS_N; j++) {
            folded_cpx_t x_next;
            fix18_t r_next_re;
            fix18_t r_next_im;
            folded_ic_pe(
                R_state[i][j][0],
                R_state[i][j][1],
                x[j],
                bc.c,
                bc.s,
                sqrt_lambda,
                r_next_re,
                r_next_im,
                x_next
            );
            R_state[i][j][0] = r_next_re;
            R_state[i][j][1] = r_next_im;
            x[j] = x_next;
        }

        folded_cpx_t zi;
        zi.re = z_state[i][0];
        zi.im = z_state[i][1];
        folded_cpx_t z_next;
        folded_cpx_t d_next;
        folded_zic_pe(zi, d, bc.c, bc.s, sqrt_lambda, z_next, d_next);
        z_state[i][0] = z_next.re;
        z_state[i][1] = z_next.im;
        d = d_next;
    }

    back_sub_i:
    for (int i = QRD_RLS_N - 1; i >= 0; i--) {
        acc_t sum_re = (acc_t)0.0f;
        acc_t sum_im = (acc_t)0.0f;
        back_sub_j:
        for (int j = QRD_RLS_N - 1; j > i; j--) {
            folded_cpx_t rij;
            rij.re = R_state[i][j][0];
            rij.im = R_state[i][j][1];
            folded_cpx_t prod = folded_cmul(rij, w[j]);
            sum_re += (acc_t)prod.re;
            sum_im += (acc_t)prod.im;
        }
        acc_t den = (acc_t)R_state[i][i][0];
        acc_t min_den = (acc_t)FOLDED_R_MIN;
        if (den < min_den && den > -min_den) {
            den = min_den;
        }
        acc_t inv_den = folded_reciprocal(den);
        w[i].re = (fix18_t)(((acc_t)z_state[i][0] - sum_re) * inv_den);
        w[i].im = (fix18_t)(((acc_t)z_state[i][1] - sum_im) * inv_den);
        w[i] = folded_clip_weight(w[i]);
    }

    write_w:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        w_real_out.write(w[i].re);
        w_imag_out.write(w[i].im);
    }

    write_diag:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        r_diag_out.write(R_state[i][i][0]);
    }

    overflow_flag = overflow_local;
}
