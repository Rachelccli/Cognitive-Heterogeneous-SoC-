#!/usr/bin/env python3
import csv
import json
import os
from collections import defaultdict


ROOT = r"E:\ASSCC"
PAPER_DIR = os.path.join(ROOT, "paper_figures")
OUT_DIR = os.path.join(ROOT, "ai_vff_dataset")
VITAL_BUILD_DIR = os.path.join(ROOT, "hls_prj_vital_sign_matrix_csim", "solution1", "csim", "build")

TRACE_SOURCE_CANDIDATES = {
    "hls_scene": [
        os.path.join(PAPER_DIR, "hls_scene_matrix_trace.csv"),
    ],
    "hls_stress": [
        os.path.join(PAPER_DIR, "hls_hybrid_stress_trace.csv"),
    ],
    "hls_vital": [
        os.path.join(VITAL_BUILD_DIR, "hls_vital_sign_trace.csv"),
        os.path.join(PAPER_DIR, "hls_vital_sign_trace.csv"),
    ],
}

WINDOW_SIZE = 100
WINDOW_TEACHER_PATHS = {"HYBRID_LP"}
WINDOW_FEATURES = [
    "cond_est",
    "delta_norm",
    "scale",
    "overflow",
    "switch_recommend",
    "selected_float",
    "active_float",
    "migration_done",
    "lambda_transit",
]

DEFAULT_SPLITS = {
    "train": [
        "burst10_norm_off",
        "burst10_norm_on",
        "jnr40_norm_off",
        "jnr40_norm_on",
        "jnr55_norm_off",
        "steady_far",
        "steady_highcond_norm_on",
        "transient_jump",
        "multi_jammer",
        "vital_nominal",
        "vital_long_drift",
        "vital_low_snr_respiration",
    ],
    "val": [
        "near_angle",
        "weak_target",
        "burst30_norm_off",
        "jnr55_norm_on",
        "multi_hard_norm_on",
        "vital_motion_burst",
    ],
    "test": [
        "burst30_norm_on",
        "jnr65_norm_on",
        "multi_hard_burst_norm_on",
        "vital_turn_over",
        "vital_strong_motion_artifact",
        "vital_repeated_turn_over_recovery",
    ],
}


def safe_float(value, default=0.0):
    try:
        return float(value)
    except Exception:
        return default


def safe_int(value, default=0):
    try:
        return int(float(value))
    except Exception:
        return default


def resolve_trace_sources():
    selected = {}
    missing = {}
    for source_name, candidates in TRACE_SOURCE_CANDIDATES.items():
        existing = []
        for path in candidates:
            if os.path.exists(path):
                existing.append((os.path.getmtime(path), path))
        if existing:
            existing.sort(reverse=True)
            selected[source_name] = existing[0][1]
        else:
            missing[source_name] = list(candidates)
    return selected, missing


def derive_scene_state(case_name, row):
    cond_est = safe_float(row.get("cond_est", 0.0))
    delta_norm = safe_float(row.get("delta_norm", 0.0))
    overflow = safe_int(row.get("overflow", 0))
    migration_done = safe_int(row.get("migration_done", 0))
    lambda_transit = safe_float(row.get("lambda_transit", 0.955))

    case_name = case_name.lower()

    if overflow or "jnr65" in case_name:
        return "HIGH_RISK"
    if migration_done or delta_norm > 0.75 or "burst" in case_name or "jump" in case_name or "turn_over" in case_name:
        return "TRANSIENT"
    if lambda_transit < 0.954 or "drift" in case_name or "recover" in case_name or "motion" in case_name:
        return "RECOVERING"
    if cond_est > 1000.0 and safe_int(row.get("selected_float", 0)) == 1:
        return "HIGH_RISK"
    return "STEADY"


def derive_lambda_bin(lambda_transit):
    if lambda_transit >= 0.954:
        return "BASE"
    if lambda_transit >= 0.915:
        return "MID"
    return "FAST"


def determine_split(case_name):
    for split_name, case_names in DEFAULT_SPLITS.items():
        if case_name in case_names:
            return split_name
    return "train"


