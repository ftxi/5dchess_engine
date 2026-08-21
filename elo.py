#!/usr/bin/env python3
"""Persistent, engine-independent Elo registry and match scheduler.

This module deliberately has no dependency on autoplay or the 5D chess
bindings.  It can be used as a command-line rating ledger, imported by an
automatic runner, or fed results from games played on another machine.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import random
import sqlite3
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, Sequence


DEFAULT_DATABASE = Path("logs/elo-matchmaker.sqlite3")
DEFAULT_RATING = 1500.0
DEFAULT_K_FACTOR = 32.0
RESULTS = {"white", "black", "draw", "void"}
FOCUSED_STRATEGIES = {"adaptive", "coverage"}


class EloError(RuntimeError):
    """A registry, scheduling, or rating operation could not be completed."""


@dataclass(frozen=True)
class EngineRecord:
    id: str
    name: str
    command: str
    rating: float
    initial_rating: float
    enabled: bool
    training: bool
    stateful: bool
    max_parallel: int
    parent_id: str | None


@dataclass(frozen=True)
class Pairing:
    match_id: int | None
    batch_id: int | None
    white_id: str
    black_id: str
    white_name: str
    black_name: str
    white_command: str
    black_command: str
    white_rating: float
    black_rating: float
    ordinal: int | None = None


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def connect(path: Path | str = DEFAULT_DATABASE) -> sqlite3.Connection:
    """Open and initialize an Elo database."""

    if str(path) != ":memory:":
        Path(path).expanduser().parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(path, timeout=30.0)
    connection.row_factory = sqlite3.Row
    connection.execute("PRAGMA foreign_keys = ON")
    connection.execute("PRAGMA journal_mode = WAL")
    connection.execute("PRAGMA busy_timeout = 30000")
    initialize_schema(connection)
    return connection


def initialize_schema(connection: sqlite3.Connection) -> None:
    connection.executescript(
        """
        CREATE TABLE IF NOT EXISTS engines (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            command TEXT NOT NULL,
            rating REAL NOT NULL,
            initial_rating REAL NOT NULL,
            enabled INTEGER NOT NULL DEFAULT 1 CHECK (enabled IN (0, 1)),
            training INTEGER NOT NULL DEFAULT 0 CHECK (training IN (0, 1)),
            stateful INTEGER NOT NULL DEFAULT 0 CHECK (stateful IN (0, 1)),
            max_parallel INTEGER NOT NULL DEFAULT 0 CHECK (max_parallel >= 0),
            parent_id TEXT REFERENCES engines(id),
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS artifacts (
            engine_id TEXT NOT NULL REFERENCES engines(id),
            path TEXT NOT NULL,
            sha256 TEXT NOT NULL,
            PRIMARY KEY (engine_id, path)
        );

        CREATE TABLE IF NOT EXISTS batches (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            created_at TEXT NOT NULL,
            finalized_at TEXT,
            status TEXT NOT NULL DEFAULT 'open'
                CHECK (status IN ('open', 'finalized', 'cancelled')),
            rated INTEGER NOT NULL CHECK (rated IN (0, 1)),
            k_factor REAL NOT NULL,
            seed INTEGER NOT NULL,
            focus_engine TEXT REFERENCES engines(id),
            strategy TEXT,
            note TEXT NOT NULL DEFAULT ''
        );

        CREATE UNIQUE INDEX IF NOT EXISTS one_open_rated_batch
            ON batches(rated) WHERE rated = 1 AND status = 'open';

        CREATE TABLE IF NOT EXISTS matches (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            batch_id INTEGER NOT NULL REFERENCES batches(id),
            ordinal INTEGER NOT NULL,
            white_id TEXT NOT NULL REFERENCES engines(id),
            black_id TEXT NOT NULL REFERENCES engines(id),
            white_command TEXT NOT NULL,
            black_command TEXT NOT NULL,
            white_rating_before REAL NOT NULL,
            black_rating_before REAL NOT NULL,
            white_delta REAL,
            black_delta REAL,
            status TEXT NOT NULL DEFAULT 'scheduled'
                CHECK (status IN ('scheduled', 'running', 'reported', 'completed', 'void')),
            result TEXT CHECK (result IN ('white', 'black', 'draw', 'void')),
            reason TEXT NOT NULL DEFAULT '',
            summary TEXT NOT NULL DEFAULT '',
            pgn TEXT NOT NULL DEFAULT '',
            output_log TEXT NOT NULL DEFAULT '',
            metrics_csv TEXT NOT NULL DEFAULT '',
            worker TEXT NOT NULL DEFAULT '',
            scheduled_at TEXT NOT NULL,
            started_at TEXT,
            reported_at TEXT,
            completed_at TEXT,
            UNIQUE (batch_id, ordinal),
            CHECK (white_id <> black_id)
        );

        CREATE INDEX IF NOT EXISTS matches_batch_status
            ON matches(batch_id, status);
        CREATE INDEX IF NOT EXISTS matches_engines
            ON matches(white_id, black_id);
        """
    )
    batch_columns = {
        row["name"] for row in connection.execute("PRAGMA table_info(batches)")
    }
    if "strategy" not in batch_columns:
        try:
            connection.execute("ALTER TABLE batches ADD COLUMN strategy TEXT")
        except sqlite3.OperationalError as exc:
            # Two processes may open an old database simultaneously and race
            # to perform this one-time migration.
            if "duplicate column name" not in str(exc).lower():
                raise
    connection.commit()


def _engine_from_row(row: sqlite3.Row) -> EngineRecord:
    return EngineRecord(
        id=row["id"],
        name=row["name"],
        command=row["command"],
        rating=float(row["rating"]),
        initial_rating=float(row["initial_rating"]),
        enabled=bool(row["enabled"]),
        training=bool(row["training"]),
        stateful=bool(row["stateful"]),
        max_parallel=int(row["max_parallel"]),
        parent_id=row["parent_id"],
    )


def get_engine(connection: sqlite3.Connection, engine_id: str) -> EngineRecord:
    row = connection.execute(
        "SELECT * FROM engines WHERE id = ?", (engine_id,)
    ).fetchone()
    if row is None:
        raise EloError(f"unknown engine {engine_id!r}")
    return _engine_from_row(row)


def list_engines(connection: sqlite3.Connection) -> list[EngineRecord]:
    rows = connection.execute("SELECT * FROM engines ORDER BY id").fetchall()
    return [_engine_from_row(row) for row in rows]


def _hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            while chunk := source.read(1024 * 1024):
                digest.update(chunk)
    except OSError as exc:
        raise EloError(f"cannot read artifact {path}: {exc}") from exc
    return digest.hexdigest()


def register_engine(
    connection: sqlite3.Connection,
    engine_id: str,
    command: str,
    *,
    name: str | None = None,
    rating: float = DEFAULT_RATING,
    enabled: bool = True,
    training: bool = False,
    stateful: bool = False,
    max_parallel: int | None = None,
    parent_id: str | None = None,
    artifacts: Sequence[Path] = (),
) -> EngineRecord:
    engine_id = engine_id.strip()
    command = command.strip()
    if not engine_id or any(character.isspace() for character in engine_id):
        raise EloError("engine ID must be non-empty and contain no whitespace")
    if not command:
        raise EloError("engine command must be non-empty")
    if not math.isfinite(rating):
        raise EloError("initial rating must be finite")
    if parent_id is not None:
        get_engine(connection, parent_id)
    if max_parallel is None:
        max_parallel = 1 if stateful else 0
    if max_parallel < 0:
        raise EloError("max_parallel cannot be negative")
    if stateful and max_parallel != 1:
        raise EloError("stateful engines must use max_parallel=1")
    timestamp = utc_now()
    try:
        with connection:
            connection.execute(
                """
                INSERT INTO engines(
                    id, name, command, rating, initial_rating, enabled, training,
                    stateful, max_parallel, parent_id, created_at, updated_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    engine_id,
                    name or engine_id,
                    command,
                    rating,
                    rating,
                    int(enabled),
                    int(training),
                    int(stateful),
                    max_parallel,
                    parent_id,
                    timestamp,
                    timestamp,
                ),
            )
            for artifact in artifacts:
                resolved = artifact.expanduser().resolve()
                connection.execute(
                    "INSERT INTO artifacts(engine_id, path, sha256) VALUES (?, ?, ?)",
                    (engine_id, str(resolved), _hash_file(resolved)),
                )
    except sqlite3.IntegrityError as exc:
        if "engines.id" in str(exc) or "UNIQUE constraint failed: engines.id" in str(
            exc
        ):
            raise EloError(
                f"engine {engine_id!r} already exists; clone it under a new ID for a new version"
            ) from exc
        raise EloError(str(exc)) from exc
    return get_engine(connection, engine_id)


