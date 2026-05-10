// qrd_rls_fp32_8x8.cpp
// Purpose: 8x8 FP32 feedforward QRD-RLS adaptive beamforming HLS kernel.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#include "qrd_rls_fp32_8x8.h"
#include "hls_math.h"

struct fp32_complex_t {
    float re;
    float im;
};

static const float FP32_R_INIT = 1.0e-2f;
static const float FP32_R_MIN = 1.0e-4f;
static const float FP32_W_CLIP = 4.0f;

static fp32_complex_t c_add(fp32_complex_t a, fp32_complex_t b) {
#pragma HLS INLINE
    fp32_complex_t y;
    y.re = a.re + b.re;
    y.im = a.im + b.im;
    return y;
}

static fp32_complex_t c_sub(fp32_complex_t a, fp32_complex_t b) {
#pragma HLS INLINE
    fp32_complex_t y;
    y.re = a.re - b.re;
    y.im = a.im - b.im;
    return y;
}

static fp32_complex_t c_mul(fp32_complex_t a, fp32_complex_t b) {
#pragma HLS INLINE
    fp32_complex_t y;
    y.re = a.re * b.re - a.im * b.im;
    y.im = a.re * b.im + a.im * b.re;
    return y;
}

static fp32_complex_t c_scale(fp32_complex_t a, float s) {
#pragma HLS INLINE
    fp32_complex_t y;
    y.re = a.re * s;
    y.im = a.im * s;
    return y;
}

static fp32_complex_t c_conj(fp32_complex_t a) {
#pragma HLS INLINE
    fp32_complex_t y;
    y.re = a.re;
    y.im = -a.im;
    return y;
}

static fp32_complex_t c_real_diag_div(fp32_complex_t a, float diag) {
#pragma HLS INLINE
    float den = diag;
    if (den < FP32_R_MIN && den > -FP32_R_MIN) {
        den = (den < 0.0f) ? -FP32_R_MIN : FP32_R_MIN;
    }
    float inv_den = 1.0f / den;
    fp32_complex_t y;
    y.re = a.re * inv_den;
    y.im = a.im * inv_den;
    return y;
}

static fp32_complex_t c_clip_weight(fp32_complex_t a) {
#pragma HLS INLINE
    float p = a.re * a.re + a.im * a.im;
    float lim = FP32_W_CLIP * FP32_W_CLIP;
    if (p > lim) {
        float scale = FP32_W_CLIP / hls::sqrt(p);
        a.re *= scale;
        a.im *= scale;
    }
    return a;
}

void qrd_rls_fp32_8x8_core_stream(
    hls::stream<float> &x_real_in,
    hls::stream<float> &x_imag_in,
    hls::stream<float> &d_real_in,
    hls::stream<float> &d_imag_in,
    hls::stream<float> &w_real_out,
    hls::stream<float> &w_imag_out,
    hls::stream<float> &r_diag_out,
    float lambda,
    ap_uint<1> reset
) {

    static fp32_complex_t R[QRD_RLS_N][QRD_RLS_N];
    static fp32_complex_t z[QRD_RLS_N];
#pragma HLS ARRAY_PARTITION variable=R complete dim=2
#pragma HLS ARRAY_PARTITION variable=z complete dim=1

    if (reset) {
        reset_i:
        for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
            reset_j:
            for (int j = 0; j < QRD_RLS_N; j++) {
#pragma HLS UNROLL
                R[i][j].re = 0.0f;
                R[i][j].im = 0.0f;
            }
            R[i][i].re = FP32_R_INIT;
            z[i].re = 0.0f;
            z[i].im = 0.0f;
        }
    }

    fp32_complex_t x[QRD_RLS_N];
    fp32_complex_t w[QRD_RLS_N];
    fp32_complex_t d;
