#!/usr/bin/env python3
import argparse
import json
import math
import os
from collections import Counter, defaultdict
from typing import Dict, List, Sequence, Tuple

from ai_vff_baseline_data import (
    BASELINE0_FEATURES,
    DEFAULT_SPLITS_PATH,
    DEFAULT_WINDOWS_PATH,
    INT_TO_LABEL,
    SCENE_STATE_ORDER,
    count_cases,
    load_window_examples,
    split_examples,
)


ROOT = r"E:\ASSCC"
OUT_DIR = os.path.join(ROOT, "ai_vff_dataset", "baseline0_outputs")


def dot(lhs: Sequence[float], rhs: Sequence[float]) -> float:
    return sum(left * right for left, right in zip(lhs, rhs))


def sigmoid(value: float) -> float:
    if value >= 0.0:
        exp_term = math.exp(-value)
        return 1.0 / (1.0 + exp_term)
    exp_term = math.exp(value)
    return exp_term / (1.0 + exp_term)


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


def compute_normalizer(train_rows) -> Dict[str, List[float]]:
    feature_count = len(BASELINE0_FEATURES)
    means = []
    stds = []

    for index in range(feature_count):
        values = [row.baseline0_features[index] for row in train_rows]
        current_mean = mean(values)
        variance = mean([(value - current_mean) ** 2 for value in values])
        current_std = math.sqrt(variance)
        means.append(current_mean)
        stds.append(current_std if current_std > 1.0e-8 else 1.0)

    return {
        "mean": means,
        "std": stds,
    }


def normalize_features(rows, normalizer) -> Tuple[List[List[float]], List[int]]:
    norm_mean = normalizer["mean"]
    norm_std = normalizer["std"]
    features = []
    labels = []

    for row in rows:
        feature_vector = []
        for index, value in enumerate(row.baseline0_features):
            normalized = (value - norm_mean[index]) / norm_std[index]
            feature_vector.append(normalized)
        features.append(feature_vector)
        labels.append(row.teacher_target_precision)

    return features, labels


def train_logistic_regression(
    features: List[List[float]],
    labels: List[int],
    learning_rate: float,
    epochs: int,
    l2_weight: float,
    class_weight_fp32: float,
) -> Dict[str, object]:
    if not features:
        raise ValueError("no training features provided")

    feature_count = len(features[0])
    weights = [0.0] * feature_count
    bias = 0.0
    history = []

    for epoch in range(epochs):
        grad_weights = [0.0] * feature_count
        grad_bias = 0.0
        weighted_loss = 0.0
        weight_sum = 0.0

        for row_features, label in zip(features, labels):
            score = dot(weights, row_features) + bias
            prob = sigmoid(score)
            sample_weight = class_weight_fp32 if label == 1 else 1.0
            error = prob - label
            clipped_prob = min(max(prob, 1.0e-9), 1.0 - 1.0e-9)
            weighted_loss += sample_weight * (
                -(label * math.log(clipped_prob) + (1 - label) * math.log(1.0 - clipped_prob))
            )
            weight_sum += sample_weight
            for index, value in enumerate(row_features):
                grad_weights[index] += sample_weight * error * value
            grad_bias += sample_weight * error

        inverse_weight_sum = 1.0 / weight_sum if weight_sum else 1.0
        for index in range(feature_count):
            grad_weights[index] = grad_weights[index] * inverse_weight_sum + l2_weight * weights[index]
            weights[index] -= learning_rate * grad_weights[index]
        grad_bias *= inverse_weight_sum
        bias -= learning_rate * grad_bias

        epoch_loss = weighted_loss * inverse_weight_sum
        if l2_weight:
            epoch_loss += 0.5 * l2_weight * sum(weight * weight for weight in weights)
        history.append(epoch_loss)

    return {
        "weights": weights,
        "bias": bias,
        "loss_history": history,
    }


def predict_probabilities(model: Dict[str, object], features: List[List[float]]) -> List[float]:
    weights = model["weights"]
    bias = model["bias"]
    return [sigmoid(dot(weights, feature_row) + bias) for feature_row in features]


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


