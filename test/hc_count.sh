#!/bin/sh
# Usage: sh test/hc_count.sh build/5dtools [hc-profile options...]
tools=$1
shift
exec "$tools" hc-profile --warmup-rounds 0 --rounds 1 "$@" <<'HC_PROFILE_INPUT'
EXPECTED=30134 COUNT [Board "Standard"] 1. (0T1)Nc3 / (0T1)a6 2. (0T2)Rb1 / (0T2)a5 3. (0T3)Rb1>>(0T2)b1~ / (1T2)a5 (0T3)Nb8>>(0T2)b6~
EXPECTED=55332 COUNT [Board "Standard - Turn Zero"] 1. Nf3 / e6 2. b3 / c6 3. e3 / Qb6 4. Ng5 / Q>>xf2+ 5. Kxf2 / Nf6 6. e3 / (-1T2)N>>(-1T1)f4+ 7. (-1T3)Ke1 / K>>d8 8. (-1T4)Qf3 / f6 9. (L-1)Nh3 (L0)Qf3 / (L0)K>>d8 (L-1)f>>f6 10. (L-1)Qf7 (L0)Qxf7 / (L0)B>xf7+
EXPECTED=1494 COUNT [Board "Standard"] 1. (0T1)h3 / (0T1)b6 2. (0T2)g3 / (0T2)g6 3. (0T3)f3 / (0T3)a6 4. (0T4)e3 / (0T4)Nb8>>(0T2)b7 5. (0T5)d3 (-1T3)f3 / (0T5)Ng8>>(0T3)g7 (-1T3)g6 6. (-1T4)e3 (-2T4)e3 / (-1T4)a6
HC_PROFILE_INPUT
