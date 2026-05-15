// snapshot_normalizer.cpp
// Purpose: Shared AXI4-Stream snapshot normalizer for 8-channel complex QRD-RLS frames.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#include "snapshot_normalizer.h"

void snapshot_normalizer(
    hls::stream<axis_cpx64_t> &snapshot_in,
    hls::stream<axis_cpx64_t> &snapshot_out,
    ap_uint<1> enable_norm,
    float eps,
    float &scale_out,
    ap_uint<1> &frame_error
) {
#pragma HLS INTERFACE mode=axis port=snapshot_in
#pragma HLS INTERFACE mode=axis port=snapshot_out
#pragma HLS INTERFACE mode=s_axilite port=enable_norm bundle=control
#pragma HLS INTERFACE mode=s_axilite port=eps bundle=control
#pragma HLS INTERFACE mode=s_axilite port=scale_out bundle=control
#pragma HLS INTERFACE mode=s_axilite port=frame_error bundle=control
#pragma HLS INTERFACE mode=s_axilite port=return bundle=control

    float re_buf[QRD_FRAME_BEATS];
    float im_buf[QRD_FRAME_BEATS];
#pragma HLS ARRAY_PARTITION variable=re_buf complete dim=1
#pragma HLS ARRAY_PARTITION variable=im_buf complete dim=1

    ap_uint<1> err = 0;
    ap_uint<32> max_abs_bits = 0;

    read_frame:
    for (int i = 0; i < QRD_FRAME_BEATS; i++) {
#pragma HLS PIPELINE II=1
        axis_cpx64_t pkt = snapshot_in.read();
        ap_uint<32> re_bits = pkt.data.range(31, 0);
        ap_uint<32> im_bits = pkt.data.range(63, 32);
        ap_uint<32> abs_re_bits = re_bits;
        ap_uint<32> abs_im_bits = im_bits;
        abs_re_bits[31] = 0;
        abs_im_bits[31] = 0;
        re_buf[i] = qrd_bits_to_float(re_bits);
        im_buf[i] = qrd_bits_to_float(im_bits);

        ap_uint<32> local_max_bits = (abs_re_bits > abs_im_bits) ? abs_re_bits : abs_im_bits;
        if (local_max_bits > max_abs_bits) {
            max_abs_bits = local_max_bits;
        }

        if ((i < QRD_FRAME_BEATS - 1 && pkt.last) || (i == QRD_FRAME_BEATS - 1 && !pkt.last)) {
            err = 1;
        }
    }

    float max_abs = qrd_bits_to_float(max_abs_bits);
    float safe_eps = (eps > 0.0f) ? eps : 1.0e-6f;
    float target_peak = (QRD_NORMALIZER_TARGET_PEAK > 0.0f) ? QRD_NORMALIZER_TARGET_PEAK : 1.0f;
    float scale = (enable_norm && max_abs > safe_eps) ? (max_abs / target_peak) : 1.0f;
    float inv_scale = 1.0f / scale;

    write_frame:
    for (int i = 0; i < QRD_FRAME_BEATS; i++) {
#pragma HLS PIPELINE II=1
        float out_re = re_buf[i] * inv_scale;
        float out_im = im_buf[i] * inv_scale;
        ap_uint<1> last = (i == QRD_FRAME_BEATS - 1) ? ap_uint<1>(1) : ap_uint<1>(0);
        ap_uint<1> user = (i == QRD_RLS_N) ? ap_uint<1>(1) : ap_uint<1>(0);
        snapshot_out.write(qrd_pack_cpx(out_re, out_im, last, user));
    }

    scale_out = scale;
    frame_error = err;
}
