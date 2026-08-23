#!/usr/bin/env python3
"""Fit and report a two-variable branching heuristic from experiment CSV data."""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from pathlib import Path


def percentile(values: list[float], probability: float) -> float:
    ordered = sorted(values)
    position = (len(ordered) - 1) * probability
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def solve(matrix: list[list[float]], vector: list[float]) -> list[float]:
    size = len(vector)
    augmented = [matrix[row][:] + [vector[row]] for row in range(size)]
    for column in range(size):
        pivot = max(range(column, size), key=lambda row: abs(augmented[row][column]))
        augmented[column], augmented[pivot] = augmented[pivot], augmented[column]
        divisor = augmented[column][column]
        if abs(divisor) < 1e-12:
            raise ValueError("singular regression matrix")
        for index in range(column, size + 1):
            augmented[column][index] /= divisor
        for row in range(size):
            if row == column:
                continue
            multiplier = augmented[row][column]
            for index in range(column, size + 1):
                augmented[row][index] -= multiplier * augmented[column][index]
    return [augmented[row][-1] for row in range(size)]


def features(row: dict[str, str]) -> list[float]:
    timelines = int(row["timelines_total"])
    age = math.log1p(int(row["actions_played"]))
    return [1.0, timelines, timelines**2, age, age**2, timelines * age]


def fit(rows: list[dict[str, str]]) -> list[float]:
    inputs = [features(row) for row in rows]
    outputs = [math.log1p(int(row["legal_actions"])) for row in rows]
    width = len(inputs[0])
    matrix = [
        [sum(item[left] * item[right] for item in inputs) for right in range(width)]
        for left in range(width)
    ]
    vector = [
        sum(item[column] * output for item, output in zip(inputs, outputs))
        for column in range(width)
    ]
    return solve(matrix, vector)


def prediction(coefficients: list[float], row: dict[str, str]) -> float:
    return sum(
        coefficient * value
        for coefficient, value in zip(coefficients, features(row))
    )


def metrics(
    rows: list[dict[str, str]], coefficients: list[float]
) -> dict[str, float]:
    residuals = [
        math.log1p(int(row["legal_actions"])) - prediction(coefficients, row)
        for row in rows
    ]
    return {
        "rows": len(rows),
        "log_rmse": math.sqrt(sum(value**2 for value in residuals) / len(residuals)),
        "median_multiplicative_error": math.exp(
            statistics.median(abs(value) for value in residuals)
        ),
        "within_factor_2": sum(abs(value) <= math.log(2) for value in residuals)
        / len(residuals),
    }


def count_distribution(rows: list[dict[str, str]]) -> list[dict]:
    result = []
    for timelines in sorted({int(row["timelines_total"]) for row in rows}):
        values = [
            int(row["legal_actions"])
            for row in rows
            if int(row["timelines_total"]) == timelines
        ]
        result.append(
            {
                "timelines": timelines,
                "positions": len(values),
                "capped_rate": sum(
                    row["capped"] == "true"
                    for row in rows
                    if int(row["timelines_total"]) == timelines
                ) / len(values),
                "p10": round(percentile(values, 0.10)),
                "median": round(percentile(values, 0.50)),
                "p90": round(percentile(values, 0.90)),
            }
        )
    return result


def residual_distribution(
    rows: list[dict[str, str]], coefficients: list[float]
) -> list[dict]:
    result = []
    for timelines in range(1, 7):
        residuals = [
            math.log1p(int(row["legal_actions"])) - prediction(coefficients, row)
            for row in rows
            if int(row["timelines_total"]) == timelines
        ]
        if residuals:
            result.append(
                {
                    "timelines": timelines,
                    "positions": len(residuals),
                    "p10": percentile(residuals, 0.10),
                    "median": percentile(residuals, 0.50),
                    "p90": percentile(residuals, 0.90),
                }
            )
    return result


