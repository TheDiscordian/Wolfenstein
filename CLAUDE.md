# CLAUDE.md — Wolfenstein / Blake Stone openFPGA port

Read this before changing the renderer. It exists because the same bug keeps
getting reintroduced.

## ⚠ White lines (the recurring renderer-corruption bug) — READ FIRST

**Symptom:** white/garbage lines across the top rows of the 3D view, on device,
while playing (often while moving). Never on PC.

**Cause:** the 3D view is drawn by the hardware GPU into SDRAM that the CPU
framebuffer shares through a cache. Corruption happens when a change alters
**CPU writes into the 3D-view region**, the **GPU-vs-CPU split** of what draws
where, the **order** they draw in, or the **cache-flush handling** — without
keeping the coherency protocol (`OF_WolfGPU_PrepareForCPUAccessRect/Column`, the
`OF_WolfGPU_EndFrameStatusBar(viewY0,viewY1)` boundary, the
`SetNextVideoFramePreserveExcludeRows` preserve list). Full details and the
two regressions are documented at the top of `src/wolfenstein/of_ecwolf_gpu.h`.

**The rule that prevents it:**

1. **White lines are DEVICE-ONLY.** On PC every `OF_WolfGPU_*` is a no-op stub
   returning false — the GPU/cache path never runs. **A clean PC render does NOT
   verify a renderer change.** It only confirms the CPU fallback still works.
2. Any change to framebuffer writes, GPU/CPU draw split, draw ordering, or cache
   handling in the 3D-view region (status bar, floor/ceiling, sprites, overlays,
   the GPU backdrop) **must be tested on the Pocket before it is called ready**,
   and **must be its own easily-revertable commit** — never bundled into the
   shippable line.
3. If it can't be device-tested yet, it is **not ready** — say that plainly
   rather than presenting a clean PC render as done.

Danger zones that have caused it: `g_blake/blake_sbar.cpp` (status-bar cache),
`wl_floorceiling.cpp` (GPU backdrop), `r_sprites.cpp` (sprite columns).

## Device perf measurement flow

Perf logging is opt-in and the loop is: **I enable it, the tester plays, I read
the log** — never ask the tester to capture anything.

1. `make build CORE=<core> PERF=1` — but `make` does **not** notice a flag-only
   change, so it reuses cached objects (identical binary). Always
   `make clean CORE=<core>` first, then verify with
   `strings <elf> | grep 'fr=%u ev=%u'` (present == instrumented).
2. Deploy; the tester plays (`OF_AUTOWALK` is env-gated, so device play is
   manual/normal).
3. **Reading the log — IMPORTANT, this is easy to forget.** The bootlog
   registers **slot 19**, which collides with save slot 9 (`savegam9`), so the
   perf lines land *inside that save's backing file*:
   `Saves/<core>/common/<Profile>_9.sav` (e.g. `Saves/blakestone/common/AliensOfGold_9.sav`).
   There is **no file named `ofbootlog.sav` on the card** — do not search for one
   (that wasted an entire session). Read it with:
   `strings Saves/blakestone/common/AliensOfGold_9.sav | grep 'fr='`
   The collision also means a PERF build clobbers the player's save slot 9 — fine
   for throwaway measurement builds, but never ship `PERF=1`.
   Key fields: `fl=` floorceil µs, `flb=` halves left to CPU (0 = both on the GPU
   backdrop), `flg=` frames the backdrop engaged, `wl=`/`wld=` wall µs.

## Build / deploy

```bash
make build CORE=blakestone        # device ELF -> build/blakestone/
make copy  CORE=blakestone        # deploy to a mounted Pocket SD card
make test  CORE=blakestone        # PC build (app_pc); GPU path is stubbed
```

After `make copy`, verify the on-card ELF matches
(`cmp` / `sha256sum`) — the SD card has unmounted mid-copy before.

## Branches

`blake-union` is the Blake Stone integration branch (the shippable line).
`blake-bootlog` holds perf-diagnostic work. Ryan opens any upstream PRs himself.
