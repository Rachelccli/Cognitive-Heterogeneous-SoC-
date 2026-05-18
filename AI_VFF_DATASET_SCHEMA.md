# AI-VFF Dataset Schema and Workflow

## Purpose

The next AI step is not to place a GRU into hardware immediately. The next AI step is to freeze a trace format that lets the current rule-based scheduler become:

1. a measurable baseline,
2. a teacher for first-pass labels,
3. a source of training examples for a future GRU scheduler.

The current hardware line already exposes the right low-cost sensors:

- `cond_est`
- `delta_R_norm`
- `scale`
- `overflow`
- `switch_recommend`
- `selected_float`
- `migration_done`
- `lambda_transit`

The dataset task is therefore a data-engineering task first, not a new RTL task.

## What Can Be Automated Now

Current HLS traces are enough to build dataset version `v0` automatically. No manual spreadsheet work is required.

The current repository now includes a first local extractor scaffold:

```text
scripts/extract_ai_vff_dataset_v0.py
```

It reads the existing local trace CSV files, normalizes the shared fields, applies deterministic teacher labels, and writes:

```text
ai_vff_dataset/raw_snapshots_v0.csv
ai_vff_dataset/manifest_v0.json
ai_vff_dataset/windows_100.csv
ai_vff_dataset/splits.yaml
```

This is intentionally a **local ignored artifact path**, not a public-release data dump.

Expected local sources include:

```text
paper_figures/hls_scene_matrix_trace.csv
paper_figures/hls_hybrid_stress_trace.csv
paper_figures/hls_vital_sign_trace.csv
```

These files are local experiment artifacts and intentionally ignored by Git. The extractor can read them, normalize the column names, generate labels, and write a local ignored dataset under:

```text
ai_vff_dataset/
```

Board traces are not required to define the schema. Board traces are required later to prove that the HLS-trained feature distribution still matches real PS-DMA-IP execution.

## What Must Wait For Board Data

These items need real BD/board traces:

- hardware-observed latency and throughput,
- real AXI-Lite sampling cadence,
- real `frame_error` behavior,
- board/Vivado power estimates,
- optional temperature or current measurements,
- final validation that the GRU policy remains sensible outside ideal HLS timing.

The right sequence is:

```text
HLS traces -> schema v0 -> automatic extractor -> offline dataset
board traces -> schema-compatible append -> distribution check -> offline GRU
```

## Dataset Layers

Use three layers instead of forcing one giant CSV to do every job.

### Layer 1: Raw Snapshot Table

One row per snapshot, one execution path per row.

Current local implementation:

```text
ai_vff_dataset/raw_snapshots_v0.csv
```

If we later want a training-optimized release artifact, we can add `parquet` without changing the schema itself.

### Layer 2: Windowed GRU Table

One row per prediction point, with a 100-snapshot history window.

Current local implementation:

```text
ai_vff_dataset/windows_100.csv
```

The current CSV form is intentionally dependency-light. Each row keeps metadata, scalar labels, rolling summary features, and a serialized 100-step feature sequence for later tensor conversion. If we later want a training-optimized release artifact, we can add `parquet` or `npz` beside it.

### Layer 3: Split Manifest

One metadata file declaring train/validation/test case ownership.

Suggested file:

```text
ai_vff_dataset/splits.yaml
```

Split by scene/case, not by randomly shuffling snapshots. Random per-snapshot splits leak adjacent dynamics and make a GRU look smarter than it is.

## Raw Snapshot Schema

### Required Identity Columns

| Column | Meaning |
| --- | --- |
| `source` | `hls_scene`, `hls_stress`, `hls_vital`, or `board` |
| `case_name` | scenario identifier such as `burst30_norm_on` |
| `path` | `FP32`, `FIXED_PE`, `HYBRID_FIXED_LAMBDA`, or `HYBRID_LP` |
| `snapshot_idx` | zero-based snapshot index within the case |
| `sequence_id` | stable identifier for windowing and split control |

### Required Configuration Columns

