// qrd_rls_fp32_8x8_axis.cpp
// Purpose: Engineering AXI4-Stream snapshot wrapper for the 8x8 FP32 QRD-RLS kernel.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#include "qrd_rls_fp32_8x8_axis.h"
#include "qrd_rls_fp32_8x8.h"

void qrd_rls_fp32_8x8_axis(
    hls::stream<axis_cpx64_t> &snapshot_in,
    hls::stream<axis_cpx64_t> &weights_out,
    hls::stream<axis_float32_t> &diag_out,
    float lambda,
    ap_uint<1> reset,
    ap_uint<1> &frame_error
) {
#pragma HLS INTERFACE mode=axis port=snapshot_in
#pragma HLS INTERFACE mode=axis port=weights_out
#pragma HLS INTERFACE mode=axis port=diag_out
#pragma HLS INTERFACE mode=s_axilite port=lambda bundle=control
#pragma HLS INTERFACE mode=s_axilite port=reset bundle=control
#pragma HLS INTERFACE mode=s_axilite port=frame_error bundle=control
#pragma HLS INTERFACE mode=s_axilite port=return bundle=control

    hls::stream<float> x_re_s;
    hls::stream<float> x_im_s;
    hls::stream<float> d_re_s;
    hls::stream<float> d_im_s;
    hls::stream<float> w_re_s;
    hls::stream<float> w_im_s;
    hls::stream<float> diag_s;
#pragma HLS STREAM variable=x_re_s depth=8
#pragma HLS STREAM variable=x_im_s depth=8
#pragma HLS STREAM variable=d_re_s depth=1
#pragma HLS STREAM variable=d_im_s depth=1
#pragma HLS STREAM variable=w_re_s depth=8
#pragma HLS STREAM variable=w_im_s depth=8
#pragma HLS STREAM variable=diag_s depth=8

    ap_uint<1> err = 0;

    read_snapshot:
    for (int i = 0; i < QRD_FRAME_BEATS; i++) {
#pragma HLS PIPELINE II=1
        axis_cpx64_t pkt = snapshot_in.read();
        float re;
        float im;
        qrd_unpack_cpx(pkt, re, im);
        if (i < QRD_RLS_N) {
            x_re_s.write(re);
            x_im_s.write(im);
        } else {
            d_re_s.write(re);
            d_im_s.write(im);
        }
        if ((i < QRD_FRAME_BEATS - 1 && pkt.last) || (i == QRD_FRAME_BEATS - 1 && !pkt.last)) {
            err = 1;
        }
    }

    qrd_rls_fp32_8x8_core_stream(
        x_re_s,
        x_im_s,
        d_re_s,
        d_im_s,
        w_re_s,
        w_im_s,
        diag_s,
        lambda,
        reset
    );

    write_weights:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        float w_re = w_re_s.read();
        float w_im = w_im_s.read();
        ap_uint<1> last = (i == QRD_RLS_N - 1) ? ap_uint<1>(1) : ap_uint<1>(0);
        weights_out.write(qrd_pack_cpx(w_re, w_im, last, 0));
    }

    write_diag:
    for (int i = 0; i < QRD_RLS_N; i++) {
#pragma HLS PIPELINE II=1
        float diag = diag_s.read();
        ap_uint<1> last = (i == QRD_RLS_N - 1) ? ap_uint<1>(1) : ap_uint<1>(0);
        diag_out.write(qrd_pack_float(diag, last, 0));
    }

    frame_error = err;
}