def clone_engine(
    connection: sqlite3.Connection,
    source_id: str,
    new_id: str,
    *,
    command: str | None = None,
    name: str | None = None,
    inherit_rating: bool = False,
    rating: float = DEFAULT_RATING,
    training: bool | None = None,
    stateful: bool | None = None,
    max_parallel: int | None = None,
    artifacts: Sequence[Path] = (),
) -> EngineRecord:
    source = get_engine(connection, source_id)
    initial = source.rating if inherit_rating else rating
    selected_stateful = source.stateful if stateful is None else stateful
    if max_parallel is None and selected_stateful == source.stateful:
        max_parallel = source.max_parallel
    return register_engine(
        connection,
        new_id,
        command or source.command,
        name=name or new_id,
        rating=initial,
        training=source.training if training is None else training,
        stateful=selected_stateful,
        max_parallel=max_parallel,
        parent_id=source.id,
        artifacts=artifacts,
    )


def update_engine(
    connection: sqlite3.Connection,
    engine_id: str,
    *,
    name: str | None = None,
    enabled: bool | None = None,
    max_parallel: int | None = None,
) -> EngineRecord:
    engine = get_engine(connection, engine_id)
    if max_parallel is not None:
        if max_parallel < 0:
            raise EloError("max_parallel cannot be negative")
        if engine.stateful and max_parallel != 1:
            raise EloError("stateful engines must use max_parallel=1")
    fields: list[str] = []
    values: list[object] = []
    for column, value in (
        ("name", name),
        ("enabled", enabled),
        ("max_parallel", max_parallel),
    ):
        if value is not None:
            fields.append(f"{column} = ?")
            values.append(int(value) if isinstance(value, bool) else value)
    if not fields:
        return engine
    fields.append("updated_at = ?")
    values.extend((utc_now(), engine_id))
    with connection:
        connection.execute(
            f"UPDATE engines SET {', '.join(fields)} WHERE id = ?",  # noqa: S608
            values,
        )
    return get_engine(connection, engine_id)