def normalize_row(source_name, source_path, row):
    case_name = (row.get("scene") or row.get("case") or row.get("case_name") or "unknown_case").strip()
    path_name = (row.get("mode") or row.get("path") or "UNKNOWN").strip()
    snapshot_idx = safe_int(row.get("snapshot", row.get("snapshot_idx", 0)))
    cond_est = safe_float(row.get("cond_est", 0.0))
    delta_norm = safe_float(row.get("delta_norm", 0.0))
    scale = safe_float(row.get("scale", 0.0))
    overflow = safe_int(row.get("overflow", 0))
    switch_recommend = safe_int(row.get("switch_recommend", 0))
    selected_float = safe_int(row.get("selected_float", 0))
    active_float = safe_int(row.get("active_float", selected_float))
    migration_done = safe_int(row.get("migration_done", 0))
    migration_direction = safe_int(row.get("migration_direction", 0))
    lambda_transit = safe_float(row.get("lambda_transit", 0.955))
    frame_error = safe_int(row.get("frame_error", 0))
    sequence_id = f"{source_name}:{case_name}:{path_name}"
    scene_state = derive_scene_state(case_name, row)
    lambda_bin = derive_lambda_bin(lambda_transit)

    return {
        "source": source_name,
        "source_file": source_path,
        "case_name": case_name,
        "path": path_name,
        "snapshot_idx": snapshot_idx,
        "sequence_id": sequence_id,
        "lambda_base": safe_float(row.get("lambda_base", 0.955)),
        "cond_threshold": safe_float(row.get("cond_threshold", 0.0)),
        "auto_hold_window": safe_int(row.get("auto_hold_window", 0)),
        "norm_on": safe_int(row.get("enable_norm", row.get("norm_on", 1))),
        "mode": row.get("mode", path_name),
        "cond_est": cond_est,
        "delta_norm": delta_norm,
        "scale": scale,
        "overflow": overflow,
        "switch_recommend": switch_recommend,
        "selected_float": selected_float,
        "active_float": active_float,
        "migration_done": migration_done,
        "migration_direction": migration_direction,
        "lambda_transit": lambda_transit,
        "frame_error": frame_error,
        "teacher_scene_state": scene_state,
        "teacher_target_precision": "FP32" if selected_float else "FIXED",
        "teacher_target_lambda_bin": lambda_bin,
        "teacher_target_lambda_value": lambda_transit,
    }


def load_rows():
    rows = []
    selected_sources, missing_sources = resolve_trace_sources()
    for source_name, source_path in selected_sources.items():
        with open(source_path, "r", encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle)
            for row in reader:
                rows.append(normalize_row(source_name, source_path, row))
    return rows, selected_sources, missing_sources


