#!/usr/bin/env python3
import argparse
import json
import os
from collections import Counter
from typing import Dict, List

from ai_vff_baseline_data import (
    CLEAN_SEQUENCE_FEATURES,
    DEFAULT_SPLITS_PATH,
    DEFAULT_WINDOWS_PATH,
    SPLIT_ORDER,
    count_cases,
    load_window_examples,
    split_examples,
)


ROOT = r"E:\ASSCC"
OUT_DIR = os.path.join(ROOT, "ai_vff_dataset", "baseline1_gru_inputs")


def build_export_record(example) -> Dict[str, object]:
    return {
        "window_id": example.window_id,
        "sequence_id": example.sequence_id,
        "source": example.source,
        "case_name": example.case_name,
        "split": example.split,
        "teacher_scene_state": example.teacher_scene_state,
        "teacher_target_precision": example.teacher_target_precision_name,
        "teacher_target_precision_int": example.teacher_target_precision,
        "teacher_target_lambda_bin": example.teacher_target_lambda_bin,
        "clean_sequence": example.clean_sequence,
        "static_features": example.static_features,
        "selected_float_ratio_100": example.selected_float_ratio_100,
        "migration_count_100": example.migration_count_100,
    }


def write_jsonl(path: str, records: List[Dict[str, object]]):
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        for record in records:
            handle.write(json.dumps(record, separators=(",", ":")))
            handle.write("\n")


def split_summary(examples) -> Dict[str, object]:
    payload = {}
    for split_name in SPLIT_ORDER:
        current = examples.get(split_name, [])
        payload[split_name] = {
            "window_count": len(current),
            "case_count": count_cases(current),
            "precision_counts": dict(sorted(Counter(item.teacher_target_precision_name for item in current).items())),
            "scene_state_counts": dict(sorted(Counter(item.teacher_scene_state for item in current).items())),
        }
    return payload


def build_manifest(grouped_examples, metadata, out_dir: str) -> Dict[str, object]:
    return {
        "purpose": "baseline1_clean_sequence_gru_input",
        "windows_path": metadata["windows_path"],
        "splits_path": metadata["splits_path"],
        "clean_sequence_feature_names": list(CLEAN_SEQUENCE_FEATURES),
        "static_feature_names": list(metadata["static_feature_names"]),
        "sequence_length": 100,
        "feature_count": len(CLEAN_SEQUENCE_FEATURES),
        "split_alignment": metadata["split_alignment"],
        "split_summary": split_summary(grouped_examples),
        "jsonl_files": {
            split_name: os.path.join(out_dir, f"{split_name}.jsonl")
            for split_name in SPLIT_ORDER
        },
    }


def detect_torch() -> Dict[str, object]:
    try:
        import torch  # type: ignore

        return {
            "available": True,
            "version": getattr(torch, "__version__", "unknown"),
        }
    except Exception as exc:
        return {
            "available": False,
            "reason": str(exc),
        }


def parse_args():
    parser = argparse.ArgumentParser(
        description="Prepare clean sensor-only sequence inputs for the AI-VFF Baseline 1 GRU run."
    )
    parser.add_argument("--windows", default=DEFAULT_WINDOWS_PATH, help="Path to windows_100.csv")
    parser.add_argument("--splits", default=DEFAULT_SPLITS_PATH, help="Path to splits.yaml")
    parser.add_argument("--out-dir", default=OUT_DIR, help="Output directory for split JSONL files")
    return parser.parse_args()


def main():
    args = parse_args()
    os.makedirs(args.out_dir, exist_ok=True)

    examples, metadata = load_window_examples(
        windows_path=args.windows,
        splits_path=args.splits,
        include_sequences=True,
    )
    grouped = split_examples(examples)

    for split_name in SPLIT_ORDER:
        records = [build_export_record(example) for example in grouped.get(split_name, [])]
        write_jsonl(os.path.join(args.out_dir, f"{split_name}.jsonl"), records)

    manifest = build_manifest(grouped, metadata, args.out_dir)
    manifest_path = os.path.join(args.out_dir, "manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")

    torch_status = detect_torch()
    status_path = os.path.join(args.out_dir, "runtime_status.json")
    with open(status_path, "w", encoding="utf-8") as handle:
        json.dump({"torch": torch_status}, handle, indent=2)
        handle.write("\n")

    print(f"manifest_json={manifest_path}")
    print(f"runtime_status_json={status_path}")
    print(f"train_jsonl={os.path.join(args.out_dir, 'train.jsonl')}")
    print(f"val_jsonl={os.path.join(args.out_dir, 'val.jsonl')}")
    print(f"test_jsonl={os.path.join(args.out_dir, 'test.jsonl')}")
    print(
        "torch_available="
        + ("yes" if torch_status["available"] else "no")
    )
    if not torch_status["available"]:
        print("torch_reason=" + torch_status["reason"])


if __name__ == "__main__":
    main()