def verify_artifacts(connection: sqlite3.Connection, engine_ids: Iterable[str]) -> None:
    """Reject rated play if a registered executable or checkpoint changed."""

    for engine_id in set(engine_ids):
        rows = connection.execute(
            "SELECT path, sha256 FROM artifacts WHERE engine_id = ? ORDER BY path",
            (engine_id,),
        ).fetchall()
        for row in rows:
            path = Path(row["path"])
            actual = _hash_file(path)
            if actual != row["sha256"]:
                raise EloError(
                    f"artifact changed for {engine_id!r}: {path}; register a new engine version"
                )


def expected_score(rating: float, opponent_rating: float) -> float:
    return 1.0 / (1.0 + 10.0 ** ((opponent_rating - rating) / 400.0))


def elo_delta(
    rating: float,
    opponent_rating: float,
    score: float,
    k_factor: float = DEFAULT_K_FACTOR,
) -> float:
    return k_factor * (score - expected_score(rating, opponent_rating))


def _eligible_engines(
    connection: sqlite3.Connection, rated: bool
) -> list[EngineRecord]:
    engines = [engine for engine in list_engines(connection) if engine.enabled]
    if rated:
        engines = [engine for engine in engines if not engine.training]
    return engines


def _historical_counts(connection: sqlite3.Connection, rated: bool):
    totals: dict[str, int] = {}
    whites: dict[str, int] = {}
    pairs: dict[tuple[str, str], int] = {}
    directed_whites: dict[tuple[str, str], int] = {}
    rows = connection.execute(
        """
        SELECT m.white_id, m.black_id
        FROM matches m JOIN batches b ON b.id = m.batch_id
        WHERE m.status <> 'void' AND (? = 0 OR b.rated = 1)
        """,
        (int(rated),),
    ).fetchall()
    for row in rows:
        white, black = row["white_id"], row["black_id"]
        totals[white] = totals.get(white, 0) + 1
        totals[black] = totals.get(black, 0) + 1
        whites[white] = whites.get(white, 0) + 1
        pair = tuple(sorted((white, black)))
        pairs[pair] = pairs.get(pair, 0) + 1
        directed_whites[(white, black)] = directed_whites.get((white, black), 0) + 1
    return totals, whites, pairs, directed_whites


def _resolve_strategy(focus_engine: str | None, strategy: str | None) -> str | None:
    if focus_engine is None:
        if strategy is not None:
            raise EloError("--strategy requires --engine")
        return None
    selected = strategy or "adaptive"
    if selected not in FOCUSED_STRATEGIES:
        raise EloError(
            f"unknown focused strategy {selected!r}; "
            f"choose one of {', '.join(sorted(FOCUSED_STRATEGIES))}"
        )
    return selected


