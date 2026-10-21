# Canonical human-position suite v1

See the [testing workflow](../../../docs/engine/testing.md) for build, reference, run, and extension commands.

The manifest and 64 snapshots define this suite. Do not resample or reorder them during an experiment series. Each position comes from a distinct source game and preserves its full multiverse, historical boards, side to move, and unmoved-piece rights. Snapshot round trips were checked against source FEN and state hashes.

Selection scanned 775 Turn Zero games with queen-only promotions, then measured 256 candidates. Positions have at least eight completed actions before them and eight recorded actions remaining. Source `171.5dpgn` crashed the native parser while loading (SIGSEGV) and was excluded; the exclusion is recorded in the manifest. Other named variants are outside v1.

The fixed pool contains 24 cheap, 24 moderate, 12 expensive, and 4 stress positions, with equal White/Black-to-move counts inside every cost group. Cost is a local short-search measurement using `zero-capture-check-pw`, including setup, measured at revision `b6e16ec` with Release, LTO, and `-fno-rtti` in the native engine. pybind11 alone requires RTTI in its binding file.

The cheap/moderate/expensive/stress boundaries are 2.330, 4.782, and 8.222 ms of measured cost (maximum of ten-iteration time and time scaled to 100 iterations). These frozen groups describe the selection machine, not universal performance. The pool covers 1–7 playable timelines and 11–156 stored boards. Small prefixes deliberately include contrasting history sizes and playable widths.

| Subset | IDs | Side to move | Cost composition |
|---|---|---|---|
| quick | p01–p04 | 2 White, 2 Black | 2 cheap, 2 moderate |
| standard | p01–p08 | 4 White, 4 Black | 3 cheap, 3 moderate, 2 expensive |
| confirm | p01–p16 | 8 White, 8 Black | 6 cheap, 6 moderate, 4 expensive |
| pool | p01–p64 | 32 White, 32 Black | 24 cheap, 24 moderate, 12 expensive, 4 stress |
| stress | p61–p64 | 2 White, 2 Black | Separate targeted runs |

The default eight have 1, 2, 3, 5, and 6 playable timelines and 17–156 stored boards. Stress includes the two seven-playable-timeline positions. This is a deliberately stratified engineering suite, not a frequency-weighted sample of human play. None is an independent held-out confirmation after tuning on it.

`Actions` counts submitted actions in the source game, not the multiverse Present. `Advantage` is relative to the player to move (`hostile_created - friendly_created`). `Cost` is the frozen local probe measurement.

