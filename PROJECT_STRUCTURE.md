# GitHub Project Structure

This repository should publish the frozen 2026-05-14 mainline only: the mature FP32, FIXED_PE, HYBRID_LP, and shared monitor/normalizer source needed to synthesize the current accelerator baseline. Local HLS projects, traces, figures, benches, papers, and exploratory scripts stay out of Git.

## Commit

Recommended source tree for the clean GitHub branch:

```text
HLS/
  COMMON/
    cond_estimator.{h,cpp}
    snapshot_normalizer.{h,cpp}
  FP32/
    qrd_rls_fp32_8x8.{h,cpp}
    qrd_rls_fp32_8x8_axis.{h,cpp}
  FIXED_PE/
    qrd_rls_cordic_pe_8x8.{h,cpp}
    qrd_fixed_pe_monitor_top.{h,cpp}
  HYBRID/
    dual_precision_qrd_top.{h,cpp}
    state_bridge.{h,cpp}
qrd_axis_types.h
qrd_rls_cordic_8x8.h
run_hls_dual_precision_qrd_top.tcl
run_hls_fp32_8x8_axis.tcl
run_hls_qrd_fixed_pe_update_top.tcl
run_hls_qrd_fixed_pe_monitor_top.tcl
export_hls_fp32_8x8_axis.tcl
export_hls_qrd_fixed_pe_monitor_top.tcl
export_hls_dual_precision_qrd_top.tcl
export_hls_all_ips.cmd
README.md
PROJECT_STRUCTURE.md
DUAL_TRACK_NEXT_STEPS.md
VIVADO_BD_BRINGUP_GUIDE.md
AI_VFF_DATASET_SCHEMA.md
```

The release `run_hls_*.tcl` files are synthesis-oriented and do not depend on local `tb_*.cpp` files. C-simulation matrices remain local validation assets and are ignored.
The `export_hls_*.tcl` files reuse synthesized HLS projects and export IPs into the ignored local `ip_repo/` directory.

Keep these reports only if the GitHub repository is also used as a research snapshot. They are ignored by default for the strict release branch; use `git add -f` if a separate research branch should publish them.

```text
MAINLINE_READINESS.md
RESOURCE_COMPARISON_LATEST.md
HYBRID_LP_V1_REPORT.md
HYBRID_STRESS_MATRIX_REPORT.md
VITAL_SIGN_HLS_MATRIX_REPORT.md
FIXED_PE_paper_architecture_assessment.md
FIXED_PE_roadmap.md
```

## Ignore

Do not commit these categories:

- `hls_prj*/`: generated Vitis HLS projects, C-sim builds, synthesis reports, RTL, drivers.
- `ip_repo/`, `vivado_prj*/`, `bd_prj*/`: exported IP packages and local Vivado integration projects.
- `paper_figures/`, `scene_figures/`, `scheduler_sweep/`, `logs/`: generated CSV, PNG/PDF figures, and run logs.
- `tb_*.cpp`: local C-sim testbenches and stress-matrix generators.
- `plot_*.py`, `verify_dual_precision.py`, `compare_python_hls_alignment.py`: local analysis and plotting tools.
- `asscc_related/`, `conference-latex-template*/`: private references and writing assets.
- `HLS/FIXED/`: legacy folded fixed path, superseded by `HLS/FIXED_PE` for the current mainline.

## Shared Utility Boundary

`cond_estimator` and `snapshot_normalizer` now have a single source of truth in `HLS/COMMON`.

- `HLS/FIXED_PE/qrd_fixed_pe_monitor_top.cpp` includes `../COMMON/...`.
- `HLS/HYBRID/dual_precision_qrd_top.cpp` includes `../COMMON/...`.
- TCL scripts should add `HLS/COMMON/cond_estimator.{h,cpp}` and `HLS/COMMON/snapshot_normalizer.{h,cpp}`.
- The old duplicate copies under `HLS/FIXED` and `HLS/HYBRID` are local legacy files. If a local clone still tracks them, `.gitignore` alone is not enough; remove them from the release branch index before the clean GitHub commit.

## Release Branch Rule

Use exact-path staging instead of `git add .` for the first clean GitHub commit. A safe starting point is:

```powershell
# Optional but recommended for the release branch: stop publishing legacy duplicates.
git rm --cached -r HLS/FIXED
git rm --cached HLS/HYBRID/cond_estimator.cpp HLS/HYBRID/cond_estimator.h
git rm --cached HLS/HYBRID/snapshot_normalizer.cpp HLS/HYBRID/snapshot_normalizer.h

git add .gitignore PROJECT_STRUCTURE.md README.md
git add HLS/COMMON HLS/FP32 HLS/FIXED_PE
git add HLS/HYBRID/dual_precision_qrd_top.cpp HLS/HYBRID/dual_precision_qrd_top.h
git add HLS/HYBRID/state_bridge.cpp HLS/HYBRID/state_bridge.h
git add qrd_axis_types.h qrd_rls_cordic_8x8.h
git add run_hls_dual_precision_qrd_top.tcl run_hls_fp32_8x8_axis.tcl run_hls_qrd_fixed_pe_update_top.tcl run_hls_qrd_fixed_pe_monitor_top.tcl
git add export_hls_fp32_8x8_axis.tcl export_hls_qrd_fixed_pe_monitor_top.tcl export_hls_dual_precision_qrd_top.tcl export_hls_all_ips.cmd
git add DUAL_TRACK_NEXT_STEPS.md VIVADO_BD_BRINGUP_GUIDE.md AI_VFF_DATASET_SCHEMA.md
```

For a strict code-only public release, stop here. For a research-snapshot branch, additionally add:

```powershell
git add -f MAINLINE_READINESS.md RESOURCE_COMPARISON_LATEST.md HYBRID_LP_V1_REPORT.md HYBRID_STRESS_MATRIX_REPORT.md VITAL_SIGN_HLS_MATRIX_REPORT.md
git add -f FIXED_PE_paper_architecture_assessment.md FIXED_PE_roadmap.md
```