def suggest_pairings(
    connection: sqlite3.Connection,
    count: int,
    *,
    focus_engine: str | None = None,
    seed: int = 0,
    rated: bool = True,
    strategy: str | None = None,
) -> list[Pairing]:
    """Suggest a balanced concurrent wave without changing the database."""

    if count <= 0:
        raise EloError("match count must be positive")
    strategy = _resolve_strategy(focus_engine, strategy)
    engines = _eligible_engines(connection, rated)
    by_id = {engine.id: engine for engine in engines}
    if focus_engine is not None and focus_engine not in by_id:
        registered = get_engine(connection, focus_engine)
        if registered.training and rated:
            raise EloError(
                f"training engine {focus_engine!r} can only play unrated matches"
            )
        raise EloError(f"engine {focus_engine!r} is disabled")
    if len(engines) < 2:
        kind = "fixed, enabled" if rated else "enabled"
        raise EloError(f"at least two {kind} engines are required")

    totals, whites, pair_counts, directed_whites = _historical_counts(connection, rated)
    historical_totals = totals.copy()
    batch_uses = {engine.id: 0 for engine in engines}
    rng = random.Random(seed)
    suggestions: list[Pairing] = []

    def has_capacity(engine: EngineRecord) -> bool:
        return engine.max_parallel == 0 or batch_uses[engine.id] < engine.max_parallel

    def choose_colors(
        first: EngineRecord, second: EngineRecord
    ) -> tuple[EngineRecord, EngineRecord]:
        first_as_white = directed_whites.get((first.id, second.id), 0)
        second_as_white = directed_whites.get((second.id, first.id), 0)
        if first_as_white != second_as_white:
            white, black = (
                (first, second) if first_as_white < second_as_white else (second, first)
            )
        else:
            first_whites = whites.get(first.id, 0)
            second_whites = whites.get(second.id, 0)
            if first_whites != second_whites:
                white, black = (
                    (first, second) if first_whites < second_whites else (second, first)
                )
            else:
                white, black = (
                    (first, second) if rng.randrange(2) == 0 else (second, first)
                )
        return white, black

    for _ in range(count):
        available = [engine for engine in engines if has_capacity(engine)]
        selected: tuple[EngineRecord, EngineRecord] | None = None
        if focus_engine is not None:
            focus = by_id[focus_engine]
            if not has_capacity(focus):
                break
            opponents = [engine for engine in available if engine.id != focus.id]
            if not opponents:
                break
            if strategy == "coverage":
                opponent = min(
                    opponents,
                    key=lambda item: (
                        pair_counts.get(tuple(sorted((focus.id, item.id))), 0),
                        abs(focus.rating - item.rating),
                        totals.get(item.id, 0),
                        rng.random(),
                    ),
                )
            else:
                # Every tenth focused game is a bounded exploration game. The
                # other nine favor informative, reliable opponents near the
                # focus engine's current rating.
                focus_game_number = totals.get(focus.id, 0) + 1
                exploration = focus_game_number % 10 == 0
                exploration_pool = [
                    item
                    for item in opponents
                    if abs(focus.rating - item.rating) <= 400.0
                ]
                if exploration and exploration_pool:
                    opponent = min(
                        exploration_pool,
                        key=lambda item: (
                            pair_counts.get(tuple(sorted((focus.id, item.id))), 0),
                            batch_uses[item.id],
                            totals.get(item.id, 0),
                            abs(focus.rating - item.rating),
                            rng.random(),
                        ),
                    )
                else:
                    has_lower = any(item.rating < focus.rating for item in opponents)
                    has_higher = any(item.rating > focus.rating for item in opponents)
                    lower_uses = sum(
                        batch_uses[item.id]
                        for item in opponents
                        if item.rating < focus.rating
                    )
                    higher_uses = sum(
                        batch_uses[item.id]
                        for item in opponents
                        if item.rating > focus.rating
                    )

                    def adaptive_score(item: EngineRecord) -> tuple[float, float]:
                        gap = abs(focus.rating - item.rating)
                        head_to_head = pair_counts.get(
                            tuple(sorted((focus.id, item.id))), 0
                        )
                        proximity = math.exp(-gap / 200.0)
                        repeat_factor = (1.0 + head_to_head) ** -0.25
                        opponent_games = historical_totals.get(item.id, 0)
                        confidence = 0.5 + 0.5 * min(
                            1.0, math.sqrt(opponent_games / 40.0)
                        )
                        load_factor = (1.0 + batch_uses[item.id]) ** -0.5
                        bracket_factor = 1.0
                        if has_lower and has_higher:
                            if item.rating < focus.rating and lower_uses < higher_uses:
                                bracket_factor = 1.15
                            elif (
                                item.rating > focus.rating and higher_uses < lower_uses
                            ):
                                bracket_factor = 1.15
                        score = (
                            proximity
                            * repeat_factor
                            * confidence
                            * load_factor
                            * bracket_factor
                        )
                        return score, rng.random()

                    opponent = max(opponents, key=adaptive_score)
            selected = focus, opponent
        else:
            candidates: list[tuple[tuple[float, ...], EngineRecord, EngineRecord]] = []
            for index, first in enumerate(available):
                for second in available[index + 1 :]:
                    candidates.append(
                        (
                            (
                                max(totals.get(first.id, 0), totals.get(second.id, 0)),
                                totals.get(first.id, 0) + totals.get(second.id, 0),
                                pair_counts.get(
                                    tuple(sorted((first.id, second.id))), 0
                                ),
                                abs(first.rating - second.rating),
                                rng.random(),
                            ),
                            first,
                            second,
                        )
                    )
            if not candidates:
                break
            _, first, second = min(candidates, key=lambda item: item[0])
            selected = first, second

        first, second = selected
        white, black = choose_colors(first, second)
        suggestions.append(
            Pairing(
                match_id=None,
                batch_id=None,
                white_id=white.id,
                black_id=black.id,
                white_name=white.name,
                black_name=black.name,
                white_command=white.command,
                black_command=black.command,
                white_rating=white.rating,
                black_rating=black.rating,
            )
        )
        batch_uses[first.id] += 1
        batch_uses[second.id] += 1
        totals[first.id] = totals.get(first.id, 0) + 1
        totals[second.id] = totals.get(second.id, 0) + 1
        whites[white.id] = whites.get(white.id, 0) + 1
        pair = tuple(sorted((first.id, second.id)))
        pair_counts[pair] = pair_counts.get(pair, 0) + 1
        directed_whites[(white.id, black.id)] = (
            directed_whites.get((white.id, black.id), 0) + 1
        )

    if not suggestions:
        raise EloError("no pairing satisfies the engines' concurrency limits")
    return suggestions


