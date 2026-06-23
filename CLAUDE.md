# CLAUDE.md — Wolfenstein / Blake Stone openFPGA port

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

`blake-union` is the shippable Blake Stone branch; `blake-bootlog` holds
perf-diagnostic work. Ryan opens upstream PRs himself.
