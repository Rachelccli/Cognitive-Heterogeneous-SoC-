# Current Freeze and Next Steps

## Freeze Decision

The current recommended freeze point is the **2026-05-18 scheduler-validated baseline** built on top of the earlier 2026-05-14 mainline.

This freeze keeps the same three-engine architecture:

- `HLS/FP32`: high-accuracy QRD-RLS reference path
- `HLS/FIXED_PE`: fixed-point CORDIC QRD-RLS path
- `HLS/HYBRID`: `HYBRID_LP` runtime precision scheduler with rule-based VFF

The only new RTL-level refinement in this freeze is the **small threshold-floor rule update** inside:

- `HLS/HYBRID/dual_precision_qrd_top.cpp`

Specifically, the effective scheduler condition threshold is now floored toward the validated `460` setting instead of relying only on the older `380`-level runtime default. This was intentionally done **without changing interfaces or module structure**.

## Why This Freeze Is Good Enough

### 1. HLS mainline is stable

The three HLS lines are all functioning and already play distinct roles:

- `FP32` is the numerical safety / upper-bound path.
- `FIXED_PE` is the low-DSP steady-state path.
- `HYBRID_LP` is the paper’s adaptive operating point: default fixed behavior with bounded FP32 activation and scheduled lambda behavior.

### 2. Radar line is already strong

The radar side now covers:

- steady scene
- transient jump
- multi-jammer
- near-angle
- weak target
- high-JNR stress
- burst stress
- off-boresight target cases
- near-angle separation sweep stress

The important off-boresight / separation additions live in:

- `tb_hls_hybrid_stress_matrix.cpp`

### 3. Board-level minimal-system evidence exists

The project is no longer “HLS only.”

Working board-side paths already exist for:

- `FP32` single-IP smoke test
- `FIXED_PE` single-IP smoke test
- `HYBRID_LP` multi-frame trace test

The current hybrid board app is:

- `hls_bd_hybrid/app_sources/hybrid_lp_trace_test.c`

### 4. The latest board A/B gate passed

The most important recent gate was the **three-scenario, dual-threshold board trace test**:

- scenarios:
  - `steady_const_64`
  - `mild_drift_64`
  - `multi_hard_sep_4deg_64`
- thresholds:
  - `th380`
  - `th460`

Observed board-side result:

- `steady_const_64`: unchanged and safe
  - `sel_frames 0 -> 0`
  - `act_frames 0 -> 0`
  - `mig_frames 0 -> 0`
- `mild_drift_64`: modest improvement
  - `sel_frames 28 -> 26`
  - `act_frames 29 -> 27`
  - `first_sel 36 -> 38`
- `multi_hard_sep_4deg_64`: strongest ROI result
  - `sel_frames 27 -> 24`
  - `act_frames 28 -> 25`
  - `first_sel 37 -> 40`
  - `switch_frames 23 -> 15`

Interpretation:

- `th460` lowers float duty on the target hard case
- steady behavior is preserved
- migration count does not increase
- no overflow or frame-error regression appears

This is enough to justify **freezing the scheduler branch** instead of continuing to hand-tune RTL rules.

## What Exactly Should Be Frozen

### Core code freeze

Freeze these implementation paths as the current mainline:

- `HLS/COMMON`
- `HLS/FP32`
- `HLS/FIXED_PE`
- `HLS/HYBRID/dual_precision_qrd_top.cpp`
- `HLS/HYBRID/dual_precision_qrd_top.h`
- `HLS/HYBRID/state_bridge.cpp`
- `HLS/HYBRID/state_bridge.h`

### Board-side freeze utilities

Keep these as reproducibility assets for bring-up and regression:

- `hls_bd_qrd/`
- `hls_bd_fixed/`
- `hls_bd_hybrid/`
- `scripts/rebuild_hybrid_ip_from_hls.tcl`
- `scripts/rebuild_hybrid_bd_xsa.tcl`
- `VIVADO_BD_BRINGUP_GUIDE.md`

These are not necessarily for the strict public code-only branch, but they are part of the real frozen engineering baseline.

### Research evidence to keep locally

These remain important as local research artifacts even if they are not pushed to a strict public branch:

