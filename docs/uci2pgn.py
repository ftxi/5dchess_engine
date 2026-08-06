#!/usr/bin/env python3
"""Convert a 5DUCI ``position ... moves ...`` command to 5DPGN.

The converter intentionally writes full 5DPGN notation.  This avoids needing
to reconstruct board state merely to shorten moves or identify captures.

Examples:
    python3 uci_to_5dpgn.py 'position startpos moves (0T1)e2e3 submit'
    printf '%s\n' 'position size 6x6 odd fen [6/6/6/6/6/6:0:1:w] moves submit' \\
        | python3 uci_to_5dpgn.py
"""

from __future__ import annotations

import argparse
import re
import sys


LAN_RE = re.compile(
    r"^\((-?\d+)T(\d+)\)([a-h])([1-8])"
    r"(?:\((-?\d+)T(\d+)\))?([a-h])([1-8])([A-Za-z])?$"
)
FEN_BLOCK_RE = re.compile(r"\[[^:\]\s]+:-?\d+:\d+:[wb]\]")
SIZE_RE = re.compile(r"^[1-8]x[1-8]$")


class PositionSyntaxError(ValueError):
    """Raised when input is not a single valid 5DUCI position command."""


def lan_to_pgn(move: str) -> str:
    """Convert one 5DUCI LAN token to unambiguous 5DPGN notation."""

    match = LAN_RE.fullmatch(move)
    if not match:
        raise PositionSyntaxError(f"invalid 5DUCI move: {move!r}")

    line0, time0, file0, rank0, line1, time1, file1, rank1, promotion = match.groups()
    source = f"({line0}T{time0}){file0}{rank0}"
    suffix = f"{file1}{rank1}" + (f"={promotion.upper()}" if promotion else "")
    if line1 is None:
        return source + suffix

    # The jump indicator is deliberately omitted.  The 5DPGN grammar permits
    # it when an absolute destination board is present, and omitting it lets a
    # PGN reader derive whether the jump is branching from the game state.
    return source + f"({line1}T{time1})" + suffix


def split_position(command: str) -> tuple[str, list[str]]:
    """Return the position base and move tokens from one position command."""

    words = command.strip().split()
    if not words or words[0] != "position":
        raise PositionSyntaxError("expected a command beginning with 'position'")
    try:
        moves_at = words.index("moves", 1)
    except ValueError:
        return " ".join(words[1:]), []
    return " ".join(words[1:moves_at]), words[moves_at + 1 :]


def pgn_start(position: str) -> list[str]:
    """Create 5DPGN headers and FEN blocks for a 5DUCI position base."""

    if position == "startpos":
        return ['[Board "Standard - Turn Zero"]']

    words = position.split()
    size = "8x8"
    parity = "odd"
    if words[:1] == ["size"]:
        if len(words) < 2 or not SIZE_RE.fullmatch(words[1]):
            raise PositionSyntaxError("expected board size in the form <m>x<n>")
        size = words[1]
        words = words[2:]
    if words[:1] and words[0] in {"odd", "even"}:
        parity = words[0]
        words = words[1:]
    if not words or words[0] != "fen":
        raise PositionSyntaxError("expected 'startpos' or '[size <m>x<n>] [odd|even] fen <5dfen>'")

    fen = " ".join(words[1:])
    blocks = FEN_BLOCK_RE.findall(fen)
    if not blocks or " ".join(blocks) != fen:
        raise PositionSyntaxError("FEN must be one or more [board:timeline:turn:color] blocks")
    return [f'[Size "{size}"]', f'[Board "Custom - {parity.title()}"]', *blocks]


def convert(command: str) -> str:
    """Convert one complete 5DUCI position command into 5DPGN text."""

    position, moves = split_position(command)
    lines = pgn_start(position)
    actions: list[list[str]] = []
    action: list[str] = []
    for token in moves:
        if token == "submit":
            if not action:
                raise PositionSyntaxError("'submit' cannot close an empty action")
            actions.append(action)
            action = []
        else:
            action.append(lan_to_pgn(token))
    if action:
        raise PositionSyntaxError("move history ends without 'submit'")

    if actions:
        lines.append("")
        for index, moves_in_action in enumerate(actions):
            serial = "1w." if index == 0 else "/"
            lines.append(f"{serial} {' '.join(moves_in_action)}")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", nargs="*", help="position command (reads standard input when omitted)")
    args = parser.parse_args()
    command = " ".join(args.command) if args.command else sys.stdin.read().strip()
    try:
        sys.stdout.write(convert(command))
    except PositionSyntaxError as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