def evaluate_rows(rows, probabilities: List[float], threshold: float = 0.5) -> Dict[str, object]:
    labels = [row.teacher_target_precision for row in rows]
    predictions = [1 if prob >= threshold else 0 for prob in probabilities]
    counts = confusion_counts(labels, predictions)
    tp = counts["tp"]
    tn = counts["tn"]
    fp = counts["fp"]
    fn = counts["fn"]

    total = len(rows)
    accuracy = safe_div(tp + tn, total)
    recall_fp32 = safe_div(tp, tp + fn)
    recall_fixed = safe_div(tn, tn + fp)
    balanced_accuracy = 0.5 * (recall_fp32 + recall_fixed)
    macro_f1 = 0.5 * (
        f1_for_positive(0, labels, predictions) +
        f1_for_positive(1, labels, predictions)
    )

    steady_rows = [index for index, row in enumerate(rows) if row.teacher_scene_state == "STEADY"]
    transient_rows = [
        index
        for index, row in enumerate(rows)
        if row.teacher_scene_state in ("TRANSIENT", "HIGH_RISK")
    ]

    false_fp32_entry_rate = (
        sum(1 for index in steady_rows if predictions[index] == 1) / len(steady_rows)
        if steady_rows
        else None
    )
    missed_fp32_entry_rate = (
        sum(1 for index in transient_rows if predictions[index] == 0) / len(transient_rows)
        if transient_rows
        else None
    )

    predicted_fp32_duty = mean(probabilities)
    teacher_fp32_duty = mean([row.selected_float_ratio_100 for row in rows])
    fp32_duty_error = predicted_fp32_duty - teacher_fp32_duty

    per_case = defaultdict(lambda: {"probs": [], "teacher": [], "scene_states": Counter(), "rows": 0})
    for row, prob in zip(rows, probabilities):
        payload = per_case[row.case_name]
        payload["probs"].append(prob)
        payload["teacher"].append(row.selected_float_ratio_100)
        payload["scene_states"][row.teacher_scene_state] += 1
        payload["rows"] += 1

    per_case_summary = {}
    abs_case_errors = []
    for case_name, payload in sorted(per_case.items()):
        predicted_ratio = mean(payload["probs"])
        teacher_ratio = mean(payload["teacher"])
        absolute_error = abs(predicted_ratio - teacher_ratio)
        abs_case_errors.append(absolute_error)
        per_case_summary[case_name] = {
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
        "window_count": total,
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
        "per_case": per_case_summary,
    }


def build_feature_importance(model: Dict[str, object], normalizer: Dict[str, List[float]]) -> List[Dict[str, float]]:
    weights = model["weights"]
    scaled = []
    for index, feature_name in enumerate(BASELINE0_FEATURES):
        effective_weight = weights[index] / normalizer["std"][index]
        scaled.append(
            {
                "feature": feature_name,
                "weight_normalized": weights[index],
                "weight_original_scale": effective_weight,
                "abs_weight_normalized": abs(weights[index]),
            }
        )
    scaled.sort(key=lambda item: item["abs_weight_normalized"], reverse=True)
    return scaled


def build_report(
    windows_path: str,
    splits_path: str,
    learning_rate: float,
    epochs: int,
    l2_weight: float,
    class_weight_fp32: float,
) -> Dict[str, object]:
    examples, metadata = load_window_examples(
        windows_path=windows_path,
        splits_path=splits_path,
        include_sequences=False,
    )
    grouped = split_examples(examples)

    train_rows = grouped["train"]
    val_rows = grouped["val"]
    test_rows = grouped["test"]

    if not train_rows or not val_rows or not test_rows:
        raise ValueError("train/val/test splits must all be non-empty")

    normalizer = compute_normalizer(train_rows)
    train_features, train_labels = normalize_features(train_rows, normalizer)
    val_features, _ = normalize_features(val_rows, normalizer)
    test_features, _ = normalize_features(test_rows, normalizer)

    model = train_logistic_regression(
        features=train_features,
        labels=train_labels,
        learning_rate=learning_rate,
        epochs=epochs,
        l2_weight=l2_weight,
        class_weight_fp32=class_weight_fp32,
    )

    train_probabilities = predict_probabilities(model, train_features)
    val_probabilities = predict_probabilities(model, val_features)
    test_probabilities = predict_probabilities(model, test_features)

    report = {
        "dataset": {
            "windows_path": windows_path,
            "splits_path": splits_path,
            "feature_names": list(BASELINE0_FEATURES),
            "split_alignment": metadata["split_alignment"],
        },
        "hyperparameters": {
            "learning_rate": learning_rate,
            "epochs": epochs,
            "l2_weight": l2_weight,
            "class_weight_fp32": class_weight_fp32,
        },
        "split_counts": {
            "train_windows": len(train_rows),
            "val_windows": len(val_rows),
            "test_windows": len(test_rows),
            "train_cases": count_cases(train_rows),
            "val_cases": count_cases(val_rows),
            "test_cases": count_cases(test_rows),
        },
        "loss_history": model["loss_history"],
        "feature_importance": build_feature_importance(model, normalizer),
        "metrics": {
            "train": evaluate_rows(train_rows, train_probabilities),
            "val": evaluate_rows(val_rows, val_probabilities),
            "test": evaluate_rows(test_rows, test_probabilities),
        },
    }
    return report