- `MAINLINE_READINESS.md`
- `HYBRID_STRESS_MATRIX_REPORT.md`
- `HYBRID_LP_V1_REPORT.md`
- `VITAL_SIGN_HLS_MATRIX_REPORT.md`
- `RESOURCE_COMPARISON_LATEST.md`
- `paper_figures/hls_scene_matrix_summary.csv`
- `paper_figures/hls_hybrid_stress_summary.csv`
- `paper_figures/hls_vital_sign_summary.csv`

## What Should Not Be Changed Before the Next Milestones

Do **not** change these unless a clearly bounded regression fix is required:

- QRD-RLS algorithm structure
- state bridge protocol
- AXI register interface for existing IPs
- scheduler architecture beyond the already validated `th460` freeze

The next stage should consume the frozen baseline, not reopen it.

## Immediate Next Task Order

### Task 1: Freeze repo boundary

Goal:

- separate “publishable baseline” from local experiment products

Main files to use:

- `.gitignore`
- `PROJECT_STRUCTURE.md`
- `CURRENT_FREEZE_AND_NEXT_STEPS.md`

Action:

- stage core code and selected docs only
- keep generated HLS/Vivado/Vitis outputs ignored
- do not `git add .` from the repo root

### Task 2: Freeze radar line

Goal:

- stop modifying radar scheduler RTL
- treat the current `th460` scheduler point as the regression baseline

Reference files:

- `HLS/HYBRID/dual_precision_qrd_top.cpp`
- `tb_hls_hybrid_stress_matrix.cpp`
- `hls_bd_hybrid/app_sources/hybrid_lp_trace_test.c`

Exit condition:

- no more scheduler changes before vital-sign extension and dataset work

### Task 3: Extend vital-sign evidence

Goal:

- strengthen the mmWave line without changing architecture

Recommended new cases:

- longer vital drift
- stronger motion artifact
- low-SNR respiration
- repeated turn-over / recovery

Primary files:

- `tb_hls_vital_sign_matrix.cpp`
- `run_hls_vital_sign_matrix_csim.tcl`
- `VITAL_SIGN_HLS_MATRIX_REPORT.md`

Current local figure output directory:

- `paper_figures/vital_sign_figures`

### Task 4: Start AI-VFF dataset extraction

Goal:

- turn the frozen rule-based controller into a reproducible teacher dataset

Use these local trace sources:

- `paper_figures/hls_scene_matrix_trace.csv`
- `paper_figures/hls_hybrid_stress_trace.csv`
- `paper_figures/hls_vital_sign_trace.csv`

Target schema reference:

- `AI_VFF_DATASET_SCHEMA.md`

Expected local output:

- `ai_vff_dataset/raw_snapshots.parquet`
- `ai_vff_dataset/windows_100.parquet`
- `ai_vff_dataset/windows_100.npz`
- `ai_vff_dataset/splits.yaml`

Important rule:

- collect and normalize data first
- do **not** jump straight to GRU hardware insertion

### Task 5: Only after dataset stability, move to GRU

Goal:

- begin with offline GRU imitation / upgrade
- only then consider software-side scheduler replacement
- only then consider DPU or HLS micro-IP insertion

This is a later milestone, not the next one.

## Recommended Paper Story From This Freeze

Use the following paper-level structure:

1. `FP32` is the numerical safety upper bound.
2. `FIXED_PE` is the low-DSP steady-state engine.
3. `HYBRID_LP` provides the runtime trade-off:
   - close to FP32 behavior in hard scenes
   - closer to fixed behavior in steady scenes
   - reduced FP32 residency instead of reduced raw top-level area
4. The rule-based VFF and monitor signals are already suitable AI scheduler sensors.
5. The next AI step is **dataset + offline GRU**, not immediate hardware insertion.

## Practical Push Criteria

You can treat the current branch as ready to move forward if all of the following remain true:

- no frame errors in board smoke / trace tests
- no unexpected migration growth
- `steady_const_64` remains fixed-only and returns to `lambda_base`
- `th460` remains equal-or-better than `th380` on hard scenes
- radar HLS regressions stay clean

That condition is now effectively met.
