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
- **GPU write-drain race — NO** (per RTL): `CMD_FENCE` stalls until
  `m_wr_inflight == 0`, so `of_gpu_finish()` provably commits all framebuffer
  writes before it returns.
- **Per-half floor/ceiling backdrop, and PERF-build alone — NO**: white lines
  appeared without either. See [history.md](history.md).

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

- **Shippable-clean:** `fa49c572` (icon-decouple + HUD secret-flag fix, no hoist,
  no per-half, non-PERF) — device-confirmed clean.
- **Next diagnostic — BUILT, awaiting a device run.** On-device GPU write-inflight
  probe: `OF_WolfGPU_EndFrameStatusBar` samples `GPU_STATUS` (0x14; bit0 busy,
  bit2 DMA_BUSY) read-only right after the `of_gpu_finish()` fence drain and
  accumulates two `OF_ECWOLF_PERF`-gated perf-line fields — `gbf=` (frames where
  the GPU still reported busy/DMA after the fence) and `gst=` (OR of the status
  bits seen there). There is no `0x34 WR_INFLIGHT` register; the real interface is
  `of_gpu_debug_snapshot()` / `GPU_STATUS`. Read-only, no framebuffer write, so it
  can't perturb the layout. To run: deploy a `PERF=1` build, play ~15 s, read the
  slot-9 save (`strings AliensOfGold_9.sav | grep 'fr='`). `gbf>0` with bit2 in
  `gst` = the GPU is still committing view-band pixels at publish (smoking gun);
  `gbf=0` clears the GPU-drain hypothesis and points further down the seam.
