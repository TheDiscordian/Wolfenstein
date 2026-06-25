# White lines — device GPU/cache corruption ⚠️

White/garbage lines across the **top rows of the 3D view**, persistent once
triggered, **device-only**. PC can never reproduce it: every `OF_WolfGPU_*` is a
no-op stub on PC, so the GPU/cache path doesn't run. A clean PC render therefore
does **not** verify a renderer change — only the CPU fallback.

> **Rule:** any change to framebuffer writes, the GPU/CPU draw split, draw order,
> or cache handling in the 3D-view region (status bar, floor/ceiling, sprites,
> overlays, GPU backdrop) must be tested on the Pocket before it's called ready,
> and must land as its own revertable commit — never bundled into the shippable line.

## The coherency invariant

The 3D view is rasterised by the hardware GPU into SDRAM that the CPU framebuffer
shares through a cache. The view band is `[viewscreeny, viewscreeny+viewheight)`.
Correctness depends on three pieces staying consistent:

- `OF_WolfGPU_PrepareForCPUAccessRect/Column` — drain + invalidate before the CPU
  reads/writes GPU-written pixels.
- `OF_WolfGPU_EndFrameStatusBar(viewY0, viewY1)` — the frame-end boundary;
  invalidates only the head `[0,viewY0)` and tail `[viewY1,height)` bands (the
  CPU-written status-bar/border rows), leaving the view band GPU-resident.
- `OF_WolfGPU_SetNextVideoFramePreserveExcludeRows(y0, y1)` — the acquire-time
  preserve copy carries head `[0,y0)` + tail `[y1,height)` from the last frame and
  skips `[y0,y1)` because the renderer redraws them.

Full mechanism: top of `src/wolfenstein/of_ecwolf_gpu.h`.

Danger zones: `g_blake/blake_sbar.cpp` (status-bar cache), `wl_floorceiling.cpp`
(GPU backdrop), `r_sprites.cpp` (sprite columns).

## Ruled out (with evidence)

- **CPU memory bug — NO.** valgrind memcheck is clean (0 errors) over ~280
  autowalk frames of MAP01 (2026-06-24), on top of an earlier ASan-clean run. No
  uninitialised read, OOB, or invalid access anywhere in the CPU render/sim path.
  (The valgrind run needs a generic-glibc container — see [build-test.md](build-test.md).)
- **Static preserve/invalidate range mismatch — NO.**
  `SetNextVideoFramePreserveExcludeRows(viewscreeny, viewscreeny+viewheight)`
  (wl_draw.cpp:1592) and `EndFrameStatusBar(viewscreeny, viewscreeny+viewheight)`
  (wl_play.cpp:1338) pass the identical view band. Head/tail ranges agree exactly.
- **GPU write-drain race — NO** (RTL + **device probe, during actual white
  lines**): `CMD_FENCE` stalls until `m_wr_inflight == 0`; and the probe read
  `gbf=0 gst=2` (ring-empty, no busy/DMA) *while the lines were on screen* — the
  GPU is fully drained when `EndFrameStatusBar` publishes.
- **CPU writes into the view band — NO** (device probe): `vbd=0` during the same
  white-lines run — the CPU never wrote a line inside `[viewscreeny,viewscreeny+viewheight)`.
- **Per-half floor/ceiling backdrop, and PERF-build alone — NO**: white lines
  appeared without either.

## Root cause (CONFIRMED 2026-06-25) + fix

**Dirty-cache writeback over the view band.** Fence drained (`gbf=0`), no tracked
CPU view-band writes (`vbd=0`), CPU path valgrind-clean — all *during* the
corruption — so it's not a race, a stray CPU write, or a CPU memory bug. The
mechanism: the acquire preserve-copy leaves the view band
`[viewscreeny,viewscreeny+viewheight)` un-copied (the GPU redraws it), and
`EndFrameStatusBar` scopes its cache-invalidate to head/tail and **skips** the view
band (the "~35% optimization"). So stale **dirty CPU cache lines** over that band,
left from this buffer's previous use, evict and write back to SDRAM *after* the GPU
rendered — clobbering its pixels = the white lines.

**Fix:** invalidate the skipped view band's CPU cache at acquire
(`gpu_acquire_video_draw_buffer`), before the GPU draws, so nothing stale can write
back over it (`of_cache_inval_range`, row-pitch aligned).

**How it was confirmed (two device tests, deterministic per build):** a PERF-gated
black-fill of the band (memset + flush) killed the lines; then the same minus the
memset (invalidate only, no colour change) *also* killed them — isolating the
**cache op**, not the pixels. The lines are persistent in the 3D view, not
intermittent; presence tracked the build.

## Known triggers (layout-sensitive)

Both reproduced and then removed; the underlying device mechanism is unverified —
**don't fabricate one**:

- The info-area icon folding `iconFrame` into the status-bar cache key (`dcf38ff`)
  — busting the bar cache every walk-frame forced full-bar redraws that corrupted
  the top view rows. Fixed by drawing the icon live every frame off the cache key.
- The raycast plane-base hoist (`bdcac32`) — render-identical and a device perf
  no-op, yet still triggered persistent white lines. Reverted (`c336e11`).

These say the corruption is sensitive to exactly what the GPU dispatch writes and
in what order — i.e. it lives in the GPU/cache seam, consistent with the
valgrind-clean CPU path.

## Current state

- **Fix landed** (acquire-time view-band cache invalidate) on `blake-union`,
  non-PERF — see the Root cause section above. The invalidate is device-confirmed
  (the PERF isolation build with it ran clean); the shippable non-PERF build is the
  final check.
- **Diagnostic probes retained** (PERF-only, never ship): `OF_WolfGPU_EndFrameStatusBar`
  samples `GPU_STATUS` after the `of_gpu_finish()` fence drain into perf-line fields
  `gbf=` (frames the GPU still reported busy/DMA after the fence) and `gst=` (OR of
  the status bits — bit0 busy, bit1 ring-empty, bit2 DMA-busy); `vbd=` counts
  CPU-dirty lines that landed inside the view band. There is no `0x34 WR_INFLIGHT`
  register — the real interface is `of_gpu_debug_snapshot()` / `GPU_STATUS`. To use:
  `PERF=1` build, play, read slot-9 (`strings AliensOfGold_9.sav | grep 'fr='`).
  If the lines ever return, `gbf>0`+bit2 = GPU-drain, `vbd>0` = a CPU view-band
  write, both `0` = back to the dirty-cache seam.
