# Build, deploy, test 🛠️

## Build / deploy

```bash
make build CORE=blakestone        # device ELF -> build/blakestone/
make copy  CORE=blakestone        # deploy to a mounted Pocket SD card
make test  CORE=blakestone        # PC build (app_pc); GPU path stubbed
```

Verify the on-card ELF after `make copy` (`cmp` / `sha256sum`). `make clean` does
**not** remove `.obj/`; a flag-only change (e.g. `PERF=1`) isn't detected, so wipe
the relevant objects or `make clean` first when toggling flags.

## Repo layout

One checkout, one Blake branch: work in `~/Programming/Wolfenstein` on
**`blake-union`** (origin `TheDiscordian/Wolfenstein`, upstream
`openfpgaOS/Wolfenstein` — never PR upstream). No `/tmp` worktrees. The Blake game
data (`*.BS6`/`*.VSI`, `ecwolf.pk3`) is tracked on `blake-union`. Toolchain:
`riscv64-elf-gcc` 15.2.0 + binutils.

## Headless PC test harness

- `OF_AUTOWALK` + the autowalk block in `wl_play.cpp` only compile under
  `OF_ECWOLF_PERF` (PERF=1) — under plain `make test` the player never moves.
- `OF_KEYSCRIPT="ms:keyname[:holdms],..."` (sdlvideo.cpp) injects key presses in
  headless `app_pc`.
- Default keys: forward = UpArrow (`bt_moveforward`), use = Space (`bt_use`).
- Native render is 320×200 (virtual == real).
- Use-key edge: the PC build fires `Cmd_Use` every tic the key is held; the device
  build gates on `!cmd.buttonheld[bt_use]` (one per press).

## Running valgrind (needs a container) 🐳

valgrind **can't run natively on this box**: it's CachyOS on Zen 4 with makepkg
`-march=native`, so the system `ld-linux` itself uses AVX-512 (EVEX `0x62`), and
valgrind 3.25.1's VEX SIGILLs in `_dl_start` before `main` on *any* dynamically
linked binary. `GLIBC_TUNABLES=glibc.cpu.hwcaps=-AVX512*` does not help (it's the
loader's own compiled code, not IFUNC selection). Run it inside a generic-glibc
container instead:

```bash
docker run --rm -v ~/Programming/Wolfenstein:/work -w /work ubuntu:24.04 bash -c '
  set -e
  apt-get update -qq
  apt-get install -y -qq build-essential pkg-config valgrind \
      libsdl2-dev libsdl2-mixer-dev libsdl2-net-dev
  cd /work/src/blakestone
  # host objects are native-march (AVX-512); reusing them re-triggers the SIGILL
  rm -rf /work/.obj/blakestone/pc /work/.obj/blakestone/sdk/pc
  rm -f  /work/.obj/blakestone/sdk/of_init.c.o app_pc
  make test CORE=blakestone PERF=1
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy OF_AUTOWALK=1 \
    timeout --signal=INT 220 \
    valgrind --tool=memcheck --track-origins=yes --num-callers=30 --error-limit=no \
    --log-file=/work/vg.log \
    ./app_pc --tedlevel MAP01 --baby --nowait > /work/vg-stdout.log 2>&1 || true
  grep -A1 "ERROR SUMMARY" /work/vg.log
'
```

The container runs as **root**, so the `.obj` objects and any logs it writes are
root-owned — chown or `rm` them via a throwaway root container afterward, or the
next host build can't overwrite them. (Add `OF_LUMPDUMP=1` to the run to get
WALLPROF frame counts on stderr, confirming the 3D path actually rendered — but
note it also exercises a benign debug `fprintf` in `FVGAGraph::Open` that memcheck
flags; the shipping path with `OF_LUMPDUMP` off is 0 errors.)
