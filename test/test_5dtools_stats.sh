#!/usr/bin/env bash
set -euo pipefail

tool="$1"
pgn='[Board "Standard - Turn Zero"]

1. e4 / e5
2. Nf3'

initial=$(printf '%s\n' "$pgn" | "$tool" stats --at 0 --format json)
initial_b=$(printf '%s\n' "$pgn" | "$tool" stats --at 0b --format json)
after_1w=$(printf '%s\n' "$pgn" | "$tool" stats --at 1w --format json)
after_1b=$(printf '%s\n' "$pgn" | "$tool" stats --at 1b --format json)

[[ "$initial" == "$initial_b" ]]
[[ "$initial" == *'"actions_played":0'* ]]
[[ "$initial" == *'"side_to_move":"white"'* ]]
[[ "$after_1w" == *'"actions_played":1'* ]]
[[ "$after_1w" == *'"side_to_move":"black"'* ]]
[[ "$after_1b" == *'"actions_played":2'* ]]
[[ "$after_1b" == *'"side_to_move":"white"'* ]]

set +e
missing=$(printf '%s\n' "$pgn" | "$tool" stats --at 2b --format json 2>&1)
missing_status=$?
invalid=$(printf '%s\n' "$pgn" | "$tool" stats --at 0w 2>&1)
invalid_status=$?
set -e

[[ $missing_status -eq 3 ]]
[[ "$missing" == *'last submitted main-line turn is 2w'* ]]
[[ $invalid_status -eq 2 ]]
[[ "$invalid" == *"invalid turn selector '0w'"* ]]

capped=$(printf '%s\n' '[Board "Standard - Turn Zero"]' \
    | "$tool" count --at 0 --format json balanced 19)
[[ "$capped" == '{"legal_actions":19}' ]]
