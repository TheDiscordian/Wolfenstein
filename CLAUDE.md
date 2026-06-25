# CLAUDE.md — Wolfenstein / Blake Stone openFPGA port

> Detailed working notes live in [`notes/blakestone/`](notes/blakestone/):
> [white-lines](notes/blakestone/white-lines.md), [perf](notes/blakestone/perf.md),
> [build-test](notes/blakestone/build-test.md). This file is the short current-state
> index; the depth is there.

## ⚠ White lines — read before changing the renderer

White/garbage lines across the top rows of the 3D view, on device only.

The 3D view is drawn by the hardware GPU into SDRAM the CPU framebuffer shares
through a cache. It corrupts when a change alters CPU writes into the 3D-view
region, the GPU-vs-CPU split of what draws where, the draw order, or the cache
handling — without keeping the coherency protocol
(`OF_WolfGPU_PrepareForCPUAccessRect/Column`, the
`OF_WolfGPU_EndFrameStatusBar(viewY0,viewY1)` boundary, the
`SetNextVideoFramePreserveExcludeRows` preserve list). Mechanism detailed at the
top of `src/wolfenstein/of_ecwolf_gpu.h`.

**Root cause + fix (device-confirmed, 2026-06-25).** The lines were a
**dirty-cache writeback over the view band**: the acquire preserve-copy leaves the
view band `[viewscreeny,viewscreeny+viewheight)` un-copied (the GPU redraws it) and
`EndFrameStatusBar` scopes its cache-invalidate to head/tail, skipping that band —
so stale dirty CPU cache lines over it, left from the buffer's previous use, could
evict and write back to SDRAM *after* the GPU rendered, clobbering its pixels. Fix:
invalidate the skipped view band at acquire (`gpu_acquire_video_draw_buffer`),
before the GPU draws. On-device probes nailed it — during the corruption the GPU
was fully drained (`gbf=0`) and the CPU made no tracked view-band write (`vbd=0`),
and both a cache-invalidate and a flush of that band killed the lines. Probe fields
+ read path: [notes/blakestone/](notes/blakestone/).

Rule:
- On PC every `OF_WolfGPU_*` is a no-op stub, so the GPU/cache path never runs —
  a clean PC render does **not** verify a renderer change, only the CPU fallback.
- Any change to framebuffer writes, the GPU/CPU draw split, draw order, or cache
  handling in the 3D-view region (status bar, floor/ceiling, sprites, overlays,
  GPU backdrop) must be tested on the Pocket before it's ready, on its own
  revertable commit.

Danger zones: `g_blake/blake_sbar.cpp` (status-bar cache), `wl_floorceiling.cpp`
(GPU backdrop), `r_sprites.cpp` (sprite columns).

## Device perf measurement

Loop: enable logging, tester plays, read the log.

1. `make clean CORE=<core>` then `make build CORE=<core> PERF=1` — `make` ignores
   a flag-only change and reuses cached objects, so the clean is required. Verify
   with `strings <elf> | grep 'fr=%u ev=%u'`.
2. Deploy; the tester plays (`OF_AUTOWALK` is env-gated, so device play is manual).
3. The bootlog uses slot 19, which is also save slot 9, so the perf lines land in
   `Saves/<core>/common/<Profile>_9.sav` (e.g. `AliensOfGold_9.sav`). Read with
   `strings <that file> | grep 'fr='`. A PERF build overwrites save slot 9, so
   never ship `PERF=1`.
   Fields: `fl=` floorceil µs, `flb=` halves left to CPU (0 = both on GPU),
   `flg=` frames the backdrop engaged, `wl=`/`wld=` wall µs.

## Build / deploy

```bash
make build CORE=blakestone        # device ELF -> build/blakestone/
make copy  CORE=blakestone        # deploy to a mounted Pocket SD card
make test  CORE=blakestone        # PC build (app_pc); GPU path stubbed
```

Verify the on-card ELF after `make copy` (`cmp` / `sha256sum`).

## Branches

All Blake Stone work goes on **`blake-union`** — the one shippable branch. Do
**not** spin up extra feature branches per change; the branch proliferation is
confusing, so commit directly to `blake-union` (this overrides the generic
gh-workspace "branch from main, PR" flow for this repo). Discordian opens upstream
PRs himself.
