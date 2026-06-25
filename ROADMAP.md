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

Known limitations and polish, none of them shipping blockers. Each is a real
TODO/stub in the source:

- **Sprite view-plane clipping** (`r_sprites.cpp:744`, `// TODO: Clip on viewplane`)
  — a sprite whose endpoints cross behind the view plane is culled whole (`return`)
  instead of clipped at the plane. Inherited from the ECWolf/Wolf4SDL scaler.
- **Per-column sprite shading** (`r_sprites.cpp:587`, `[XA] TODO`) — sprites shade
  by one distance (the sprite's) for the whole sprite rather than per screen
  column. An upstream ECWolf TODO.
- **`^AN` animated text-presenter pages** (`jm_tp.cpp:1348`; `TP_AnimatePage` is a
  no-op) — static `^SH` shapes (the per-mission briefing location pics `M_EPIS1`–`6`
  and the Mission-6 generator icon) are drawn; the `^AN` *animation* opcode is
  parsed-and-skipped. Per the source comment, `^AN` is used by the intro /
  enemy-showcase pages, not the AOG mission briefings.
- **Queued score bonus lost on save/reload** (`blake_sbar.cpp:360`) — cosmetic
  edge case.

(The floor/ceiling GPU offload that used to head this list is done — the per-half
GPU backdrop.)
