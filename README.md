# Runtime-Adaptive Dual-Precision QRD-RLS Beamforming

This repository contains the 2026-05-14 frozen HLS mainline for a runtime-adaptive dual-precision QRD-RLS beamforming accelerator targeting Xilinx Kria KV260 / Vitis HLS.

## Main Blocks

- `HLS/FP32`: FP32 QRD-RLS reference and numerical safety path.
- `HLS/FIXED_PE`: paper-aligned fixed-point CORDIC QRD-RLS path with separated QR update and back-substitution.
- `HLS/HYBRID`: low-power hybrid top with one active precision path, state migration, precision scheduling, and rule-based VFF.
- `HLS/COMMON`: shared snapshot normalizer and diagonal condition/delta-R monitor.

## Current Modes

`dual_precision_qrd_top` exposes four runtime modes:

- `mode=0`: fixed-only.
- `mode=1`: FP32-only.
- `mode=2`: HYBRID_LP with rule-based VFF.
- `mode=3`: HYBRID_FIXED_LAMBDA, used as the precision-only ablation baseline.

## Resource Comparison Policy

Use two separate tables when reporting resources:

- Core-only: proves the fixed CORDIC QR update reduces DSP pressure versus FP32.
- System/top-level: includes wrappers, monitor, bridge, mux/control, and both physical precision paths.

HYBRID_LP is an activity/dynamic-power story, not a raw-area-between-FP32-and-FIXED story. The synthesized hybrid top still contains both precision paths.

## Repository Hygiene

Generated HLS projects, testbenches, logs, traces, figures, local papers, and plotting scripts are intentionally ignored. See `PROJECT_STRUCTURE.md` for the exact GitHub staging plan.