| ID | Source | Actions | Remaining | To move | Playable | Total lines | Boards | Advantage | Group | Cost (ms) |
|---|---|---:|---:|---|---:|---:|---:|---:|---|---:|
| [p01](positions/p01.5dpgn) | [681.5dpgn](../../pgn/online/681.5dpgn) | 70 | 24 | white | 1 | 1 | 72 | +0 | cheap | 1.784 |
| [p02](positions/p02.5dpgn) | [268.5dpgn](../../pgn/online/268.5dpgn) | 75 | 20 | black | 2 | 2 | 95 | -1 | cheap | 1.198 |
| [p03](positions/p03.5dpgn) | [004.5dpgn](../../pgn/online/004.5dpgn) | 124 | 8 | white | 3 | 4 | 130 | -1 | moderate | 2.672 |
| [p04](positions/p04.5dpgn) | [569.5dpgn](../../pgn/online/569.5dpgn) | 15 | 24 | black | 1 | 1 | 17 | +0 | moderate | 2.350 |
| [p05](positions/p05.5dpgn) | [242.5dpgn](../../pgn/online/242.5dpgn) | 62 | 12 | white | 2 | 2 | 65 | +1 | cheap | 0.934 |
| [p06](positions/p06.5dpgn) | [310.5dpgn](../../pgn/online/310.5dpgn) | 35 | 24 | black | 2 | 3 | 39 | +0 | moderate | 2.371 |
| [p07](positions/p07.5dpgn) | [640.5dpgn](../../pgn/online/640.5dpgn) | 42 | 11 | white | 5 | 6 | 57 | +3 | expensive | 5.201 |
| [p08](positions/p08.5dpgn) | [757.5dpgn](../../pgn/online/757.5dpgn) | 83 | 11 | black | 6 | 6 | 156 | +5 | expensive | 5.175 |
| [p09](positions/p09.5dpgn) | [721.5dpgn](../../pgn/online/721.5dpgn) | 29 | 17 | black | 2 | 2 | 32 | +1 | cheap | 0.970 |
| [p10](positions/p10.5dpgn) | [159.5dpgn](../../pgn/online/159.5dpgn) | 16 | 9 | white | 2 | 3 | 20 | +0 | moderate | 2.397 |
| [p11](positions/p11.5dpgn) | [207.5dpgn](../../pgn/online/207.5dpgn) | 60 | 18 | white | 2 | 2 | 66 | -1 | cheap | 1.147 |
| [p12](positions/p12.5dpgn) | [738.5dpgn](../../pgn/online/738.5dpgn) | 59 | 11 | black | 2 | 2 | 63 | -1 | cheap | 1.026 |
| [p13](positions/p13.5dpgn) | [740.5dpgn](../../pgn/online/740.5dpgn) | 14 | 9 | white | 3 | 3 | 19 | +0 | moderate | 2.402 |
| [p14](positions/p14.5dpgn) | [747.5dpgn](../../pgn/online/747.5dpgn) | 53 | 9 | black | 3 | 3 | 57 | +2 | moderate | 2.392 |
| [p15](positions/p15.5dpgn) | [370.5dpgn](../../pgn/online/370.5dpgn) | 24 | 12 | white | 3 | 6 | 32 | +1 | expensive | 4.796 |
| [p16](positions/p16.5dpgn) | [059.5dpgn](../../pgn/online/059.5dpgn) | 61 | 12 | black | 2 | 2 | 68 | +1 | expensive | 4.801 |
| [p17](positions/p17.5dpgn) | [633.5dpgn](../../pgn/online/633.5dpgn) | 24 | 13 | white | 2 | 2 | 27 | +1 | cheap | 1.179 |
| [p18](positions/p18.5dpgn) | [516.5dpgn](../../pgn/online/516.5dpgn) | 56 | 15 | white | 2 | 2 | 60 | -1 | cheap | 1.388 |
| [p19](positions/p19.5dpgn) | [223.5dpgn](../../pgn/online/223.5dpgn) | 16 | 13 | white | 2 | 3 | 20 | +0 | cheap | 1.643 |
| [p20](positions/p20.5dpgn) | [299.5dpgn](../../pgn/online/299.5dpgn) | 14 | 13 | white | 1 | 1 | 16 | +0 | cheap | 1.850 |
| [p21](positions/p21.5dpgn) | [733.5dpgn](../../pgn/online/733.5dpgn) | 64 | 11 | white | 3 | 3 | 73 | +0 | cheap | 2.256 |
| [p22](positions/p22.5dpgn) | [167.5dpgn](../../pgn/online/167.5dpgn) | 30 | 9 | white | 3 | 3 | 35 | +0 | cheap | 2.330 |
| [p23](positions/p23.5dpgn) | [659.5dpgn](../../pgn/online/659.5dpgn) | 64 | 8 | white | 2 | 2 | 67 | +1 | cheap | 0.960 |
| [p24](positions/p24.5dpgn) | [546.5dpgn](../../pgn/online/546.5dpgn) | 48 | 9 | white | 2 | 2 | 51 | +1 | cheap | 1.182 |
| [p25](positions/p25.5dpgn) | [005.5dpgn](../../pgn/online/005.5dpgn) | 80 | 21 | white | 2 | 2 | 84 | -1 | cheap | 1.313 |
| [p26](positions/p26.5dpgn) | [017.5dpgn](../../pgn/online/017.5dpgn) | 77 | 14 | black | 2 | 2 | 80 | +1 | cheap | 1.045 |
| [p27](positions/p27.5dpgn) | [095.5dpgn](../../pgn/online/095.5dpgn) | 13 | 13 | black | 2 | 3 | 17 | +0 | cheap | 1.382 |
| [p28](positions/p28.5dpgn) | [585.5dpgn](../../pgn/online/585.5dpgn) | 41 | 9 | black | 3 | 3 | 45 | +2 | cheap | 1.418 |
| [p29](positions/p29.5dpgn) | [662.5dpgn](../../pgn/online/662.5dpgn) | 51 | 9 | black | 2 | 3 | 77 | +0 | cheap | 1.503 |
| [p30](positions/p30.5dpgn) | [448.5dpgn](../../pgn/online/448.5dpgn) | 9 | 44 | black | 1 | 1 | 11 | +0 | cheap | 1.536 |
| [p31](positions/p31.5dpgn) | [624.5dpgn](../../pgn/online/624.5dpgn) | 31 | 9 | black | 3 | 3 | 36 | +0 | cheap | 1.791 |
| [p32](positions/p32.5dpgn) | [188.5dpgn](../../pgn/online/188.5dpgn) | 67 | 25 | black | 4 | 4 | 92 | +3 | cheap | 1.795 |
| [p33](positions/p33.5dpgn) | [366.5dpgn](../../pgn/online/366.5dpgn) | 69 | 11 | black | 1 | 1 | 71 | +0 | cheap | 2.225 |
| [p34](positions/p34.5dpgn) | [706.5dpgn](../../pgn/online/706.5dpgn) | 51 | 9 | black | 2 | 2 | 54 | +1 | cheap | 1.010 |
| [p35](positions/p35.5dpgn) | [621.5dpgn](../../pgn/online/621.5dpgn) | 20 | 15 | white | 2 | 2 | 24 | -1 | moderate | 2.445 |
| [p36](positions/p36.5dpgn) | [166.5dpgn](../../pgn/online/166.5dpgn) | 10 | 25 | white | 2 | 2 | 13 | +1 | moderate | 2.449 |
| [p37](positions/p37.5dpgn) | [744.5dpgn](../../pgn/online/744.5dpgn) | 26 | 19 | white | 1 | 1 | 28 | +0 | moderate | 2.459 |
| [p38](positions/p38.5dpgn) | [269.5dpgn](../../pgn/online/269.5dpgn) | 68 | 12 | white | 2 | 3 | 78 | +0 | moderate | 2.597 |
| [p39](positions/p39.5dpgn) | [625.5dpgn](../../pgn/online/625.5dpgn) | 44 | 9 | white | 3 | 4 | 49 | +1 | moderate | 2.693 |
| [p40](positions/p40.5dpgn) | [519.5dpgn](../../pgn/online/519.5dpgn) | 90 | 9 | white | 3 | 5 | 102 | +0 | moderate | 2.783 |
| [p41](positions/p41.5dpgn) | [493.5dpgn](../../pgn/online/493.5dpgn) | 84 | 9 | white | 3 | 3 | 88 | +2 | moderate | 3.020 |
| [p42](positions/p42.5dpgn) | [433.5dpgn](../../pgn/online/433.5dpgn) | 56 | 8 | white | 3 | 4 | 64 | -1 | moderate | 3.046 |
| [p43](positions/p43.5dpgn) | [541.5dpgn](../../pgn/online/541.5dpgn) | 28 | 19 | white | 5 | 5 | 40 | +4 | moderate | 3.332 |
| [p44](positions/p44.5dpgn) | [109.5dpgn](../../pgn/online/109.5dpgn) | 33 | 11 | black | 3 | 3 | 38 | +0 | moderate | 2.425 |
| [p45](positions/p45.5dpgn) | [213.5dpgn](../../pgn/online/213.5dpgn) | 69 | 11 | black | 3 | 3 | 73 | +2 | moderate | 2.470 |
| [p46](positions/p46.5dpgn) | [399.5dpgn](../../pgn/online/399.5dpgn) | 61 | 8 | black | 2 | 2 | 67 | -1 | moderate | 2.508 |
| [p47](positions/p47.5dpgn) | [528.5dpgn](../../pgn/online/528.5dpgn) | 65 | 14 | black | 2 | 3 | 75 | +0 | moderate | 2.637 |
| [p48](positions/p48.5dpgn) | [376.5dpgn](../../pgn/online/376.5dpgn) | 69 | 10 | black | 3 | 3 | 74 | +0 | moderate | 2.645 |
| [p49](positions/p49.5dpgn) | [349.5dpgn](../../pgn/online/349.5dpgn) | 75 | 16 | black | 4 | 4 | 84 | -1 | moderate | 2.856 |
| [p50](positions/p50.5dpgn) | [432.5dpgn](../../pgn/online/432.5dpgn) | 49 | 18 | black | 1 | 2 | 52 | -1 | moderate | 2.978 |
| [p51](positions/p51.5dpgn) | [003.5dpgn](../../pgn/online/003.5dpgn) | 45 | 11 | black | 3 | 4 | 61 | -1 | moderate | 3.669 |
| [p52](positions/p52.5dpgn) | [418.5dpgn](../../pgn/online/418.5dpgn) | 17 | 12 | black | 4 | 4 | 22 | +3 | moderate | 3.736 |
| [p53](positions/p53.5dpgn) | [352.5dpgn](../../pgn/online/352.5dpgn) | 56 | 8 | white | 3 | 6 | 86 | -3 | expensive | 4.842 |
| [p54](positions/p54.5dpgn) | [550.5dpgn](../../pgn/online/550.5dpgn) | 34 | 11 | white | 1 | 2 | 37 | -1 | expensive | 4.898 |
| [p55](positions/p55.5dpgn) | [229.5dpgn](../../pgn/online/229.5dpgn) | 28 | 10 | white | 3 | 4 | 38 | -1 | expensive | 5.121 |
| [p56](positions/p56.5dpgn) | [573.5dpgn](../../pgn/online/573.5dpgn) | 36 | 11 | white | 2 | 2 | 43 | +1 | expensive | 5.401 |
| [p57](positions/p57.5dpgn) | [776.5dpgn](../../pgn/online/776.5dpgn) | 29 | 14 | black | 4 | 4 | 45 | +1 | expensive | 4.872 |
| [p58](positions/p58.5dpgn) | [330.5dpgn](../../pgn/online/330.5dpgn) | 17 | 15 | black | 1 | 2 | 21 | +1 | expensive | 4.943 |
| [p59](positions/p59.5dpgn) | [208.5dpgn](../../pgn/online/208.5dpgn) | 29 | 11 | black | 3 | 4 | 35 | -1 | expensive | 5.060 |
| [p60](positions/p60.5dpgn) | [678.5dpgn](../../pgn/online/678.5dpgn) | 63 | 33 | black | 3 | 4 | 71 | -1 | expensive | 5.075 |
| [p61](positions/p61.5dpgn) | [034.5dpgn](../../pgn/online/034.5dpgn) | 58 | 8 | white | 7 | 9 | 76 | +0 | stress | 19.259 |
| [p62](positions/p62.5dpgn) | [312.5dpgn](../../pgn/online/312.5dpgn) | 18 | 12 | white | 2 | 3 | 22 | +0 | stress | 13.614 |
| [p63](positions/p63.5dpgn) | [531.5dpgn](../../pgn/online/531.5dpgn) | 69 | 16 | black | 7 | 8 | 145 | -1 | stress | 26.827 |
| [p64](positions/p64.5dpgn) | [296.5dpgn](../../pgn/online/296.5dpgn) | 61 | 15 | black | 1 | 2 | 64 | -1 | stress | 9.103 |
