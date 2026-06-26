# Roadmap

## 1.0 — Blake Stone: Aliens of Gold + Planet Strike

The shipping target is a faithful, full-game Blake Stone on the Pocket — **both**
Aliens of Gold (all six episodes) **and** Planet Strike. The core ships both as
selectable games (`Assets/blakestone/.../Aliens of Gold.json` and
`Planet Strike.json`, `.BS6` and `.VSI` data sets). Covered: the LINC info area,
interrogation/informant system, barriers, elevators, briefings, score/pinball
bonuses, save/load, and OPL music.

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

- **STORY / ORDERING menus + the Lose-screen text overlay.** bstone's `LoseScreen`
  (`3d_game.cpp:3247`), `CP_BlakeStoneSaga` and `CP_OrderingInfo`
  (`3d_menu.cpp:1958`) draw a background art page *and* present a text chunk. The
  text is present and named: decoding `VGAGRAPH.BS6` shows the lumps `bs6map.txt`
  calls `SAGAART` / `LOSEART` / `ORDERART` actually hold the Saga, Lose ("REBA:
  INCOMING TRANSMISSION") and Ordering presenter scripts. So these just need
  wiring through `TP_Presenter` (as the briefings already are) — the Lose overlay
  over the existing `LOSEPIC`, and two new menu entries. Not blocked.
- **Fluid Alien AI** (`LIQ` sprites, `blakemonsters.txt:1752`) — the puddle alien's
  rise/attack/submerge cycle is present but approximated: the actor's own comments
  flag the missing exact gating ("should rise if the player is >1 block away in
  both directions but <6 in either", the 40/255 fall and 80/255 attack chances,
  the 5-attack counter). bstone's `T_LiquidStand` logic; likely needs a native
  action to match precisely.

### Planet Strike

Planet Strike ships in 1.0 (`.VSI` data, `planet.txt`, `a_electrosphere`,
`a_detonator`). A verified port-vs-bstone audit confirmed 11 gaps, all now fixed:
the showstopper (boss death wins the game), the anti-plasma explosion and rip
behaviour, the electrosphere device speed and its roam/death/pain animation rates,
the morph halt sound, the morphed enemy waking into the chase, the morph trigger
(on-screen `FL_VISIBLE` timer instead of line-of-sight), the Goldfire-morph weapon
lock, and the radar-pak top-band refusal.

One refinement remains: the morph posts use a fixed ~2s on-screen delay rather
than bstone's per-post `scan_value` — that value is a `0xfa`-prefixed byte in the
DOS map info plane (3d_game.cpp:299) that the port's tile→thing xlat discards.
Recovering it would mean modelling bstone's info-plane scan order in the map
loader; the current fixed delay is already correct on the trigger, where the
previous port was not.

### Text presenter

- **`^AN` animated pages** (`jm_tp.cpp:1348`; `TP_AnimatePage` is a no-op) — static
  `^SH` shapes (briefing location pics `M_EPIS1`–`6`, the Mission-6 generator icon)
  are drawn; the `^AN` *animation* opcode is parsed-and-skipped. Per the source
  comment `^AN` drives the intro / enemy-showcase pages, not the briefings.