def write_report_json(report: Dict[str, object], out_json: str):
    os.makedirs(os.path.dirname(out_json), exist_ok=True)
    with open(out_json, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
        handle.write("\n")


def fmt_optional(value) -> str:
    if value is None:
        return "n/a"
    return f"{value:.3f}"


def write_report_markdown(report: Dict[str, object], out_md: str):
    os.makedirs(os.path.dirname(out_md), exist_ok=True)
    lines = []
    lines.append("# AI-VFF Baseline 0 Starter Report")
    lines.append("")
    lines.append("## Dataset")
    lines.append("")
    lines.append(f"- Windows: `{report['dataset']['windows_path']}`")
    lines.append(f"- Splits: `{report['dataset']['splits_path']}`")
    lines.append(f"- Feature count: `{len(report['dataset']['feature_names'])}`")
    lines.append(f"- Features: `{report['dataset']['feature_names']}`")
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
    lines.append("## Metrics")
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
    lines.append("## Top Features")
    lines.append("")
    for item in report["feature_importance"][:8]:
        lines.append(
            f"- `{item['feature']}`: normalized_weight={item['weight_normalized']:.4f}, "
            f"original_scale_weight={item['weight_original_scale']:.4f}"
        )
    lines.append("")
    lines.append("## Test Case Replay Summary")
    lines.append("")
    test_cases = report["metrics"]["test"]["per_case"]
    for case_name, payload in test_cases.items():
        lines.append(
            f"- `{case_name}`: predicted_fp32_ratio={payload['predicted_fp32_ratio']:.3f}, "
            f"teacher_fp32_ratio={payload['teacher_fp32_ratio']:.3f}, "
            f"abs_error={payload['absolute_fp32_ratio_error']:.3f}, states={payload['scene_states']}"
        )
    lines.append("")

    with open(out_md, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))


def parse_args():
    parser = argparse.ArgumentParser(description="Train the AI-VFF Baseline 0 summary-feature starter model.")
    parser.add_argument("--windows", default=DEFAULT_WINDOWS_PATH, help="Path to windows_100.csv")
    parser.add_argument("--splits", default=DEFAULT_SPLITS_PATH, help="Path to splits.yaml")
    parser.add_argument("--epochs", type=int, default=250, help="Number of training epochs")
    parser.add_argument("--lr", type=float, default=0.05, help="Learning rate")
    parser.add_argument("--l2", type=float, default=1.0e-4, help="L2 regularization weight")
    parser.add_argument("--fp32-class-weight", type=float, default=1.5, help="Positive-class weight for FP32 samples")
    parser.add_argument(
        "--out-prefix",
        default=os.path.join(OUT_DIR, "baseline0_starter"),
        help="Output prefix without extension",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    report = build_report(
        windows_path=args.windows,
        splits_path=args.splits,
        learning_rate=args.lr,
        epochs=args.epochs,
        l2_weight=args.l2,
        class_weight_fp32=args.fp32_class_weight,
    )

    out_json = args.out_prefix + ".json"
    out_md = args.out_prefix + ".md"
    write_report_json(report, out_json)
    write_report_markdown(report, out_md)

    print(f"baseline0_json={out_json}")
    print(f"baseline0_md={out_md}")
    print(f"test_accuracy={report['metrics']['test']['accuracy']:.4f}")
    print(f"test_macro_f1={report['metrics']['test']['macro_f1']:.4f}")
    print(f"test_fp32_duty_error={report['metrics']['test']['fp32_duty_ratio_error']:.4f}")


if __name__ == "__main__":
    main()