#pragma HLS ARRAY_PARTITION variable=x complete dim=1
#pragma HLS ARRAY_PARTITION variable=w complete dim=1

    read_x:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        x[i].re = x_real_in.read();
        x[i].im = x_imag_in.read();
    }
    d.re = d_real_in.read();
    d.im = d_imag_in.read();

    float lam = lambda;
    if (lam < 0.0f) {
        lam = 0.0f;
    }
    if (lam > 1.0f) {
        lam = 1.0f;
    }
    float sqrt_lambda = hls::sqrt(lam);

    qrd_loop:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        fp32_complex_t xi = x[i];
        float r_old = R[i][i].re * sqrt_lambda;
        float x_abs_sq = xi.re * xi.re + xi.im * xi.im;
        float r_new = hls::sqrt(r_old * r_old + x_abs_sq);
        if (r_new < FP32_R_MIN) {
            r_new = FP32_R_MIN;
        }
        float inv_r = (r_new > 1.0e-12f) ? (1.0f / r_new) : 0.0f;
        float c = r_old * inv_r;
        fp32_complex_t s;
        s.re = xi.re * inv_r;
        s.im = -xi.im * inv_r;

        R[i][i].re = r_new;
        R[i][i].im = 0.0f;

        update_r:
        for (int j = i + 1; j < QRD_RLS_N; j++) {
#pragma HLS UNROLL
            fp32_complex_t r_scaled = c_scale(R[i][j], sqrt_lambda);
            fp32_complex_t s_x = c_mul(s, x[j]);
            fp32_complex_t s_conj = c_conj(s);
            fp32_complex_t s_r = c_mul(s_conj, r_scaled);
            fp32_complex_t r_next = c_add(c_scale(r_scaled, c), s_x);
            fp32_complex_t x_next = c_sub(c_scale(x[j], c), s_r);
            R[i][j] = r_next;
            x[j] = x_next;
        }

        fp32_complex_t z_scaled = c_scale(z[i], sqrt_lambda);
        fp32_complex_t s_d = c_mul(s, d);
        fp32_complex_t s_conj = c_conj(s);
        fp32_complex_t s_z = c_mul(s_conj, z_scaled);
        z[i] = c_add(c_scale(z_scaled, c), s_d);
        d = c_sub(c_scale(d, c), s_z);
    }

    back_sub_i:
    for (int i = QRD_RLS_N - 1; i >= 0; i--) {
#pragma HLS PIPELINE II=1
        fp32_complex_t sum;
        sum.re = 0.0f;
        sum.im = 0.0f;
        back_sub_j:
        for (int j = QRD_RLS_N - 1; j > i; j--) {
#pragma HLS UNROLL
            sum = c_add(sum, c_mul(R[i][j], w[j]));
        }
        fp32_complex_t rhs = c_sub(z[i], sum);
        w[i] = c_clip_weight(c_real_diag_div(rhs, R[i][i].re));
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
        r_diag_out.write(R[i][i].re);
    }
}

void qrd_rls_fp32_8x8(
    hls::stream<float> &x_real_in,
    hls::stream<float> &x_imag_in,
    hls::stream<float> &d_real_in,
    hls::stream<float> &d_imag_in,
    hls::stream<float> &w_real_out,
    hls::stream<float> &w_imag_out,
    hls::stream<float> &r_diag_out,
    float lambda,
    ap_uint<1> reset
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
#pragma HLS INTERFACE mode=s_axilite port=return bundle=control

    qrd_rls_fp32_8x8_core_stream(
        x_real_in,
        x_imag_in,
        d_real_in,
        d_imag_in,
        w_real_out,
        w_imag_out,
        r_diag_out,
        lambda,
        reset
    );
}

void qrd_rls_fp32_8x8_core_state_stream(
    hls::stream<float> &x_real_in,
    hls::stream<float> &x_imag_in,
    hls::stream<float> &d_real_in,
    hls::stream<float> &d_imag_in,
    hls::stream<float> &w_real_out,
    hls::stream<float> &w_imag_out,
    hls::stream<float> &r_diag_out,
    float R_state[QRD_RLS_N][QRD_RLS_N][2],
    float z_state[QRD_RLS_N][2],
    float lambda,
    ap_uint<1> reset
) {
#pragma HLS ARRAY_PARTITION variable=R_state complete dim=2
#pragma HLS ARRAY_PARTITION variable=R_state complete dim=3
#pragma HLS ARRAY_PARTITION variable=z_state complete dim=1
#pragma HLS ARRAY_PARTITION variable=z_state complete dim=2

    if (reset) {
        reset_i:
        for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
            reset_j:
            for (int j = 0; j < QRD_RLS_N; j++) {
#pragma HLS UNROLL
                R_state[i][j][0] = 0.0f;
                R_state[i][j][1] = 0.0f;
            }
            R_state[i][i][0] = FP32_R_INIT;
            z_state[i][0] = 0.0f;
            z_state[i][1] = 0.0f;
        }
    }

    fp32_complex_t x[QRD_RLS_N];
    fp32_complex_t w[QRD_RLS_N];
    fp32_complex_t d;
