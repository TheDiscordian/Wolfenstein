# Roadmap

## 1.0 — Blake Stone: Aliens of Gold

The first shipping target is a faithful, full-game Blake Stone: Aliens of Gold
on the Pocket: all six episodes, the LINC info area, interrogation/informant
system, barriers, elevators, briefings, score/pinball bonuses, save/load, and
OPL music. This is feature-complete and is what 1.0 ships.

## After 1.0

### Multiplayer

Local split-screen multiplayer (co-op and deathmatch) through the Pocket dock,
which already drives multiple controllers (see the dock controls in the
README). The engine carries ECWolf's netplay/split-screen scaffolding
(`Net::`, `ClearSplitVWB`), so the remaining work is wiring it up and finishing
the pieces that are stubbed for single-player today:

- player-to-player collision response (`wl_agent.cpp` — players currently clip
  through each other)
- damage attribution when the attacker/target is another player
  (`wl_state.cpp`)
- split-screen viewport plumbing and per-player input routing

**This lands after 1.0 ships, not before.**

## Backlog (post-1.0, unscheduled)

Known limitations and polish, none of them shipping blockers:

- **Floor/ceiling GPU offload** — the floor still renders on a per-pixel CPU
  walk on maps with mixed flats (the common Blake case); moving it fully onto
  the GPU span path is the next big framerate win. In progress.
- **Sprite near-plane clipping** — sprites straddling the view plane are culled
  whole rather than clipped (inherited from Wolf4SDL/ECWolf; matches the DOS
  engine's behaviour).
- **Per-column sprite shading** — sprites shade by one distance for the whole
  sprite rather than per column.
- **Briefing `^AN` animation / shape embedding** — the Text Presenter renders
  the mission-briefing location pictures but not animated `^AN` pages (used
  only by intro/showcase screens, not the shipped AOG briefings).