def write_raw_csv(rows):
    os.makedirs(OUT_DIR, exist_ok=True)
    out_path = os.path.join(OUT_DIR, "raw_snapshots_v0.csv")
    fieldnames = list(rows[0].keys()) if rows else []
    with open(out_path, "w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    return out_path


def summarize_window(window_rows):
    def mean_of(field_name):
        return sum(row[field_name] for row in window_rows) / len(window_rows)

    return {
        "cond_est_mean_100": mean_of("cond_est"),
        "cond_est_max_100": max(row["cond_est"] for row in window_rows),
        "delta_norm_mean_100": mean_of("delta_norm"),
        "delta_norm_max_100": max(row["delta_norm"] for row in window_rows),
        "scale_mean_100": mean_of("scale"),
        "scale_max_100": max(row["scale"] for row in window_rows),
        "overflow_count_100": sum(row["overflow"] for row in window_rows),
        "switch_count_100": sum(row["switch_recommend"] for row in window_rows),
        "selected_float_ratio_100": mean_of("selected_float"),
        "active_float_ratio_100": mean_of("active_float"),
        "migration_count_100": sum(row["migration_done"] for row in window_rows),
    }


def build_windows(rows):
    grouped = defaultdict(list)
    for row in rows:
        if row["path"] in WINDOW_TEACHER_PATHS:
            grouped[row["sequence_id"]].append(row)

    windows = []
    for sequence_id, sequence_rows in grouped.items():
        sequence_rows.sort(key=lambda item: item["snapshot_idx"])
        if len(sequence_rows) < WINDOW_SIZE:
            continue
        for end_idx in range(WINDOW_SIZE - 1, len(sequence_rows)):
            window_rows = sequence_rows[end_idx - WINDOW_SIZE + 1 : end_idx + 1]
            current = window_rows[-1]
            feature_sequence = [
                [row[feature_name] for feature_name in WINDOW_FEATURES]
                for row in window_rows
            ]
            summary = summarize_window(window_rows)
            split_name = determine_split(current["case_name"])
            windows.append(
                {
                    "window_id": f"{sequence_id}:{current['snapshot_idx']}",
                    "sequence_id": sequence_id,
                    "source": current["source"],
                    "case_name": current["case_name"],
                    "path": current["path"],
                    "split": split_name,
                    "window_size": WINDOW_SIZE,
                    "window_start_idx": window_rows[0]["snapshot_idx"],
                    "window_end_idx": current["snapshot_idx"],
                    "lambda_base": current["lambda_base"],
                    "cond_threshold": current["cond_threshold"],
                    "auto_hold_window": current["auto_hold_window"],
                    "norm_on": current["norm_on"],
                    "teacher_scene_state": current["teacher_scene_state"],
                    "teacher_target_precision": current["teacher_target_precision"],
                    "teacher_target_lambda_bin": current["teacher_target_lambda_bin"],
                    "teacher_target_lambda_value": current["teacher_target_lambda_value"],
                    "cond_est_last": current["cond_est"],
                    "delta_norm_last": current["delta_norm"],
                    "scale_last": current["scale"],
                    "overflow_last": current["overflow"],
                    "switch_recommend_last": current["switch_recommend"],
                    "selected_float_last": current["selected_float"],
                    "active_float_last": current["active_float"],
                    "migration_done_last": current["migration_done"],
                    "lambda_transit_last": current["lambda_transit"],
                    "feature_sequence_json": json.dumps(feature_sequence, separators=(",", ":")),
                    **summary,
                }
            )
    return windows


def write_windows_csv(windows):
    out_path = os.path.join(OUT_DIR, "windows_100.csv")
    fieldnames = list(windows[0].keys()) if windows else []
    with open(out_path, "w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(windows)
    return out_path


def build_split_payload(rows, windows, selected_sources):
    available_cases = sorted({row["case_name"] for row in rows})
    split_to_cases = defaultdict(list)
    auto_assigned_cases = []
    explicitly_assigned = {case_name for cases in DEFAULT_SPLITS.values() for case_name in cases}

    for case_name in available_cases:
        split_name = determine_split(case_name)
        split_to_cases[split_name].append(case_name)
        if case_name not in explicitly_assigned:
            auto_assigned_cases.append(case_name)

    raw_rows_by_split = defaultdict(int)
    for row in rows:
        raw_rows_by_split[determine_split(row["case_name"])] += 1

    windows_by_split = defaultdict(int)
    windows_by_case = defaultdict(int)
    for window in windows:
        windows_by_split[window["split"]] += 1
        windows_by_case[window["case_name"]] += 1

    return {
        "version": "v0",
        "policy": {
            "split_unit": "case_name",
            "window_size": WINDOW_SIZE,
            "window_paths": sorted(WINDOW_TEACHER_PATHS),
        },
        "feature_order": list(WINDOW_FEATURES),
        "source_files": selected_sources,
        "splits": {
            "train": {"cases": sorted(split_to_cases["train"])},
            "val": {"cases": sorted(split_to_cases["val"])},
            "test": {"cases": sorted(split_to_cases["test"])},
        },
        "counts": {
            "raw_rows_by_split": dict(sorted(raw_rows_by_split.items())),
            "windows_by_split": dict(sorted(windows_by_split.items())),
            "windows_by_case": dict(sorted(windows_by_case.items())),
        },
        "auto_assigned_cases": sorted(auto_assigned_cases),
    }


def write_splits_yaml(payload):
    out_path = os.path.join(OUT_DIR, "splits.yaml")
    lines = [
        f"version: {payload['version']}",
        "policy:",
        f"  split_unit: {payload['policy']['split_unit']}",
        f"  window_size: {payload['policy']['window_size']}",
        "  window_paths:",
    ]
    for path_name in payload["policy"]["window_paths"]:
        lines.append(f"    - {path_name}")

    lines.append("feature_order:")
    for feature_name in payload["feature_order"]:
        lines.append(f"  - {feature_name}")

    lines.append("source_files:")
    for source_name, source_path in sorted(payload["source_files"].items()):
        lines.append(f"  {source_name}: {json.dumps(source_path)}")

    lines.append("splits:")
    for split_name in ("train", "val", "test"):
        lines.append(f"  {split_name}:")
        lines.append("    cases:")
        for case_name in payload["splits"][split_name]["cases"]:
            lines.append(f"      - {case_name}")

    lines.append("counts:")
    lines.append("  raw_rows_by_split:")
    for split_name, count in sorted(payload["counts"]["raw_rows_by_split"].items()):
        lines.append(f"    {split_name}: {count}")

    lines.append("  windows_by_split:")
    for split_name, count in sorted(payload["counts"]["windows_by_split"].items()):
        lines.append(f"    {split_name}: {count}")

    lines.append("  windows_by_case:")
    for case_name, count in sorted(payload["counts"]["windows_by_case"].items()):
        lines.append(f"    {case_name}: {count}")

    lines.append("auto_assigned_cases:")
    if payload["auto_assigned_cases"]:
        for case_name in payload["auto_assigned_cases"]:
            lines.append(f"  - {case_name}")
    else:
        lines.append("  []")

    with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")
    return out_path


def write_manifest(rows, windows, selected_sources, missing_sources):
    by_source = defaultdict(int)
    by_case = defaultdict(int)
    windows_by_source = defaultdict(int)
    windows_by_case = defaultdict(int)
    windows_by_split = defaultdict(int)

    for row in rows:
        by_source[row["source"]] += 1
        by_case[row["case_name"]] += 1

    for window in windows:
        windows_by_source[window["source"]] += 1
        windows_by_case[window["case_name"]] += 1
        windows_by_split[window["split"]] += 1

    manifest = {
        "dataset_version": "v0",
        "raw_snapshot_file": "raw_snapshots_v0.csv",
        "window_file": "windows_100.csv",
        "split_file": "splits.yaml",
        "row_count": len(rows),
        "window_count": len(windows),
        "window_size": WINDOW_SIZE,
        "window_teacher_paths": sorted(WINDOW_TEACHER_PATHS),
        "window_feature_order": list(WINDOW_FEATURES),
        "sources": dict(sorted(by_source.items())),
        "cases": dict(sorted(by_case.items())),
        "window_counts_by_source": dict(sorted(windows_by_source.items())),
        "window_counts_by_case": dict(sorted(windows_by_case.items())),
        "window_counts_by_split": dict(sorted(windows_by_split.items())),
        "selected_source_files": dict(sorted(selected_sources.items())),
        "missing_sources": sorted(missing_sources.keys()),
        "missing_source_candidates": dict(sorted(missing_sources.items())),
    }
    out_path = os.path.join(OUT_DIR, "manifest_v0.json")
    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")
    return out_path


def main():
    rows, selected_sources, missing_sources = load_rows()
    if not rows:
        print("ERROR: no trace rows found")
        if missing_sources:
            print("Missing sources:")
            for source_name, candidates in sorted(missing_sources.items()):
                print(f"  - {source_name}: {';'.join(candidates)}")
        raise SystemExit(1)

    raw_path = write_raw_csv(rows)
    windows = build_windows(rows)
    windows_path = write_windows_csv(windows)
    split_payload = build_split_payload(rows, windows, selected_sources)
    split_path = write_splits_yaml(split_payload)
    manifest_path = write_manifest(rows, windows, selected_sources, missing_sources)

    print(f"dataset_rows={len(rows)}")
    print(f"window_rows={len(windows)}")
    print(f"raw_snapshot_csv={raw_path}")
    print(f"windows_csv={windows_path}")
    print(f"splits_yaml={split_path}")
    print(f"manifest_json={manifest_path}")
    if missing_sources:
        print("missing_sources=" + ";".join(sorted(missing_sources.keys())))


if __name__ == "__main__":
    main()