| Column | Meaning |
| --- | --- |
| `lambda_base` | externally programmed forgetting factor |
| `cond_threshold` | programmed condition proxy threshold |
| `auto_hold_window` | programmed hybrid hold window |
| `norm_on` | snapshot normalizer enabled flag |
| `mode` | runtime precision mode when available |

### Required Feature Columns

| Column | Meaning |
| --- | --- |
| `cond_est` | diagonal-R condition proxy |
| `delta_norm` | diagonal-R motion proxy |
| `scale` | snapshot normalization scale / dynamic-range proxy |
| `overflow` | fixed-path overflow indicator |
| `switch_recommend` | monitor-level switch recommendation |
| `selected_float` | precision scheduler output |
| `active_float` | active precision state when available |
| `migration_done` | bridge migration completion flag |
| `migration_direction` | bridge direction when a migration completes |
| `lambda_transit` | effective scheduled lambda exposed by the top |
| `frame_error` | AXIS framing error indicator |

### Optional Result Columns

| Column | Meaning |
| --- | --- |
| `output_re` / `output_im` | beamformer output if present in the trace |
| `reference_re` / `reference_im` | desired/reference signal if present |
| `sinr_db` | per-case or rolling output quality metric |
| `phase_error` | useful for vital-sign traces |
| `resp_peak_hz` | useful for vital-sign spectral validation |
| `heart_peak_hz` | useful for vital-sign spectral validation |

### Board-Only Optional Columns

| Column | Meaning |
| --- | --- |
| `board_latency_cycles` | measured latency for the snapshot |
| `board_latency_us` | wall-clock latency |
| `power_mw` | sampled board or rail power when available |
| `temperature_c` | sampled temperature when available |
| `dma_error` | software-visible DMA error state |

## Labels

### Scene-State Label

Use:

```text
STEADY
TRANSIENT
RECOVERING
HIGH_RISK
```

Suggested deterministic labeling priority for dataset `v0`:

1. `HIGH_RISK`
   - overflow asserted, or
   - known extreme-stress interval, or
   - persistent very-high condition proxy with high active FP32 dependence.
2. `TRANSIENT`
   - burst/jump onset window, or
   - large `delta_norm`, or
   - migration onset associated with abrupt scene change.
3. `RECOVERING`
   - snapshots after a transient until the system returns to calm criteria and lambda returns toward base.
4. `STEADY`
   - all remaining nominal calm snapshots.

For the first pass, labels may combine scenario metadata with sensor-derived rules. That is acceptable because the goal is to build a supervised policy dataset, not pretend the labels fell from the sky.

### Control Labels

| Label | Meaning |
| --- | --- |
| `target_precision` | `FIXED` or `FP32` |
| `target_lambda_bin` | `BASE`, `MID`, or `FAST` |
| `target_lambda_value` | numeric lambda target |

Use two label families:

1. `teacher_*`
   - copied from the frozen rule-based controller;
   - useful for behavior cloning and parity checks.
2. `oracle_*`
   - derived later from ablations or optimization under a quality/activity objective;
   - useful when training a GRU to improve on the handcrafted controller rather than merely imitate it.

Keeping both families prevents a quiet conceptual trap: a model trained only to copy the rule scheduler cannot later be advertised as having discovered a better scheduler.

## 100-Snapshot GRU Window Format

The target future GRU input is a 100-snapshot sequence.

Recommended ordered feature vector per snapshot:

```text
[cond_est, delta_norm, scale, overflow, switch_recommend,
 selected_float, active_float, migration_done, lambda_transit]
```

The current local `windows_100.csv` generator uses exactly this feature order and stores it as a sequence payload per window.

Recommended tensor shape:

```text
[num_windows, 100, num_features]
```

Recommended scalar side inputs, if needed:

```text
[lambda_base, cond_threshold, auto_hold_window, norm_on]
```

Recommended outputs:

```text
scene_state
target_precision
target_lambda_bin
```

For dataset `v0`, the window generator should use `HYBRID_LP` sequences as the teacher path by default. That keeps the labels aligned with the frozen rule-based controller we actually want the later GRU to imitate or improve upon.

