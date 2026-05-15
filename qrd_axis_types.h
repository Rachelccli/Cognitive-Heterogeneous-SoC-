// qrd_axis_types.h
// Purpose: Shared AXI4-Stream packet types for engineering QRD-RLS interfaces.
// Target: Xilinx Kria KV260, Vitis HLS 2022.2, 154MHz
// Part of: AI-Driven Dual-Precision GSC-QRD-RLS Beamforming SoC

#ifndef QRD_AXIS_TYPES_H
#define QRD_AXIS_TYPES_H

#include <ap_axi_sdata.h>
#include <ap_int.h>

#ifndef QRD_RLS_N
#define QRD_RLS_N 8
#endif

#define QRD_FRAME_BEATS (QRD_RLS_N + 1)

typedef ap_axiu<64, 1, 1, 1> axis_cpx64_t;
typedef ap_axiu<32, 1, 1, 1> axis_float32_t;

static inline float qrd_bits_to_float(ap_uint<32> bits) {
#pragma HLS INLINE
    union {
        unsigned int i;
        float f;
    } conv;
    conv.i = bits.to_uint();
    return conv.f;
}

static inline ap_uint<32> qrd_float_to_bits(float val) {
#pragma HLS INLINE
    union {
        unsigned int i;
        float f;
    } conv;
    conv.f = val;
    return ap_uint<32>(conv.i);
}

static inline axis_cpx64_t qrd_pack_cpx(float re, float im, ap_uint<1> last, ap_uint<1> user) {
#pragma HLS INLINE
    axis_cpx64_t pkt;
    ap_uint<64> data;
    data.range(31, 0) = qrd_float_to_bits(re);
    data.range(63, 32) = qrd_float_to_bits(im);
    pkt.data = data;
    pkt.keep = -1;
    pkt.strb = -1;
    pkt.last = last;
    pkt.user = user;
    pkt.id = 0;
    pkt.dest = 0;
    return pkt;
}

static inline void qrd_unpack_cpx(axis_cpx64_t pkt, float &re, float &im) {
#pragma HLS INLINE
    re = qrd_bits_to_float(pkt.data.range(31, 0));
    im = qrd_bits_to_float(pkt.data.range(63, 32));
}

static inline axis_float32_t qrd_pack_float(float value, ap_uint<1> last, ap_uint<1> user) {
#pragma HLS INLINE
    axis_float32_t pkt;
    pkt.data = qrd_float_to_bits(value);
    pkt.keep = -1;
    pkt.strb = -1;
    pkt.last = last;
    pkt.user = user;
    pkt.id = 0;
    pkt.dest = 0;
    return pkt;
}

static inline float qrd_unpack_float(axis_float32_t pkt) {
#pragma HLS INLINE
    return qrd_bits_to_float(pkt.data);
}

#endif