def markdown_table(headers: list[str], rows: list[list[str]]) -> str:
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    lines.extend("| " + " | ".join(row) + " |" for row in rows)
    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=Path("logs/branching-human.csv"))
    parser.add_argument(
        "--output", type=Path, default=Path("logs/branching-human-report.md")
    )
    parser.add_argument(
        "--json-output", type=Path, default=Path("logs/branching-human-model.json")
    )
    parser.add_argument("--stride", type=int, default=4)
    parser.add_argument("--max-timelines", type=int, default=6)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    with args.input.open(newline="") as source:
        rows = list(csv.DictReader(source))
    required = {
        "game_index",
        "actions_played",
        "timelines_total",
        "legal_actions",
        "count_limit",
        "count_status",
    }
    missing = required - set(rows[0])
    if missing:
        raise SystemExit(f"missing columns: {', '.join(sorted(missing))}")
    if any(row["count_status"] != "ok" for row in rows):
        raise SystemExit("input contains failed or timed-out count rows")
    capped_limits = {
        int(row["count_limit"]) for row in rows if row["capped"] == "true"
    }
    if len(capped_limits) > 1:
        raise SystemExit("capped input rows use multiple count limits")
    limit = capped_limits.pop() if capped_limits else max(
        int(row["count_limit"]) for row in rows
    )

    # Extra samples bracket timeline changes. The regular stride grid avoids
    # letting those deliberately enriched transition positions bias the fit.
    regular = [row for row in rows if int(row["actions_played"]) % args.stride == 0]
    model_rows = [
        row
        for row in regular
        if int(row["timelines_total"]) <= args.max_timelines
    ]
    train = [row for row in model_rows if int(row["game_index"]) % 5 != 0]
    test = [row for row in model_rows if int(row["game_index"]) % 5 == 0]
    validation_coefficients = fit(train)
    coefficients = fit(model_rows)
    validation = metrics(test, validation_coefficients)
    distributions = count_distribution(regular)
    residuals = residual_distribution(model_rows, coefficients)

    summary = {
        "source": str(args.input),
        "sampled_rows": len(rows),
        "regular_grid_rows": len(regular),
        "model_rows": len(model_rows),
        "games": len({row["game_index"] for row in rows}),
        "count_limit": limit,
        "capped_rows": sum(row["capped"] == "true" for row in rows),
        "response": "ln(1 + legal_actions), top-coded at count_limit",
        "features": ["1", "L", "L^2", "a", "a^2", "L*a"],
        "age_definition": "a = ln(1 + actions_played)",
        "coefficients": coefficients,
        "validation": validation,
        "count_distribution": distributions,
        "residual_distribution": residuals,
    }
    args.json_output.write_text(json.dumps(summary, indent=2) + "\n")

    beta = coefficients
    formula = (
        f"μ(L,A) = {beta[0]:.4f} + {beta[1]:.4f}L "
        f"{beta[2]:+.4f}L² + {beta[3]:.4f}a "
        f"{beta[4]:+.4f}a² {beta[5]:+.4f}La, where a = ln(1+A)."
    )
    distribution_table = markdown_table(
        ["Timelines", "Positions", "Capped", "P10", "Median", "P90"],
        [
            [
                str(item["timelines"]),
                str(item["positions"]),
                f'{item["capped_rate"]:.1%}',
                str(item["p10"]),
                str(item["median"]),
                str(item["p90"]),
            ]
            for item in distributions
        ],
    )
    residual_table = markdown_table(
        ["Timelines", "Positions", "Residual P10", "Median", "P90"],
        [
            [
                str(item["timelines"]),
                str(item["positions"]),
                f'{item["p10"]:.3f}',
                f'{item["median"]:.3f}',
                f'{item["p90"]:.3f}',
            ]
            for item in residuals
        ],
    )
    report = f"""# Human-game branching-factor pilot

## Dataset

- {len(rows):,} sampled positions from {summary['games']} human games.
- {len(regular):,} positions lie on the unbiased every-{args.stride}-actions grid.
- Counts are capped at {limit:,}; {summary['capped_rows']} of all sampled positions hit the cap.
- The fitted domain is 1–{args.max_timelines} timelines. Higher counts have too little data.

## Empirical distribution

The table uses the regular sampling grid. A value of {limit:,} means {limit - 1:,}+ legal actions.

{distribution_table}

## Heuristic

Let `L` be total timelines, `A` be submitted actions, and `N` be legal actions. The fitted center is:

`{formula}`

Then `ln(1+N) = μ(L,A) + ε_L`. Use the following empirical residual quantiles to obtain a distribution: `exp(μ + residual_quantile) - 1`.

{residual_table}

This is a descriptive heuristic, not a calibrated probability model. On a game-held-out 20% split, the median multiplicative error was {validation['median_multiplicative_error']:.2f}×, {validation['within_factor_2']:.1%} of positions were within a factor of two, and log-RMSE was {validation['log_rmse']:.3f}.

## Limitations

- Positions with {limit:,} actions are top-coded, so upper-tail quantiles are lower bounds.
- Timeline counts above six are too sparse for the formula.
- Total timelines and game age omit tactically important state. HC volume and playable/mandatory timeline counts should be evaluated as additional predictors.
"""
    args.output.write_text(report)
    print(json.dumps({"report": str(args.output), "model": str(args.json_output)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
