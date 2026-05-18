#!/usr/bin/env python3
import csv
import json
import os
from collections import defaultdict


ROOT = r"E:\ASSCC"
PAPER_DIR = os.path.join(ROOT, "paper_figures")
OUT_DIR = os.path.join(ROOT, "ai_vff_dataset")

TRACE_SOURCES = [
    ("hls_scene", os.path.join(PAPER_DIR, "hls_scene_matrix_trace.csv")),
    ("hls_stress", os.path.join(PAPER_DIR, "hls_hybrid_stress_trace.csv")),
    ("hls_vital", os.path.join(PAPER_DIR, "hls_vital_sign_trace.csv")),
]


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


def normalize_row(source_name, row):
    case_name = row.get("scene") or row.get("case") or row.get("case_name") or "unknown_case"
    path_name = row.get("mode") or row.get("path") or "UNKNOWN"
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
    missing = []
    for source_name, path in TRACE_SOURCES:
        if not os.path.exists(path):
            missing.append(path)
            continue
        with open(path, "r", encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle)
            for row in reader:
                rows.append(normalize_row(source_name, row))
    return rows, missing


def write_raw_csv(rows):
    os.makedirs(OUT_DIR, exist_ok=True)
    out_path = os.path.join(OUT_DIR, "raw_snapshots_v0.csv")
    fieldnames = list(rows[0].keys()) if rows else []
    with open(out_path, "w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    return out_path


def write_manifest(rows, missing):
    by_source = defaultdict(int)
    by_case = defaultdict(int)
    for row in rows:
        by_source[row["source"]] += 1
        by_case[row["case_name"]] += 1

    manifest = {
        "dataset_version": "v0",
        "raw_snapshot_file": "raw_snapshots_v0.csv",
        "row_count": len(rows),
        "sources": dict(sorted(by_source.items())),
        "cases": dict(sorted(by_case.items())),
        "missing_sources": missing,
        "next_expected_outputs": [
            "windows_100.parquet",
            "windows_100.npz",
            "splits.yaml",
        ],
    }
    out_path = os.path.join(OUT_DIR, "manifest_v0.json")
    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")
    return out_path


def main():
    rows, missing = load_rows()
    if not rows:
        print("ERROR: no trace rows found")
        if missing:
            print("Missing sources:")
            for item in missing:
                print(f"  - {item}")
        raise SystemExit(1)

    raw_path = write_raw_csv(rows)
    manifest_path = write_manifest(rows, missing)

    print(f"dataset_rows={len(rows)}")
    print(f"raw_snapshot_csv={raw_path}")
    print(f"manifest_json={manifest_path}")
    if missing:
        print("missing_sources=" + ";".join(missing))


if __name__ == "__main__":
    main()
