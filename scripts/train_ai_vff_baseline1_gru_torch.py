#!/usr/bin/env python3
import argparse
import json
import math
import os
import random
from collections import Counter, defaultdict
from typing import Dict, List, Tuple

import torch
from torch import nn
from torch.utils.data import DataLoader, Dataset

from ai_vff_baseline_data import (
    CLEAN_SEQUENCE_FEATURES,
    DEFAULT_SPLITS_PATH,
    DEFAULT_WINDOWS_PATH,
    SCENE_STATE_ORDER,
    count_cases,
    load_window_examples,
    split_examples,
)


ROOT = r"E:\ASSCC"
OUT_DIR = os.path.join(ROOT, "ai_vff_dataset", "baseline1_outputs")


def set_seed(seed: int):
    random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def safe_div(numerator: float, denominator: float) -> float:
    return numerator / denominator if denominator else 0.0


def percentile(values: List[float], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    index = fraction * (len(ordered) - 1)
    lower = int(math.floor(index))
    upper = int(math.ceil(index))
    if lower == upper:
        return ordered[lower]
    weight = index - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def mean(values: List[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def compute_sequence_normalizer(rows) -> Dict[str, List[float]]:
    feature_count = len(CLEAN_SEQUENCE_FEATURES)
    means = []
    stds = []

    for feature_index in range(feature_count):
        values = []
        for row in rows:
            for step in row.clean_sequence:
                values.append(float(step[feature_index]))
        current_mean = mean(values)
        variance = mean([(value - current_mean) ** 2 for value in values])
        current_std = math.sqrt(variance)
        means.append(current_mean)
        stds.append(current_std if current_std > 1.0e-8 else 1.0)

    return {
        "mean": means,
        "std": stds,
    }


class WindowSequenceDataset(Dataset):
    def __init__(self, rows, normalizer: Dict[str, List[float]]):
        self.rows = rows
        self.norm_mean = normalizer["mean"]
        self.norm_std = normalizer["std"]

    def __len__(self):
        return len(self.rows)

    def __getitem__(self, index: int):
        row = self.rows[index]
        normalized_sequence = []
        for step in row.clean_sequence:
            normalized_sequence.append(
                [
                    (float(value) - self.norm_mean[feature_index]) / self.norm_std[feature_index]
                    for feature_index, value in enumerate(step)
                ]
            )

        return {
            "sequence": torch.tensor(normalized_sequence, dtype=torch.float32),
            "target": torch.tensor(row.teacher_target_precision, dtype=torch.long),
            "window_id": row.window_id,
            "case_name": row.case_name,
            "scene_state": row.teacher_scene_state,
            "teacher_ratio": torch.tensor(row.selected_float_ratio_100, dtype=torch.float32),
        }


class PrecisionOnlyGRU(nn.Module):
    def __init__(self, input_dim: int, hidden_dim: int, num_layers: int, dropout: float):
        super().__init__()
        effective_dropout = dropout if num_layers > 1 else 0.0
        self.gru = nn.GRU(
            input_size=input_dim,
            hidden_size=hidden_dim,
            num_layers=num_layers,
            batch_first=True,
            dropout=effective_dropout,
        )
        self.classifier = nn.Sequential(
            nn.Linear(hidden_dim, hidden_dim),
            nn.ReLU(),
            nn.Linear(hidden_dim, 2),
        )

    def forward(self, sequence_batch: torch.Tensor) -> torch.Tensor:
        output, hidden = self.gru(sequence_batch)
        final_hidden = hidden[-1]
        return self.classifier(final_hidden)


def collate_batch(batch):
    sequence = torch.stack([item["sequence"] for item in batch], dim=0)
    target = torch.stack([item["target"] for item in batch], dim=0)
    teacher_ratio = torch.stack([item["teacher_ratio"] for item in batch], dim=0)
    return {
        "sequence": sequence,
        "target": target,
        "teacher_ratio": teacher_ratio,
        "window_id": [item["window_id"] for item in batch],
        "case_name": [item["case_name"] for item in batch],
        "scene_state": [item["scene_state"] for item in batch],
    }


def make_loader(rows, normalizer, batch_size: int, shuffle: bool) -> DataLoader:
    dataset = WindowSequenceDataset(rows, normalizer)
    return DataLoader(
        dataset,
        batch_size=batch_size,
        shuffle=shuffle,
        num_workers=0,
        collate_fn=collate_batch,
    )


def predict_epoch(model, loader, device):
    model.eval()
    probabilities = []
    labels = []
    rows_meta = []
    losses = []
    loss_fn = nn.CrossEntropyLoss(reduction="none")

    with torch.no_grad():
        for batch in loader:
            sequence = batch["sequence"].to(device)
            target = batch["target"].to(device)
            logits = model(sequence)
            batch_losses = loss_fn(logits, target)
            probs = torch.softmax(logits, dim=1)[:, 1]

            losses.extend(batch_losses.detach().cpu().tolist())
            probabilities.extend(probs.detach().cpu().tolist())
            labels.extend(batch["target"].tolist())
            for index in range(len(batch["window_id"])):
                rows_meta.append(
                    {
                        "window_id": batch["window_id"][index],
                        "case_name": batch["case_name"][index],
                        "scene_state": batch["scene_state"][index],
                        "teacher_ratio": float(batch["teacher_ratio"][index].item()),
                    }
                )

    return {
        "loss": mean(losses),
        "probabilities": probabilities,
        "labels": labels,
        "rows_meta": rows_meta,
    }


def train_epoch(model, loader, optimizer, device, class_weights):
    model.train()
    loss_fn = nn.CrossEntropyLoss(weight=class_weights)
    losses = []

    for batch in loader:
        sequence = batch["sequence"].to(device)
        target = batch["target"].to(device)

        optimizer.zero_grad()
        logits = model(sequence)
        loss = loss_fn(logits, target)
        loss.backward()
        optimizer.step()

        losses.append(float(loss.detach().cpu().item()))

    return mean(losses)


def confusion_counts(labels: List[int], predictions: List[int]) -> Dict[str, int]:
    counts = {"tp": 0, "tn": 0, "fp": 0, "fn": 0}
    for label, prediction in zip(labels, predictions):
        if label == 1 and prediction == 1:
            counts["tp"] += 1
        elif label == 0 and prediction == 0:
            counts["tn"] += 1
        elif label == 0 and prediction == 1:
            counts["fp"] += 1
        else:
            counts["fn"] += 1
    return counts


def f1_for_positive(label_value: int, labels: List[int], predictions: List[int]) -> float:
    tp = fp = fn = 0
    for label, prediction in zip(labels, predictions):
        if prediction == label_value and label == label_value:
            tp += 1
        elif prediction == label_value and label != label_value:
            fp += 1
        elif prediction != label_value and label == label_value:
            fn += 1
    precision = safe_div(tp, tp + fp)
    recall = safe_div(tp, tp + fn)
    if precision + recall == 0.0:
        return 0.0
    return 2.0 * precision * recall / (precision + recall)


def evaluate_predictions(probabilities: List[float], labels: List[int], rows_meta: List[Dict[str, object]]) -> Dict[str, object]:
    predictions = [1 if prob >= 0.5 else 0 for prob in probabilities]
    counts = confusion_counts(labels, predictions)
    tp = counts["tp"]
    tn = counts["tn"]
    fp = counts["fp"]
    fn = counts["fn"]

    accuracy = safe_div(tp + tn, len(labels))
    recall_fp32 = safe_div(tp, tp + fn)
    recall_fixed = safe_div(tn, tn + fp)
    balanced_accuracy = 0.5 * (recall_fp32 + recall_fixed)
    macro_f1 = 0.5 * (
        f1_for_positive(0, labels, predictions) +
        f1_for_positive(1, labels, predictions)
    )

    steady_indices = [index for index, item in enumerate(rows_meta) if item["scene_state"] == "STEADY"]
    transient_indices = [
        index
        for index, item in enumerate(rows_meta)
        if item["scene_state"] in ("TRANSIENT", "HIGH_RISK")
    ]

    false_fp32_entry_rate = (
        sum(1 for index in steady_indices if predictions[index] == 1) / len(steady_indices)
        if steady_indices
        else None
    )
    missed_fp32_entry_rate = (
        sum(1 for index in transient_indices if predictions[index] == 0) / len(transient_indices)
        if transient_indices
        else None
    )

    predicted_fp32_duty = mean(probabilities)
    teacher_fp32_duty = mean([float(item["teacher_ratio"]) for item in rows_meta])
    fp32_duty_error = predicted_fp32_duty - teacher_fp32_duty

    per_case = defaultdict(lambda: {"probs": [], "teacher": [], "scene_states": Counter(), "rows": 0})
    for item, prob in zip(rows_meta, probabilities):
        payload = per_case[item["case_name"]]
        payload["probs"].append(prob)
        payload["teacher"].append(float(item["teacher_ratio"]))
        payload["scene_states"][item["scene_state"]] += 1
        payload["rows"] += 1

    abs_case_errors = []
    case_summary = {}
    for case_name, payload in sorted(per_case.items()):
        predicted_ratio = mean(payload["probs"])
        teacher_ratio = mean(payload["teacher"])
        absolute_error = abs(predicted_ratio - teacher_ratio)
        abs_case_errors.append(absolute_error)
        case_summary[case_name] = {
            "rows": payload["rows"],
            "predicted_fp32_ratio": predicted_ratio,
            "teacher_fp32_ratio": teacher_ratio,
            "absolute_fp32_ratio_error": absolute_error,
            "scene_states": {
                state_name: payload["scene_states"].get(state_name, 0)
                for state_name in SCENE_STATE_ORDER
                if payload["scene_states"].get(state_name, 0)
            },
        }

    return {
        "accuracy": accuracy,
        "macro_f1": macro_f1,
        "balanced_accuracy": balanced_accuracy,
        "fp32_recall": recall_fp32,
        "fixed_recall": recall_fixed,
        "false_fp32_entry_rate_steady": false_fp32_entry_rate,
        "missed_fp32_entry_rate_transient": missed_fp32_entry_rate,
        "predicted_fp32_duty_ratio": predicted_fp32_duty,
        "teacher_fp32_duty_ratio": teacher_fp32_duty,
        "fp32_duty_ratio_error": fp32_duty_error,
        "mean_abs_case_fp32_ratio_error": mean(abs_case_errors),
        "p90_abs_case_fp32_ratio_error": percentile(abs_case_errors, 0.90),
        "confusion": counts,
        "per_case": case_summary,
    }


def fmt_optional(value) -> str:
    if value is None:
        return "n/a"
    return f"{value:.3f}"


def write_markdown(report: Dict[str, object], out_md: str):
    os.makedirs(os.path.dirname(out_md), exist_ok=True)
    lines = []
    lines.append("# AI-VFF Baseline 1 GRU Report")
    lines.append("")
    lines.append("## Runtime")
    lines.append("")
    lines.append(f"- Device: `{report['runtime']['device']}`")
    lines.append(f"- Torch version: `{report['runtime']['torch_version']}`")
    lines.append("")
    lines.append("## Dataset")
    lines.append("")
    lines.append(f"- Windows: `{report['dataset']['windows_path']}`")
    lines.append(f"- Splits: `{report['dataset']['splits_path']}`")
    lines.append(f"- Feature names: `{report['dataset']['feature_names']}`")
    lines.append(f"- Split alignment: `{report['dataset']['split_alignment']}`")
    lines.append("")
    lines.append("## Hyperparameters")
    lines.append("")
    for key, value in report["hyperparameters"].items():
        lines.append(f"- `{key}`: `{value}`")
    lines.append("")
    lines.append("## Split Counts")
    lines.append("")
    lines.append("| Split | Windows | Cases |")
    lines.append("| --- | ---: | ---: |")
    lines.append(f"| train | {report['split_counts']['train_windows']} | {report['split_counts']['train_cases']} |")
    lines.append(f"| val | {report['split_counts']['val_windows']} | {report['split_counts']['val_cases']} |")
    lines.append(f"| test | {report['split_counts']['test_windows']} | {report['split_counts']['test_cases']} |")
    lines.append("")
    lines.append("## Final Metrics")
    lines.append("")
    lines.append("| Split | Accuracy | Macro F1 | Balanced Acc | Pred FP32 Duty | Teacher FP32 Duty | Duty Error | Calm False FP32 | Transient Missed FP32 |")
    lines.append("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for split_name in ("train", "val", "test"):
        metrics = report["metrics"][split_name]
        lines.append(
            f"| {split_name} | {metrics['accuracy']:.3f} | {metrics['macro_f1']:.3f} | "
            f"{metrics['balanced_accuracy']:.3f} | {metrics['predicted_fp32_duty_ratio']:.3f} | "
            f"{metrics['teacher_fp32_duty_ratio']:.3f} | {metrics['fp32_duty_ratio_error']:.3f} | "
            f"{fmt_optional(metrics['false_fp32_entry_rate_steady'])} | "
            f"{fmt_optional(metrics['missed_fp32_entry_rate_transient'])} |"
        )
    lines.append("")
    lines.append("## Training History")
    lines.append("")
    lines.append("| Epoch | Train Loss | Val Loss | Val Macro F1 | Val Duty Error |")
    lines.append("| ---: | ---: | ---: | ---: | ---: |")
    for item in report["history"]:
        lines.append(
            f"| {item['epoch']} | {item['train_loss']:.4f} | {item['val_loss']:.4f} | "
            f"{item['val_macro_f1']:.4f} | {item['val_fp32_duty_ratio_error']:.4f} |"
        )
    lines.append("")
    lines.append("## Test Case Replay Summary")
    lines.append("")
    for case_name, payload in report["metrics"]["test"]["per_case"].items():
        lines.append(
            f"- `{case_name}`: predicted_fp32_ratio={payload['predicted_fp32_ratio']:.3f}, "
            f"teacher_fp32_ratio={payload['teacher_fp32_ratio']:.3f}, "
            f"abs_error={payload['absolute_fp32_ratio_error']:.3f}, states={payload['scene_states']}"
        )
    lines.append("")

    with open(out_md, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))


def parse_args():
    parser = argparse.ArgumentParser(description="Train the AI-VFF Baseline 1 clean-sequence GRU.")
    parser.add_argument("--windows", default=DEFAULT_WINDOWS_PATH, help="Path to windows_100.csv")
    parser.add_argument("--splits", default=DEFAULT_SPLITS_PATH, help="Path to splits.yaml")
    parser.add_argument("--epochs", type=int, default=8, help="Training epochs")
    parser.add_argument("--batch-size", type=int, default=64, help="Mini-batch size")
    parser.add_argument("--lr", type=float, default=1.0e-3, help="Learning rate")
    parser.add_argument("--hidden-dim", type=int, default=32, help="GRU hidden size")
    parser.add_argument("--num-layers", type=int, default=1, help="Number of GRU layers")
    parser.add_argument("--dropout", type=float, default=0.1, help="GRU dropout for multi-layer runs")
    parser.add_argument("--fp32-class-weight", type=float, default=1.5, help="Positive-class weight for FP32 samples")
    parser.add_argument("--seed", type=int, default=7, help="Random seed")
    parser.add_argument("--cpu", action="store_true", help="Force CPU even if CUDA is available")
    parser.add_argument(
        "--out-prefix",
        default=os.path.join(OUT_DIR, "baseline1_gru"),
        help="Output prefix without extension",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    set_seed(args.seed)

    examples, metadata = load_window_examples(
        windows_path=args.windows,
        splits_path=args.splits,
        include_sequences=True,
    )
    grouped = split_examples(examples)
    train_rows = grouped["train"]
    val_rows = grouped["val"]
    test_rows = grouped["test"]

    normalizer = compute_sequence_normalizer(train_rows)
    train_loader = make_loader(train_rows, normalizer, batch_size=args.batch_size, shuffle=True)
    val_loader = make_loader(val_rows, normalizer, batch_size=args.batch_size, shuffle=False)
    test_loader = make_loader(test_rows, normalizer, batch_size=args.batch_size, shuffle=False)

    device = torch.device("cpu" if args.cpu or not torch.cuda.is_available() else "cuda")
    model = PrecisionOnlyGRU(
        input_dim=len(CLEAN_SEQUENCE_FEATURES),
        hidden_dim=args.hidden_dim,
        num_layers=args.num_layers,
        dropout=args.dropout,
    ).to(device)

    class_weights = torch.tensor([1.0, args.fp32_class_weight], dtype=torch.float32, device=device)
    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr)

    history = []
    best_state = None
    best_val_macro_f1 = -1.0

    for epoch in range(1, args.epochs + 1):
        train_loss = train_epoch(model, train_loader, optimizer, device, class_weights)
        val_prediction = predict_epoch(model, val_loader, device)
        val_metrics = evaluate_predictions(
            val_prediction["probabilities"],
            val_prediction["labels"],
            val_prediction["rows_meta"],
        )

        history.append(
            {
                "epoch": epoch,
                "train_loss": train_loss,
                "val_loss": val_prediction["loss"],
                "val_macro_f1": val_metrics["macro_f1"],
                "val_accuracy": val_metrics["accuracy"],
                "val_fp32_duty_ratio_error": val_metrics["fp32_duty_ratio_error"],
            }
        )

        if val_metrics["macro_f1"] > best_val_macro_f1:
            best_val_macro_f1 = val_metrics["macro_f1"]
            best_state = {
                "model": model.state_dict(),
                "epoch": epoch,
            }

    if best_state is not None:
        model.load_state_dict(best_state["model"])

    train_prediction = predict_epoch(model, train_loader, device)
    val_prediction = predict_epoch(model, val_loader, device)
    test_prediction = predict_epoch(model, test_loader, device)

    report = {
        "runtime": {
            "device": str(device),
            "torch_version": torch.__version__,
            "cuda_available": torch.cuda.is_available(),
            "best_epoch": best_state["epoch"] if best_state is not None else None,
        },
        "dataset": {
            "windows_path": args.windows,
            "splits_path": args.splits,
            "feature_names": list(CLEAN_SEQUENCE_FEATURES),
            "split_alignment": metadata["split_alignment"],
        },
        "hyperparameters": {
            "epochs": args.epochs,
            "batch_size": args.batch_size,
            "learning_rate": args.lr,
            "hidden_dim": args.hidden_dim,
            "num_layers": args.num_layers,
            "dropout": args.dropout,
            "fp32_class_weight": args.fp32_class_weight,
            "seed": args.seed,
        },
        "split_counts": {
            "train_windows": len(train_rows),
            "val_windows": len(val_rows),
            "test_windows": len(test_rows),
            "train_cases": count_cases(train_rows),
            "val_cases": count_cases(val_rows),
            "test_cases": count_cases(test_rows),
        },
        "normalizer": normalizer,
        "history": history,
        "metrics": {
            "train": evaluate_predictions(
                train_prediction["probabilities"],
                train_prediction["labels"],
                train_prediction["rows_meta"],
            ),
            "val": evaluate_predictions(
                val_prediction["probabilities"],
                val_prediction["labels"],
                val_prediction["rows_meta"],
            ),
            "test": evaluate_predictions(
                test_prediction["probabilities"],
                test_prediction["labels"],
                test_prediction["rows_meta"],
            ),
        },
    }

    out_json = args.out_prefix + ".json"
    out_md = args.out_prefix + ".md"
    os.makedirs(os.path.dirname(out_json), exist_ok=True)
    with open(out_json, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
        handle.write("\n")
    write_markdown(report, out_md)

    print(f"baseline1_json={out_json}")
    print(f"baseline1_md={out_md}")
    print(f"device={report['runtime']['device']}")
    print(f"best_epoch={report['runtime']['best_epoch']}")
    print(f"test_accuracy={report['metrics']['test']['accuracy']:.4f}")
    print(f"test_macro_f1={report['metrics']['test']['macro_f1']:.4f}")
    print(f"test_fp32_duty_error={report['metrics']['test']['fp32_duty_ratio_error']:.4f}")


if __name__ == "__main__":
    main()