#pragma HLS ARRAY_PARTITION variable=x complete dim=1
#pragma HLS ARRAY_PARTITION variable=w complete dim=1

    read_x:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        x[i].re = x_real_in.read();
        x[i].im = x_imag_in.read();
    }
    d.re = d_real_in.read();
    d.im = d_imag_in.read();

    float lam = lambda;
    if (lam < 0.0f) {
        lam = 0.0f;
    }
    if (lam > 1.0f) {
        lam = 1.0f;
    }
    float sqrt_lambda = hls::sqrt(lam);

    qrd_loop:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        fp32_complex_t xi = x[i];
        float r_old = R_state[i][i][0] * sqrt_lambda;
        float x_abs_sq = xi.re * xi.re + xi.im * xi.im;
        float r_new = hls::sqrt(r_old * r_old + x_abs_sq);
        if (r_new < FP32_R_MIN) {
            r_new = FP32_R_MIN;
        }
        float inv_r = (r_new > 1.0e-12f) ? (1.0f / r_new) : 0.0f;
        float c = r_old * inv_r;
        fp32_complex_t s;
        s.re = xi.re * inv_r;
        s.im = -xi.im * inv_r;

        R_state[i][i][0] = r_new;
        R_state[i][i][1] = 0.0f;

        update_r:
        for (int j = i + 1; j < QRD_RLS_N; j++) {
#pragma HLS UNROLL
            fp32_complex_t rij;
            rij.re = R_state[i][j][0];
            rij.im = R_state[i][j][1];
            fp32_complex_t r_scaled = c_scale(rij, sqrt_lambda);
            fp32_complex_t s_x = c_mul(s, x[j]);
            fp32_complex_t s_conj = c_conj(s);
            fp32_complex_t s_r = c_mul(s_conj, r_scaled);
            fp32_complex_t r_next = c_add(c_scale(r_scaled, c), s_x);
            fp32_complex_t x_next = c_sub(c_scale(x[j], c), s_r);
            R_state[i][j][0] = r_next.re;
            R_state[i][j][1] = r_next.im;
            x[j] = x_next;
        }

        fp32_complex_t zi;
        zi.re = z_state[i][0];
        zi.im = z_state[i][1];
        fp32_complex_t z_scaled = c_scale(zi, sqrt_lambda);
        fp32_complex_t s_d = c_mul(s, d);
        fp32_complex_t s_conj = c_conj(s);
        fp32_complex_t s_z = c_mul(s_conj, z_scaled);
        fp32_complex_t z_next = c_add(c_scale(z_scaled, c), s_d);
        z_state[i][0] = z_next.re;
        z_state[i][1] = z_next.im;
        d = c_sub(c_scale(d, c), s_z);
    }

    back_sub_i:
    for (int i = QRD_RLS_N - 1; i >= 0; i--) {
#pragma HLS PIPELINE II=1
        fp32_complex_t sum;
        sum.re = 0.0f;
        sum.im = 0.0f;
        back_sub_j:
        for (int j = QRD_RLS_N - 1; j > i; j--) {
#pragma HLS UNROLL
            fp32_complex_t rij;
            rij.re = R_state[i][j][0];
            rij.im = R_state[i][j][1];
            sum = c_add(sum, c_mul(rij, w[j]));
        }
        fp32_complex_t zi;
        zi.re = z_state[i][0];
        zi.im = z_state[i][1];
        fp32_complex_t rhs = c_sub(zi, sum);
        w[i] = c_clip_weight(c_real_diag_div(rhs, R_state[i][i][0]));
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
}
