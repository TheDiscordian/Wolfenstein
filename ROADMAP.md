# Roadmap

## 1.0 — Blake Stone: Aliens of Gold + Planet Strike

The shipping target is a faithful port of the **DOS original** Blake Stone on the
Pocket — **both** Aliens of Gold (all six episodes) **and** Planet Strike. The
core ships both as selectable games (`Assets/blakestone/.../Aliens of Gold.json`
and `Planet Strike.json`, `.BS6` and `.VSI` data sets). Covered: the LINC info
area, interrogation/informant system, barriers, elevators, briefings,
score/pinball bonuses, save/load, and OPL music.

**Reference vs target:** the DOS game is authoritative. `bstone` is an
open-source reimplementation used only as a *reference* for behaviour and
constants because it closely tracks DOS — where `bstone` diverges from DOS, DOS
wins. "Parity" below means DOS faithfulness (bstone-referenced), not matching
bstone for its own sake.

## Audit backlog (2026-06-26)

A multi-agent audit of the port vs `bstone` surfaced 26 verified gaps. These were
confirmed against bstone; **each still needs a DOS check before fixing** (bstone
is a reference, not the target — see above), but most are core DOS Blake Stone
behaviour. Ordered roughly by impact.

**Fixed (11):** `^AN` ghost/smear, liquid-alien submerge-when-unseen,
OVERALL MISSION baseline (300→100), FLOOR:/AREA: + secret-floor labels,
floating-bomb detonation, volatile-transport explosion + ooze, steam-grate
on-screen gating, lose-screen terminal sounds, plasma-detonator info-area icons,
alien shoot/move-mode firing cadence (`+SHOOTMODEAI`). Remaining items below are
the larger/riskier ones (renderer, spawn system, xlat, DamageActor).

### Functional
- **PS electro-alien spawning walls inert** — tile-24 walls never emit ElectroAliens; the `0xFA` spawn byte is discarded in `gamemap_planes.cpp`, `CheckSpawnEA` missing from `wl_play.cpp` PlayLoop.
- **PS in-game radar minimap unimplemented** — overhead map + energy drain + zoom; `blake_sbar.cpp:902-912`.
- **PS arc barrier can't be shot down with the anti-plasma cannon** (BFG shutdown); `blakebarriers.txt:161-191`.

### Behavioural — enemies
- **STAR Trooper / Alien Protector wound-knockdown** dead code — no trigger in DamageActor, `T_SwatWound` commented out; `blakemonsters.txt:261-268,318-325`.
- **PS enemy cloaking** not rendered (`FL2_CLOAKED`/`FL2_DAMAGE_CLOAK` fuzz branch); `wl_draw.cpp:768-855`, `r_sprites.cpp`.

### Behavioural — skill gating (`planet.txt`)
- Skill-gated canister/gurney wake-ups, POD egg, and morph posts spawn **nothing** below skill instead of bstone's inert static decoration; `planet.txt:1068-1088,1136-1148,1340-1358`.

### Behavioural — presenter / HUD / LINC
- **AOG elevator arrival** doesn't run AlignPlayerInElevator (player spawns on the elevator tile); `blake_elevator.cpp:1269-1292`.
- **Informant near-100% enemy/treasure location report** unimplemented; `blake_informant.cpp:159-192`.
- **First-time QUICK_INFO instructions** never shown on a new AoG game; `wl_play.cpp`.
- **JAM secret cheat** missing; `wl_play.cpp` CheckKeys.
- `anim_bgcolor` snapshot/swap around `^EP` omitted; `^SH` width hardcoded 0 in centering pass. (minor; `jm_tp.cpp`)

### Cosmetic
- Elevator radar omits hidden-area shading.

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

### bstone parity — done

- **STORY / ORDERING menus + the Lose-screen text overlay** wired through
  `TP_Presenter` (`Blake_ShowStory` / `Blake_ShowOrdering` / `Blake_ShowLoseScreen`,
  `READ THIS!` submenu).
- **Fluid Alien AI** — `A_LiquidStand` (`a_liquid.cpp`) ports `T_LiquidStand`: rise
  solid/shootable, fire up to five times (80/255 each), submerge on the 40/255 roll
  or after the fifth shot.
- **Ceiling turret** — `A_TurretSeek` (`a_turret.cpp`) ports `T_Seek`: tracks the
  player in line of sight within fifteen tiles and opens fire on a distance-weighted
  chance (was facing forever without shooting).
- **Security light** — `A_SecurityLook` (`a_security.cpp`) ports `T_Security`'s
  area-presence trip (`map->CheckLink` over sound zones); the old `A_Look` had an
  empty sight window.
- **Floating bomb / volatile transport** — collapsed to the single W1 row bstone
  actually draws (`temp1 + SPR_DEMO`, with the engine adding rotations); the B-D
  damage-tier frames are never selected.
- **Instructions screen + `^AN` animations** — the `READ THIS!` -> INSTRUCTIONS
  menu now presents HELPART through `TP_Presenter` (`Blake_ShowInstructions`)
  instead of ECWolf's `ShowArticle`, which couldn't read the Blake presenter
  codes.  `TP_AnimatePage` (`jm_tp.cpp`) ports bstone's animation stepping, and
  `anShapeTable` maps each `^AN` shape index to its port texture, with
  `anShapeAlt` covering the three slots PS reuses for its own enemies (Sector
  Guard / Tech Warrior / Alien Protector vs AOG's Sector Patrol / STAR Sentinel /
  STAR Trooper).  Verified: both AOG and PS resolve every used `^AN` shape, and
  the screen renders through the presenter; the live sprite animation is
  confirmable on-device (the headless harness can't drive the presenter's input
  to page to those screens).

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

- **`^AN` animated pages** (`jm_tp.cpp`; `TP_AnimatePage` is a no-op) — static
  `^SH` shapes (briefing location pics `M_EPIS1`–`6`, the Mission-6 generator icon)
  are drawn; the `^AN` *animation* opcode is parsed-and-skipped. `^AN` appears only
  on the instructions / character-profile page (`VGAGRAPH` chunk 201 in AOG, 226 in
  PS — the `READ THIS!` → INSTRUCTIONS screen), which showcases enemy walk loops and
  control diagrams. Porting it means the runtime stepping (`TP_AnimatePage`: advance
  frame with cycle/rebound, redraw via `TP_DrawShape`) plus `piAnimTable` and a
  `piShapeTable` slice — both of which differ between AOG and PS, and whose sprite
  entries map to port textures by VSWAP-sprite identity, not by bstone's `SPR_`
  index (the port's `bs6map`/`vsimap` order does not match bstone's enum). Each
  shape needs cross-referencing per version; getting an index wrong draws the wrong
  sprite, so it wants visual verification on both games.
