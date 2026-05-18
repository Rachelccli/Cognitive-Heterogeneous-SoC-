#!/usr/bin/env python3
import csv
import json
import os
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple


ROOT = r"E:\ASSCC"
DATA_DIR = os.path.join(ROOT, "ai_vff_dataset")
DEFAULT_WINDOWS_PATH = os.path.join(DATA_DIR, "windows_100.csv")
DEFAULT_SPLITS_PATH = os.path.join(DATA_DIR, "splits.yaml")

WINDOW_SEQUENCE_FEATURES = [
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

CLEAN_SEQUENCE_FEATURES = [
    "cond_est",
    "delta_norm",
    "scale",
    "overflow",
    "switch_recommend",
]

SUMMARY_FEATURES = [
    "cond_est_last",
    "delta_norm_last",
    "scale_last",
    "overflow_last",
    "switch_recommend_last",
    "cond_est_mean_100",
    "cond_est_max_100",
    "delta_norm_mean_100",
    "delta_norm_max_100",
    "scale_mean_100",
    "scale_max_100",
    "overflow_count_100",
    "switch_count_100",
]

STATIC_FEATURES = [
    "lambda_base",
    "cond_threshold",
    "auto_hold_window",
    "norm_on",
]

BASELINE0_FEATURES = SUMMARY_FEATURES + STATIC_FEATURES

LABEL_TO_INT = {
    "FIXED": 0,
    "FP32": 1,
}

INT_TO_LABEL = {
    0: "FIXED",
    1: "FP32",
}

SCENE_STATE_ORDER = ["STEADY", "TRANSIENT", "RECOVERING", "HIGH_RISK"]
SPLIT_ORDER = ["train", "val", "test"]


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


@dataclass
class WindowExample:
    window_id: str
    sequence_id: str
    source: str
    case_name: str
    split: str
    teacher_scene_state: str
    teacher_target_precision: int
    teacher_target_precision_name: str
    teacher_target_lambda_bin: str
    baseline0_features: List[float]
    clean_sequence: Optional[List[List[float]]]
    static_features: List[float]
    selected_float_ratio_100: float
    migration_count_100: float


def load_split_manifest(path: str = DEFAULT_SPLITS_PATH) -> Tuple[Dict[str, List[str]], Dict[str, str]]:
    split_to_cases = {split_name: [] for split_name in SPLIT_ORDER}
    case_to_split: Dict[str, str] = {}

    if not os.path.exists(path):
        raise FileNotFoundError(f"missing split manifest: {path}")

    in_splits = False
    current_split: Optional[str] = None

    with open(path, "r", encoding="utf-8") as handle:
        for raw_line in handle:
            line = raw_line.rstrip("\n")
            stripped = line.strip()

            if not stripped or stripped.startswith("#"):
                continue

            if not line.startswith(" "):
                in_splits = stripped == "splits:"
                if not in_splits:
                    current_split = None
                continue

            if not in_splits:
                continue

            if line.startswith("  ") and not line.startswith("    ") and stripped.endswith(":"):
                candidate = stripped[:-1]
                current_split = candidate if candidate in split_to_cases else None
                continue

            if current_split is None:
                continue

            if stripped == "cases:":
                continue

            if stripped.startswith("- "):
                case_name = stripped[2:].strip()
                split_to_cases[current_split].append(case_name)
                case_to_split[case_name] = current_split

    return split_to_cases, case_to_split


def parse_feature_sequence(feature_sequence_json: str) -> List[List[float]]:
    payload = json.loads(feature_sequence_json)
    return [[safe_float(item) for item in step] for step in payload]


def build_clean_sequence(raw_sequence: List[List[float]]) -> List[List[float]]:
    clean_feature_count = len(CLEAN_SEQUENCE_FEATURES)
    return [step[:clean_feature_count] for step in raw_sequence]


def build_baseline0_features(row: Dict[str, str]) -> List[float]:
    return [safe_float(row[field_name]) for field_name in BASELINE0_FEATURES]


def build_static_features(row: Dict[str, str]) -> List[float]:
    return [safe_float(row[field_name]) for field_name in STATIC_FEATURES]


def load_window_examples(
    windows_path: str = DEFAULT_WINDOWS_PATH,
    splits_path: str = DEFAULT_SPLITS_PATH,
    include_sequences: bool = False,
) -> Tuple[List[WindowExample], Dict[str, object]]:
    if not os.path.exists(windows_path):
        raise FileNotFoundError(f"missing windows file: {windows_path}")

    split_to_cases, case_to_split = load_split_manifest(splits_path)
    examples: List[WindowExample] = []

    split_alignment = {
        "rows_loaded": 0,
        "manifest_case_count": len(case_to_split),
        "csv_split_mismatches": 0,
        "rows_without_manifest_case": 0,
    }

    with open(windows_path, "r", encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            split_alignment["rows_loaded"] += 1

            case_name = row["case_name"]
            manifest_split = case_to_split.get(case_name)
            csv_split = row.get("split", "")

            if manifest_split is None:
                split_alignment["rows_without_manifest_case"] += 1
                assigned_split = csv_split or "train"
            else:
                assigned_split = manifest_split
                if csv_split and csv_split != manifest_split:
                    split_alignment["csv_split_mismatches"] += 1

            raw_sequence = None
            clean_sequence = None
            if include_sequences:
                raw_sequence = parse_feature_sequence(row["feature_sequence_json"])
                clean_sequence = build_clean_sequence(raw_sequence)

            precision_name = row["teacher_target_precision"]
            if precision_name not in LABEL_TO_INT:
                raise ValueError(f"unsupported precision label: {precision_name}")

            examples.append(
                WindowExample(
                    window_id=row["window_id"],
                    sequence_id=row["sequence_id"],
                    source=row["source"],
                    case_name=case_name,
                    split=assigned_split,
                    teacher_scene_state=row["teacher_scene_state"],
                    teacher_target_precision=LABEL_TO_INT[precision_name],
                    teacher_target_precision_name=precision_name,
                    teacher_target_lambda_bin=row["teacher_target_lambda_bin"],
                    baseline0_features=build_baseline0_features(row),
                    clean_sequence=clean_sequence,
                    static_features=build_static_features(row),
                    selected_float_ratio_100=safe_float(row.get("selected_float_ratio_100", 0.0)),
                    migration_count_100=safe_float(row.get("migration_count_100", 0.0)),
                )
            )

    metadata = {
        "windows_path": windows_path,
        "splits_path": splits_path,
        "split_to_cases": split_to_cases,
        "case_to_split": case_to_split,
        "split_alignment": split_alignment,
        "baseline0_feature_names": list(BASELINE0_FEATURES),
        "clean_sequence_feature_names": list(CLEAN_SEQUENCE_FEATURES),
        "static_feature_names": list(STATIC_FEATURES),
    }
    return examples, metadata


def split_examples(examples: List[WindowExample]) -> Dict[str, List[WindowExample]]:
    grouped = {split_name: [] for split_name in SPLIT_ORDER}
    for example in examples:
        grouped.setdefault(example.split, []).append(example)
    return grouped


def count_cases(examples: List[WindowExample]) -> int:
    return len({example.case_name for example in examples})
