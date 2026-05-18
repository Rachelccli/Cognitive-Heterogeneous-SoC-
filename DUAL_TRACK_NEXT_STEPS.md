# Dual-Track Next Steps

## Decision

The next primary step is HLS IP export plus a minimal Vivado Block Design, not immediate GRU integration.

Status update:

- board bring-up for `FP32`, `FIXED_PE`, and `HYBRID_LP` is already established;
- the current highest-value immediate work is now:
  1. freeze the validated `HYBRID_LP` scheduler point,
  2. extend vital-sign evidence,
  3. extract AI-VFF dataset `v0`.

For the exact freeze conclusion and milestone ordering, see:

- `CURRENT_FREEZE_AND_NEXT_STEPS.md`

## Why IP Export Comes First

1. The three HLS engines are already stable enough to freeze:
   - `FP32`: high-accuracy QRD-RLS reference and numerical safety path.
   - `FIXED_PE`: paper-aligned fixed-point CORDIC QRD-RLS path.
   - `HYBRID_LP`: one-active-path precision scheduler plus rule-based VFF.
2. GRU would add a new controller before the hardware substrate is proven at system level.
3. Board-level validation exposes risks that HLS C-sim cannot prove:
   - AXIS packet framing and TLAST behavior.
   - AXI DMA transfer order and buffer layout.
   - AXI-Lite control register programming.
   - Real clock, reset, backpressure, and thermal/power behavior.
4. AI-VFF should be trained from reliable traces. The best labels come after HLS traces are aligned with BD/board behavior.

## Track A: Hardware Bring-Up

Goal: get the current frozen baseline onto the board with the smallest useful system.

Detailed implementation guide: `VIVADO_BD_BRINGUP_GUIDE.md`.

1. Freeze and push the clean baseline.
2. Export HLS IPs:
   - `qrd_rls_fp32_8x8_axis`
   - `qrd_fixed_pe_monitor_top`
   - `dual_precision_qrd_top`
3. Build a minimal Vivado BD:
   - Zynq MPSoC PS
   - AXI DMA
   - AXI interconnect / SmartConnect
   - one HLS IP at a time
4. Bring-up order:
   - FP32 single-IP loopback style validation
   - FIXED_PE single-IP validation
   - HYBRID_LP validation with AXI-Lite mode/lambda/threshold control
5. Measure:
   - output correctness against HLS reference vectors
   - frame error / overflow / selected_float / lambda_transit registers
   - basic throughput per snapshot
   - initial Vivado power estimate, then board power if available

## Track B: AI-VFF Dataset

Goal: prepare GRU without touching the current hardware baseline yet.

Detailed schema and workflow: `AI_VFF_DATASET_SCHEMA.md`.

1. Keep rule-based VFF as the frozen controller baseline.
2. Export features from existing HLS traces:
   - `cond_est`
   - `delta_norm`
   - `scale`
   - `overflow`
   - `switch_recommend`
   - `selected_float`
   - `migration_done`
   - `lambda_transit`
3. Add labels:
   - `STEADY`
   - `TRANSIENT`
   - `RECOVERING`
   - `HIGH_RISK`
4. Train GRU offline only after the feature/label format is stable.
5. Hardware insertion path:
   - offline GRU inference first
   - software-side scheduler emulation second
   - HLS GRU micro-IP only after board data confirms the value

## Track C: Vital-Sign Extension

Goal: strengthen the mmWave vital-sign evidence without changing architecture.

Add cases:

- longer vital drift
- stronger motion artifact
- low-SNR respiration
- repeated turn-over / recovery

Metrics:

- output SINR
- phase tracking
- respiration / heartbeat spectral peaks
- selected-FP32 ratio
- migration count
- lambda range

## Freeze Rule

Do not modify the QRD-RLS engines while board bring-up is in progress. Any scheduler changes should be tested in HLS trace matrices first and only merged after the baseline IP path is reproducible.
