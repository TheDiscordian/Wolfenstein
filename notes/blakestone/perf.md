# Performance — device breakdown + measurement 📊

## Device breakdown (captured 2026-06-23)

Frame `fr` ≈ 42 ms (~23–24 fps). Dominant cost is walls:

- `wl` ≈ 16–18 ms = raycast ~10–11 ms (CPU, **cache-miss bound** on the fat Map
  struct) + GPU column dispatch `wld` ≈ 7 ms.
- `fl` ≈ 1 ms when the GPU floor/ceiling backdrop engages (`flb=0` = both halves
  on GPU).
- `th` ≈ 5.5 ms think.

The two remaining wall levers — cache-bound map reads and the 7 ms GPU dispatch —
are **not PC-iterable**. PC perf does not predict device perf when the device is
cache-bound: the raycast plane-base hoist saved ~15% on PC WALLPROF but was a
device no-op (~3.5 µs/step both ways), because the device raycast is bound by the
Map-struct cache miss the hoist doesn't touch. PC is valid for **correctness**
only. PC-iterable CPU perf wins are considered exhausted.

## Device PERF build + bootlog read flow

The loop is: I build + deploy, the tester plays, I read the log.

1. `make clean CORE=blakestone` then `make build CORE=blakestone PERF=1`. The clean
   is required — `make` ignores a flag-only change and reuses cached objects.
   Verify the build took: `strings <elf> | grep 'fr=%u ev=%u'`.
2. Deploy (`make copy`). The tester plays normally (`OF_AUTOWALK` is env-gated, so
   device play is manual — never ask them to "capture a perf line", just to play).
3. Read the log. The bootlog registers slot 19, which **collides with save slot 9**,
   so the perf lines land inside `Saves/blakestone/common/AliensOfGold_9.sav` (there
   is no `ofbootlog.sav` on the card). Read with
   `strings Saves/blakestone/common/AliensOfGold_9.sav | grep 'fr='`. Slot 9 is the
   intentional log slot — not a save the user keeps, so it isn't a "clobbered save".

A PERF build overwrites slot 9, so **never ship `PERF=1`**.

Perf-line fields: `fl=` floorceil µs, `flb=` halves left to CPU (0 = both GPU,
1 ceiling, 2 floor, 3 both), `flg=` frames the backdrop engaged, `wl=`/`wld=`
wall µs (total / GPU-dispatch). White-lines probe: `gbf=` count of frames where
`GPU_STATUS` still showed busy/DMA right after the `EndFrameStatusBar` fence
drain, `gst=` OR of the status bits seen there (bit0 busy, bit1 ring-empty,
bit2 DMA-busy). `gbf=0` every window = the fence is honouring write-commit (probe
clears the GPU-drain hypothesis); `gbf>0` with bit2 set in `gst` = residual GPU
writes at publish = white-lines smoking gun.

To check whether a core actually *ran* (vs was merely deployed), read the card
mtimes: `Saves/<core>/`, `System/lastcore.bin`, `recent.bin`, Browser MRU
`*.<Core>.bin`.

## PC profiling harness (CPU-side only)

`make test CORE=blakestone PERF=1`, then run with `OF_LUMPDUMP=1 OF_AUTOWALK=1`.
WALLPROF (walls total / draw / raycast / posts / steps) and SIMPROF (collide /
think) print to stderr every 70 frames. Good for A/B-ing CPU-side raycast/think
changes — but it only measures the CPU raycast/think, never the device GPU
dispatch or cache effects. `OF_AUTOWALK` drives the player only when
`OF_ECWOLF_PERF` is defined (i.e. PERF=1); under plain `make test` the player
never moves.
