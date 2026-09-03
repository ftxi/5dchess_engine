#!/usr/bin/env python3
"""Plot measured 5D-chess branching observations in three dimensions.

For an interactive IPython plot::

    %matplotlib qt
    from tools.plot_branching import plot_branching
    figure = plot_branching(panel="linear")
    figure
"""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.colors import Normalize
from matplotlib.figure import Figure
from matplotlib.ticker import ScalarFormatter


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=Path("logs/branching-human.csv"))
    parser.add_argument("--output", type=Path, help="optionally save a static image")
    parser.add_argument(
        "--show",
        action="store_true",
        help="open an interactive Matplotlib window",
    )
    parser.add_argument(
        "--panel",
        choices=("both", "linear", "log"),
        default="both",
        help="show both panels, or only one full-size rotatable 3D panel",
    )
    return parser.parse_args()


def plot_branching(
    input_path: str | Path = "logs/branching-human.csv",
    panel: str = "both",
) -> Figure:
    """Build and return a compact, rotatable Matplotlib 3D figure."""
    if panel not in {"both", "linear", "log"}:
        raise ValueError("panel must be 'both', 'linear', or 'log'")
    with Path(input_path).open(newline="") as source:
        rows = list(csv.DictReader(source))

    submitted = [int(row["actions_played"]) for row in rows]
    timelines = [int(row["timelines_total"]) for row in rows]
    actions = [int(row["legal_actions"]) for row in rows]
    log_actions = [math.log10(1 + value) for value in actions]
    capped = [row["capped"] == "true" for row in rows]
    capped_limits = {
        int(row["count_limit"]) for row in rows if row["capped"] == "true"
    }
    if len(capped_limits) > 1:
        raise ValueError("capped input rows use multiple count limits")
    limit = capped_limits.pop() if capped_limits else max(
        int(row["count_limit"]) for row in rows
    )
    capped_label = f"{limit - 1:,}+ (capped)"
    exact_indices = [index for index, value in enumerate(capped) if not value]
    capped_indices = [index for index, value in enumerate(capped) if value]

    single_panel = panel != "both"
    figure = plt.figure(figsize=(8.5, 6.2) if single_panel else (11, 6.2), dpi=150)
    if hasattr(figure.canvas.manager, "window"):
        window = figure.canvas.manager.window
        if hasattr(window, "resize"):
            window.resize(1000 if single_panel else 1300, 760)
    linear_axis = None
    log_axis = None
    if panel in {"both", "linear"}:
        linear_axis = figure.add_subplot(1, 1 if single_panel else 2, 1, projection="3d")
    if panel in {"both", "log"}:
        log_axis = figure.add_subplot(
            1, 1 if single_panel else 2, 1 if panel == "log" else 2, projection="3d"
        )
    normalization = Normalize(vmin=min(log_actions), vmax=max(log_actions))

    def values(items: list, indices: list[int]) -> list:
        return [items[index] for index in indices]

    scatter = None
    if linear_axis is not None:
        scatter = linear_axis.scatter(
            values(submitted, exact_indices), values(timelines, exact_indices),
            values(actions, exact_indices), c=values(log_actions, exact_indices),
            cmap="viridis", norm=normalization, s=13, alpha=0.55,
            linewidths=0, depthshade=False,
        )
        linear_axis.scatter(
            values(submitted, capped_indices), values(timelines, capped_indices),
            values(actions, capped_indices), c="#d62728", marker="^", s=27,
            alpha=0.9, label=capped_label, depthshade=False,
        )
    if log_axis is not None:
        scatter = log_axis.scatter(
            values(submitted, exact_indices), values(timelines, exact_indices),
            values(log_actions, exact_indices), c=values(log_actions, exact_indices),
            cmap="viridis", norm=normalization, s=13, alpha=0.55,
            linewidths=0, depthshade=False,
        )
        log_axis.scatter(
            values(submitted, capped_indices), values(timelines, capped_indices),
            values(log_actions, capped_indices), c="#d62728", marker="^", s=27,
            alpha=0.9, label=capped_label, depthshade=False,
        )

    for axis in (linear_axis, log_axis):
        if axis is None:
            continue
        axis.set_xlabel("Submitted actions", labelpad=9)
        axis.set_ylabel("Total timelines", labelpad=9)
        axis.set_yticks(sorted(set(timelines)))
        axis.view_init(elev=24, azim=-58)
        axis.grid(True, alpha=0.25)
        axis.legend(loc="upper left", frameon=False, fontsize=8)

    if linear_axis is not None:
        linear_axis.set_title("Linear action-count scale", pad=14)
        linear_axis.set_zlabel("Valid actions", labelpad=9)
        linear_axis.set_zlim(0, limit * 1.05)
        formatter = ScalarFormatter(useMathText=True)
        formatter.set_powerlimits((0, 0))
        linear_axis.zaxis.set_major_formatter(formatter)
    if log_axis is not None:
        log_axis.set_title("Log action-count scale", pad=14)
        log_axis.set_zlabel("Valid actions (log₁₀ scale)", labelpad=9)
        maximum_log_tick = math.floor(math.log10(limit))
        log_ticks = list(range(maximum_log_tick + 1))
        log_axis.set_zticks(log_ticks)
        log_axis.set_zticklabels([f"{10**tick - 1:,}" for tick in log_ticks])

    if panel == "both":
        figure.suptitle(
            f"Human 5D Chess: Valid Action Counts ({len(rows):,} sampled positions)",
            fontsize=14, y=0.97,
        )
    colorbar_axis = figure.add_axes([0.925 if not single_panel else 0.90, 0.25, 0.014, 0.50])
    colorbar = figure.colorbar(scatter, cax=colorbar_axis)
    colorbar.set_label("log₁₀(1 + valid actions)")
    figure.subplots_adjust(
        left=0.02, right=0.86 if single_panel else 0.87,
        bottom=0.05, top=0.90, wspace=0.01,
    )
    return figure


def main() -> int:
    args = parse_args()
    if not args.output and not args.show:
        raise SystemExit("specify --show, --output, or both")

    figure = plot_branching(args.input, panel=args.panel)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        figure.savefig(args.output, bbox_inches="tight", facecolor="white")
    if args.show:
        plt.show()
    else:
        plt.close(figure)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
