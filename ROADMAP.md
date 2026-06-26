# Roadmap

## 1.0 — Blake Stone: Aliens of Gold

The first shipping target is a faithful, full-game Blake Stone: Aliens of Gold
on the Pocket: all six episodes, the LINC info area, interrogation/informant
system, barriers, elevators, briefings, score/pinball bonuses, save/load, and
OPL music. This is feature-complete and is what 1.0 ships.

## Out of scope for 1.0

### Multiplayer

**Multiplayer is out of scope for 1.0 — not scheduled.** Local split-screen
(co-op and deathmatch) through the Pocket dock is only a possible future
direction. The engine carries ECWolf's netplay/split-screen scaffolding (`Net::`,
`ClearSplitVWB`); if it's ever picked up, the stubbed-for-single-player pieces
are player-to-player collision (`wl_agent.cpp`), damage attribution between
players (`wl_state.cpp`), and split-screen viewport/input routing. None of this
is 1.0 work.

## Backlog (post-1.0, unscheduled)

Known limitations and polish, none of them shipping blockers.

### Renderer (upstream ECWolf scaler)

- **Sprite view-plane clipping** (`r_sprites.cpp:744`, `// TODO: Clip on viewplane`)
  — a sprite whose endpoints cross behind the view plane is culled whole (`return`)
  instead of clipped at the plane.
- **Per-column sprite shading** (`r_sprites.cpp:587`, `[XA] TODO`) — sprites shade
  by one distance (the sprite's) for the whole sprite rather than per screen column.

### bstone parity — remaining

- **Story / Ordering / Lose text overlays — blocked on VGAGRAPH chunk naming.**
  bstone's `LoseScreen` (`3d_game.cpp:3247`), `CP_BlakeStoneSaga` and
  `CP_OrderingInfo` (`3d_menu.cpp:1958`) draw a full-screen art page *and* present
  a text chunk (`LOSETEXT` / `SAGATEXT` / `ORDERTEXT`). The art pages
  (`LOSEPIC`, `SAGAART`, `ORDERART`) are named in `bs6map.txt` and usable — the
  Lose screen already shows `LOSEPIC` — but the matching *text* chunks are not
  named in the VGAGRAPH map, so the overlays and the STORY/ORDERING menu entries
  can't be wired. Unblocking needs the bstone `GrChunk` enum positions confirmed
  against `bs6map.txt` so the names can be inserted without shifting the
  already-working briefing chunks (`BRIEFW`/`BRIEFI`, `bs6map.txt:60`).
- **Informant high-completion hint.** bstone gives a distinct informant hint near
  full level completion; the exact dispatch was not located in the bstone source
  during the parity audit. Needs the reference pinned (`3d_act1.cpp` /
  `3d_state.cpp`) before porting — the port's per-room hint system
  (`blake_informant.cpp`) is otherwise in place.
- **Fluid Alien AI** (`LIQ` sprites) — the puddle alien's rise / sink / submerge
  behaviour is simplified relative to bstone. Cosmetic.

### Text presenter

- **`^AN` animated pages** (`jm_tp.cpp:1348`; `TP_AnimatePage` is a no-op) — static
  `^SH` shapes (briefing location pics `M_EPIS1`–`6`, the Mission-6 generator icon)
  are drawn; the `^AN` *animation* opcode is parsed-and-skipped. Per the source
  comment `^AN` drives the intro / enemy-showcase pages, not the AOG briefings.

Planet Strike content (morphing enemies, electro-alien generators) is a separate
game and not part of the AOG 1.0 target.