The numeric lambda can be reconstructed from `target_lambda_bin` plus the configured base/mid/fast schedule, or predicted directly later if the discrete version is too coarse.

## Normalization

Fit normalization statistics on the training split only:

- log-scale `cond_est` if its dynamic range is wide,
- consider clipping or robust scaling for `delta_norm` and `scale`,
- keep binary flags binary,
- store normalization parameters beside the split manifest.

Never fit scalers on all scenes before the split. That is another small, friendly-looking leak.

## Recommended Initial Split

Use scene-level splits, not random rows.

### Radar

- Train candidates:
  - `steady_far`
  - `transient_jump`
  - `multi_jammer`
  - lower JNR and lower burst cases
- Validation candidates:
  - `near_angle`
  - `weak_target`
  - one medium stress case
- Test candidates:
  - `burst30_norm_on`
  - `jnr65_norm_on`
  - `multi_hard_burst_norm_on`

### Vital-Sign

Do not mix every vital case into training immediately.

- Train candidates:
  - `vital_nominal`
  - one mild drift case when added
- Validation candidate:
  - `vital_motion_burst`
- Test candidate:
  - `vital_turn_over`
  - stronger/repeated motion cases when added

The point is to test whether the scheduler generalizes across motion regimes, not just across neighboring rows.

## Evaluation Metrics

### Classification / Control Metrics

- scene-state macro F1,
- precision-mode accuracy,
- lambda-bin accuracy,
- false FP32 entry rate in calm windows,
- missed FP32 entry rate in high-risk windows,
- migration count versus baseline,
- selected-FP32 ratio versus baseline.

### System Metrics

- output SINR,
- vital-sign phase tracking error,
- respiration / heartbeat peak preservation,
- precision recovery ratio,
- activity proxy:

```text
FP32 activity reduction = 1 - selected_float_ratio
```

- later, board energy proxy or measured energy:

```text
E_hybrid ~= alpha * E_FP32 + (1 - alpha) * E_FIXED + N_mig * E_bridge
```

where `alpha = selected_float_ratio`.

Do not call `selected_float_ratio` a speedup metric. It is an activity/duty-cycle proxy until paired with real timing and power measurements.

## HLS-to-Board Merge Plan

When board traces exist, append them without changing the schema:

1. Export board snapshots with the same column names.
2. Set `source=board`.
3. Preserve `case_name`, `snapshot_idx`, and programmed configuration values.
4. Run distribution checks:
   - feature histograms,
   - per-feature quantiles,
   - selected-float ratio,
   - migration rate,
   - lambda range.
5. Compare HLS and board output traces on the same deterministic vectors.
6. Only then decide whether to:
   - train on HLS and validate on board,
   - fine-tune with board data,
   - or revise labels/features.

Board data does not invalidate the HLS dataset. It tells us whether the simulator-shaped world and the physical world are still close cousins.

## Automation Ownership

### Can Be Automated By The Project Scripts

- reading the existing HLS CSV traces,
- standardizing column names,
- constructing raw snapshot rows,
- generating deterministic labels,
- creating 100-snapshot windows,
- writing split manifests,
- computing dataset summary statistics,
- later appending board traces if exported as CSV.

### Requires A Real Run Or Human Choice

- obtaining board traces in the first place,
- deciding the final publication split protocol,
- choosing whether the GRU should imitate the rule scheduler or optimize against an oracle objective,
- validating that power claims are supported by measured data.

## Recommended Sequence

1. Keep the frozen rule-based controller as the hardware baseline.
2. Build `dataset_v0` from HLS traces now.
3. Complete FP32/FIXED/HYBRID board bring-up.
4. Capture board traces using the same deterministic cases.
5. Merge HLS and board data under the shared schema.
6. Train an offline GRU.
7. Replay GRU predictions against saved traces in software.
8. Only after clear offline benefit, design a small GRU HLS micro-IP or PS-side controller.

This keeps the AI line alive without letting it hijack the hardware bring-up before the road exists.