def schedule_batch(
    connection: sqlite3.Connection,
    count: int,
    *,
    focus_engine: str | None = None,
    seed: int = 0,
    rated: bool = True,
    k_factor: float = DEFAULT_K_FACTOR,
    strategy: str | None = None,
    note: str = "",
) -> tuple[int, list[Pairing]]:
    if not math.isfinite(k_factor) or k_factor <= 0:
        raise EloError("K-factor must be positive and finite")
    strategy = _resolve_strategy(focus_engine, strategy)
    try:
        connection.execute("BEGIN IMMEDIATE")
        if rated:
            open_batch = connection.execute(
                "SELECT id FROM batches WHERE rated = 1 AND status = 'open'"
            ).fetchone()
            if open_batch:
                raise EloError(
                    f"rated batch {open_batch['id']} is still open; report/finalize it first"
                )
        suggestions = suggest_pairings(
            connection,
            count,
            focus_engine=focus_engine,
            seed=seed,
            rated=rated,
            strategy=strategy,
        )
        if rated:
            verify_artifacts(
                connection,
                [
                    identifier
                    for pairing in suggestions
                    for identifier in (pairing.white_id, pairing.black_id)
                ],
            )
        cursor = connection.execute(
            """
            INSERT INTO batches(
                created_at, status, rated, k_factor, seed, focus_engine, strategy, note
            ) VALUES (?, 'open', ?, ?, ?, ?, ?, ?)
            """,
            (utc_now(), int(rated), k_factor, seed, focus_engine, strategy, note),
        )
        batch_id = int(cursor.lastrowid)
        scheduled: list[Pairing] = []
        for ordinal, pairing in enumerate(suggestions, 1):
            cursor = connection.execute(
                """
                INSERT INTO matches(
                    batch_id, ordinal, white_id, black_id,
                    white_command, black_command,
                    white_rating_before, black_rating_before, scheduled_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    batch_id,
                    ordinal,
                    pairing.white_id,
                    pairing.black_id,
                    pairing.white_command,
                    pairing.black_command,
                    pairing.white_rating,
                    pairing.black_rating,
                    utc_now(),
                ),
            )
            scheduled.append(
                Pairing(
                    match_id=int(cursor.lastrowid),
                    batch_id=batch_id,
                    ordinal=ordinal,
                    **{
                        key: value
                        for key, value in asdict(pairing).items()
                        if key not in {"match_id", "batch_id", "ordinal"}
                    },
                )
            )
        connection.commit()
        return batch_id, scheduled
    except Exception:
        connection.rollback()
        raise


def batch_pairings(connection: sqlite3.Connection, batch_id: int) -> list[Pairing]:
    rows = connection.execute(
        """
        SELECT m.*, ew.name AS white_name, eb.name AS black_name
        FROM matches m
        JOIN engines ew ON ew.id = m.white_id
        JOIN engines eb ON eb.id = m.black_id
        WHERE m.batch_id = ?
        ORDER BY m.ordinal
        """,
        (batch_id,),
    ).fetchall()
    return [
        Pairing(
            match_id=row["id"],
            batch_id=row["batch_id"],
            white_id=row["white_id"],
            black_id=row["black_id"],
            white_name=row["white_name"],
            black_name=row["black_name"],
            white_command=row["white_command"],
            black_command=row["black_command"],
            white_rating=row["white_rating_before"],
            black_rating=row["black_rating_before"],
            ordinal=row["ordinal"],
        )
        for row in rows
    ]


def claim_match(
    connection: sqlite3.Connection, match_id: int, worker: str = ""
) -> bool:
    with connection:
        cursor = connection.execute(
            """
            UPDATE matches SET status = 'running', worker = ?, started_at = ?
            WHERE id = ? AND status = 'scheduled'
            """,
            (worker, utc_now(), match_id),
        )
    return cursor.rowcount == 1


def reset_running_match(connection: sqlite3.Connection, match_id: int) -> None:
    with connection:
        cursor = connection.execute(
            """
            UPDATE matches SET status = 'scheduled', worker = '', started_at = NULL
            WHERE id = ? AND status = 'running'
            """,
            (match_id,),
        )
    if cursor.rowcount != 1:
        raise EloError(f"match {match_id} is not running")


def report_result(
    connection: sqlite3.Connection,
    match_id: int,
    result: str,
    *,
    reason: str = "",
    summary: str = "",
    pgn: str = "",
    output_log: str = "",
    metrics_csv: str = "",
    auto_finalize: bool = True,
) -> int:
    result = result.lower()
    if result not in RESULTS:
        raise EloError(f"result must be one of {', '.join(sorted(RESULTS))}")
    status = "void" if result == "void" else "reported"
    with connection:
        row = connection.execute(
            "SELECT batch_id, status FROM matches WHERE id = ?", (match_id,)
        ).fetchone()
        if row is None:
            raise EloError(f"unknown match {match_id}")
        if row["status"] not in {"scheduled", "running"}:
            raise EloError(f"match {match_id} has already been {row['status']}")
        connection.execute(
            """
            UPDATE matches
            SET status = ?, result = ?, reason = ?, summary = ?, pgn = ?,
                output_log = ?, metrics_csv = ?, reported_at = ?
            WHERE id = ?
            """,
            (
                status,
                result,
                reason,
                summary,
                pgn,
                output_log,
                metrics_csv,
                utc_now(),
                match_id,
            ),
        )
    batch_id = int(row["batch_id"])
    if auto_finalize:
        remaining = connection.execute(
            """
            SELECT COUNT(*) FROM matches
            WHERE batch_id = ? AND status IN ('scheduled', 'running')
            """,
            (batch_id,),
        ).fetchone()[0]
        if remaining == 0:
            finalize_batch(connection, batch_id)
    return batch_id


def finalize_batch(connection: sqlite3.Connection, batch_id: int) -> list[sqlite3.Row]:
    """Apply a complete wave atomically and return its completed match rows."""

    try:
        connection.execute("BEGIN IMMEDIATE")
        batch = connection.execute(
            "SELECT * FROM batches WHERE id = ?", (batch_id,)
        ).fetchone()
        if batch is None:
            raise EloError(f"unknown batch {batch_id}")
        if batch["status"] != "open":
            raise EloError(f"batch {batch_id} is already {batch['status']}")
        active = connection.execute(
            """
            SELECT COUNT(*) FROM matches
            WHERE batch_id = ? AND status IN ('scheduled', 'running')
            """,
            (batch_id,),
        ).fetchone()[0]
        if active:
            raise EloError(f"batch {batch_id} still has {active} unfinished match(es)")

        rows = connection.execute(
            "SELECT * FROM matches WHERE batch_id = ? ORDER BY ordinal", (batch_id,)
        ).fetchall()
        totals: dict[str, float] = {}
        for row in rows:
            if row["status"] != "reported":
                continue
            white_delta = black_delta = 0.0
            if batch["rated"]:
                white_score = {"white": 1.0, "black": 0.0, "draw": 0.5}[row["result"]]
                white_delta = elo_delta(
                    row["white_rating_before"],
                    row["black_rating_before"],
                    white_score,
                    batch["k_factor"],
                )
                black_delta = elo_delta(
                    row["black_rating_before"],
                    row["white_rating_before"],
                    1.0 - white_score,
                    batch["k_factor"],
                )
                totals[row["white_id"]] = totals.get(row["white_id"], 0.0) + white_delta
                totals[row["black_id"]] = totals.get(row["black_id"], 0.0) + black_delta
            connection.execute(
                """
                UPDATE matches
                SET status = 'completed', white_delta = ?, black_delta = ?, completed_at = ?
                WHERE id = ?
                """,
                (white_delta, black_delta, utc_now(), row["id"]),
            )
        for engine_id, delta in totals.items():
            connection.execute(
                "UPDATE engines SET rating = rating + ?, updated_at = ? WHERE id = ?",
                (delta, utc_now(), engine_id),
            )
        connection.execute(
            "UPDATE batches SET status = 'finalized', finalized_at = ? WHERE id = ?",
            (utc_now(), batch_id),
        )
        connection.commit()
    except Exception:
        connection.rollback()
        raise
    return connection.execute(
        "SELECT * FROM matches WHERE batch_id = ? ORDER BY ordinal", (batch_id,)
    ).fetchall()


def leaderboard(
    connection: sqlite3.Connection, include_training: bool = False
) -> list[dict[str, object]]:
    engines = list_engines(connection)
    if not include_training:
        engines = [engine for engine in engines if not engine.training]
    output: list[dict[str, object]] = []
    for engine in engines:
        rows = connection.execute(
            """
            SELECT m.white_id, m.black_id, m.result
            FROM matches m JOIN batches b ON b.id = m.batch_id
            WHERE m.status = 'completed' AND b.rated = 1
              AND (m.white_id = ? OR m.black_id = ?)
            """,
            (engine.id, engine.id),
        ).fetchall()
        wins = losses = draws = white_games = black_games = 0
        for row in rows:
            is_white = row["white_id"] == engine.id
            white_games += int(is_white)
            black_games += int(not is_white)
            if row["result"] == "draw":
                draws += 1
            elif (row["result"] == "white") == is_white:
                wins += 1
            else:
                losses += 1
        output.append(
            {
                "id": engine.id,
                "name": engine.name,
                "rating": engine.rating,
                "games": len(rows),
                "wins": wins,
                "draws": draws,
                "losses": losses,
                "white_games": white_games,
                "black_games": black_games,
                "enabled": engine.enabled,
                "training": engine.training,
            }
        )
    return sorted(output, key=lambda row: (-float(row["rating"]), str(row["id"])))


def match_history(
    connection: sqlite3.Connection, limit: int = 20
) -> list[dict[str, object]]:
    rows = connection.execute(
        """
        SELECT m.*, b.rated, b.strategy, b.status AS batch_status
        FROM matches m JOIN batches b ON b.id = m.batch_id
        ORDER BY m.id DESC LIMIT ?
        """,
        (limit,),
    ).fetchall()
    return [dict(row) for row in rows]


def pending_matches(connection: sqlite3.Connection) -> list[dict[str, object]]:
    rows = connection.execute(
        """
        SELECT m.*, ew.name AS white_name, eb.name AS black_name,
               b.rated, b.strategy
        FROM matches m
        JOIN batches b ON b.id = m.batch_id
        JOIN engines ew ON ew.id = m.white_id
        JOIN engines eb ON eb.id = m.black_id
        WHERE m.status IN ('scheduled', 'running', 'reported')
        ORDER BY m.batch_id, m.ordinal
        """
    ).fetchall()
    return [dict(row) for row in rows]


def _print_pairings(pairings: Sequence[Pairing], output_format: str) -> None:
    if output_format == "json":
        print(json.dumps([asdict(pairing) for pairing in pairings], indent=2))
        return
    if output_format == "tsv":
        print("match_id\tbatch_id\twhite_id\tblack_id\twhite_command\tblack_command")
        for item in pairings:
            print(
                f"{item.match_id or ''}\t{item.batch_id or ''}\t{item.white_id}\t"
                f"{item.black_id}\t{item.white_command}\t{item.black_command}"
            )
        return
    for item in pairings:
        prefix = f"Match {item.match_id}: " if item.match_id else ""
        print(
            f"{prefix}white={item.white_id} ({item.white_rating:.1f}), "
            f"black={item.black_id} ({item.black_rating:.1f})"
        )


def _print_leaderboard(rows: Sequence[dict[str, object]]) -> None:
    print("Rank  Engine                    Elo     Games    W    D    L  Enabled")
    for rank, row in enumerate(rows, 1):
        print(
            f"{rank:>4}  {str(row['id']):<24} {float(row['rating']):>7.1f} "
            f"{int(row['games']):>7} {int(row['wins']):>4} {int(row['draws']):>4} "
            f"{int(row['losses']):>4}  {'yes' if row['enabled'] else 'no'}"
        )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path, default=DEFAULT_DATABASE)
    subparsers = parser.add_subparsers(dest="action", required=True)

    register = subparsers.add_parser(
        "register", help="register an immutable engine version"
    )
    register.add_argument("id")
    register.add_argument("--command", required=True)
    register.add_argument("--name")
    register.add_argument("--rating", type=float, default=DEFAULT_RATING)
    register.add_argument("--disabled", action="store_true")
    register.add_argument("--training", action="store_true")
    register.add_argument("--stateful", action="store_true")
    register.add_argument("--max-parallel", type=int)
    register.add_argument("--artifact", action="append", type=Path, default=[])

    clone = subparsers.add_parser(
        "clone", help="create a distinct version from an engine"
    )
    clone.add_argument("source_id")
    clone.add_argument("new_id")
    clone.add_argument("--command")
    clone.add_argument("--name")
    clone.add_argument("--rating", type=float, default=DEFAULT_RATING)
    clone.add_argument("--inherit-rating", action="store_true")
    clone.add_argument(
        "--training", action=argparse.BooleanOptionalAction, default=None
    )
    clone.add_argument(
        "--stateful", action=argparse.BooleanOptionalAction, default=None
    )
    clone.add_argument("--max-parallel", type=int)
    clone.add_argument("--artifact", action="append", type=Path, default=[])

    update = subparsers.add_parser("update", help="update non-playing engine metadata")
    update.add_argument("id")
    update.add_argument("--name")
    update.add_argument(
        "--enabled", action=argparse.BooleanOptionalAction, default=None
    )
    update.add_argument("--max-parallel", type=int)

    for action in ("enable", "disable"):
        command = subparsers.add_parser(action, help=f"{action} an engine")
        command.add_argument("id")

    for action in ("list", "leaderboard"):
        command = subparsers.add_parser(action)
        command.add_argument("--json", action="store_true")
        if action == "leaderboard":
            command.add_argument("--include-training", action="store_true")

    for action in ("suggest", "schedule"):
        command = subparsers.add_parser(action)
        command.add_argument("count", type=int)
        command.add_argument("--engine", dest="focus_engine")
        command.add_argument(
            "--strategy",
            choices=sorted(FOCUSED_STRATEGIES),
            help="focused matchmaking strategy; requires --engine (default: adaptive)",
        )
        command.add_argument("--seed", type=int, default=0)
        command.add_argument("--unrated", action="store_true")
        command.add_argument(
            "--format", choices=("text", "json", "tsv"), default="text"
        )
        if action == "schedule":
            command.add_argument("--k-factor", type=float, default=DEFAULT_K_FACTOR)
            command.add_argument("--note", default="")

    report = subparsers.add_parser("report", help="report one externally played result")
    report.add_argument("match_id", type=int)
    report.add_argument("result", choices=sorted(RESULTS))
    report.add_argument("--reason", default="")
    report.add_argument("--summary", default="")
    report.add_argument("--pgn", type=Path)
    report.add_argument("--no-auto-finalize", action="store_true")

    finalize = subparsers.add_parser("finalize")
    finalize.add_argument("batch_id", type=int)

    history = subparsers.add_parser("history")
    history.add_argument("--limit", type=int, default=20)
    history.add_argument("--json", action="store_true")

    pending = subparsers.add_parser("pending")
    pending.add_argument("--json", action="store_true")

    reset = subparsers.add_parser("reset-running")
    reset.add_argument("match_id", type=int)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if (
        args.action in {"suggest", "schedule"}
        and args.strategy is not None
        and args.focus_engine is None
    ):
        parser.error("--strategy requires --engine")
    try:
        with connect(args.database) as connection:
            if args.action == "register":
                engine = register_engine(
                    connection,
                    args.id,
                    args.command,
                    name=args.name,
                    rating=args.rating,
                    enabled=not args.disabled,
                    training=args.training,
                    stateful=args.stateful,
                    max_parallel=args.max_parallel,
                    artifacts=args.artifact,
                )
                print(
                    f"Registered {engine.id}: {engine.name} at {engine.rating:.1f} Elo"
                )
            elif args.action == "clone":
                engine = clone_engine(
                    connection,
                    args.source_id,
                    args.new_id,
                    command=args.command,
                    name=args.name,
                    inherit_rating=args.inherit_rating,
                    rating=args.rating,
                    training=args.training,
                    stateful=args.stateful,
                    max_parallel=args.max_parallel,
                    artifacts=args.artifact,
                )
                print(
                    f"Cloned {args.source_id} as {engine.id} at {engine.rating:.1f} Elo"
                )
            elif args.action == "update":
                engine = update_engine(
                    connection,
                    args.id,
                    name=args.name,
                    enabled=args.enabled,
                    max_parallel=args.max_parallel,
                )
                print(f"Updated {engine.id}")
            elif args.action in {"enable", "disable"}:
                engine = update_engine(
                    connection,
                    args.id,
                    enabled=args.action == "enable",
                )
                print(f"{args.action.title()}d {engine.id}")
            elif args.action == "list":
                engines = list_engines(connection)
                if args.json:
                    print(json.dumps([asdict(engine) for engine in engines], indent=2))
                else:
                    for engine in engines:
                        flags = ["enabled" if engine.enabled else "disabled"]
                        if engine.training:
                            flags.append("training")
                        if engine.stateful:
                            flags.append("stateful")
                        parallel = (
                            "unlimited"
                            if engine.max_parallel == 0
                            else str(engine.max_parallel)
                        )
                        print(
                            f"{engine.id}: {engine.name}; Elo {engine.rating:.1f}; "
                            f"parallel {parallel}; {', '.join(flags)}\n  {engine.command}"
                        )
            elif args.action == "leaderboard":
                rows = leaderboard(connection, args.include_training)
                if args.json:
                    print(json.dumps(rows, indent=2))
                else:
                    _print_leaderboard(rows)
            elif args.action == "suggest":
                pairings = suggest_pairings(
                    connection,
                    args.count,
                    focus_engine=args.focus_engine,
                    seed=args.seed,
                    rated=not args.unrated,
                    strategy=args.strategy,
                )
                _print_pairings(pairings, args.format)
            elif args.action == "schedule":
                batch_id, pairings = schedule_batch(
                    connection,
                    args.count,
                    focus_engine=args.focus_engine,
                    seed=args.seed,
                    rated=not args.unrated,
                    k_factor=args.k_factor,
                    strategy=args.strategy,
                    note=args.note,
                )
                if args.format == "text":
                    print(
                        f"Batch {batch_id} ({'unrated' if args.unrated else 'rated'})"
                    )
                _print_pairings(pairings, args.format)
            elif args.action == "report":
                pgn = args.pgn.read_text() if args.pgn else ""
                batch_id = report_result(
                    connection,
                    args.match_id,
                    args.result,
                    reason=args.reason,
                    summary=args.summary,
                    pgn=pgn,
                    auto_finalize=not args.no_auto_finalize,
                )
                print(
                    f"Reported match {args.match_id} in batch {batch_id}: {args.result}"
                )
            elif args.action == "finalize":
                rows = finalize_batch(connection, args.batch_id)
                print(f"Finalized batch {args.batch_id} with {len(rows)} match(es)")
            elif args.action == "history":
                rows = match_history(connection, args.limit)
                if args.json:
                    print(json.dumps(rows, indent=2))
                else:
                    for row in rows:
                        delta = ""
                        if row["white_delta"] is not None:
                            delta = f" ({row['white_delta']:+.2f}/{row['black_delta']:+.2f})"
                        print(
                            f"Match {row['id']} batch {row['batch_id']}: {row['white_id']} vs "
                            f"{row['black_id']} — {row['result'] or row['status']}{delta}"
                        )
            elif args.action == "pending":
                rows = pending_matches(connection)
                if args.json:
                    print(json.dumps(rows, indent=2))
                else:
                    for row in rows:
                        print(
                            f"Match {row['id']} batch {row['batch_id']}: {row['white_id']} vs "
                            f"{row['black_id']} [{row['status']}]"
                        )
            elif args.action == "reset-running":
                reset_running_match(connection, args.match_id)
                print(f"Reset match {args.match_id} to scheduled")
    except (EloError, OSError, sqlite3.Error) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
