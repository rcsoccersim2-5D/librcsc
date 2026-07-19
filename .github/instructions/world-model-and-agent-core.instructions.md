---
applyTo: 'rcsc/player/**'
---

# World Model & Agent Core

## TL;DR
`WorldModel` (`world_model.cpp/.h`, 5820/1599 lines — the largest file in librcsc) reconstructs the agent's belief state each cycle from noisy partial `see`/`sense_body`/`hear` messages; `PlayerAgent` (`player_agent.cpp/.h`) is the per-cycle dispatch loop that feeds sensors into it and drives decision-making.
- `PlayerAgent::parse()` (player_agent.cpp:1448) routes raw server messages to `analyzeSee`/`analyzeSenseBody`/`analyzeHear`, each of which calls the matching `WorldModel::updateAfter*` method.
- `PlayerAgent::action()` (player_agent.cpp:2361) is the fixed per-cycle sequence: `updateJustBeforeDecision()` → `handleActionStart()` → `actionImpl()` (pure virtual, subclass hook) → arm/view/neck/focus actions → `communicationImpl()` → `updateJustAfterDecision()`.
- `WorldModel::updateJustBeforeDecision()` (world_model.cpp:1976) is the belief-fusion pipeline: hear-based corrections, collision checks, player-type/card refresh, offense/defense lines, `updateInterceptTable()` (world_model.cpp:5243, delegates to `InterceptTable::update`), offside line, kickable-state resolution.
- **Open the full file when:** touching localization math (`localizeSelf`/`localizeBall`/`localizePlayers`), interception logic, or any `update*` method not summarized here — this file has ~140 member functions and subtle ordering dependencies (comments explicitly warn "have to be called after ...").

## Overview
`WorldModel` is librcsc's belief-state engine — the analog of rcssserver's `stadium.cpp`/`referee.cpp` in architectural importance, but inverted: instead of authoritative simulation state, it holds probabilistic *reconstructions* of ball/player positions/velocities from limited, delayed, noisy sensory input (visual cone, sense_body, occasional fullstate/hear). Every higher-level librcsc/helios-base decision (formations, dribble, pass, intercept-based defense) reads from `WorldModel`, never from raw sensors directly.

`PlayerAgent` is the concrete `SoccerAgent` subclass (see `client-framework.instructions.md`) that owns a `WorldModel M_worldmodel` (+ an optional `M_fullstate_worldmodel` for debug comparisons), an `ActionEffector`, `SeeState`, and dispatches the cycle. It performs the version handshake via `sendInitCommand()`/`analyzeInit()` and populates `ServerParam` — both already documented in `client-framework.instructions.md`.

## Architecture
**Per-cycle message flow** (player_agent.cpp):
1. `PlayerAgent::parse(msg)` (:1448) — dispatches on message prefix:
   - `"(see "` → `Impl::analyzeSee()` (:1550) → parses via `VisualSensor`, then `WorldModel::updateAfterSee()` (world_model.cpp:1108), which calls `localizeSelf`/`localizeBall`/`localizePlayers` (world_model.cpp:2145/2233/2694) to convert relative polar observations into absolute `Vector2D` positions using `Localization` (see below).
   - `"(sense_body "` → `Impl::analyzeSenseBody()` (:1631) → `BodySensor` → `WorldModel::updateAfterSenseBody()` (world_model.cpp:861) — ground truth for stamina, view width, self velocity/angle (much more reliable than `see`).
   - `"(hear "` → `Impl::analyzeHear()` (:1689) → sub-dispatch to `analyzeHearReferee`/`analyzeHearPlayer`/`analyzeHearOurCoach`/`analyzeHearOpponentCoach`/`analyzeHearTrainer`.
   - `"(fullstate "` → `WorldModel::updateAfterFullstate()` (world_model.cpp:1254), used mainly for the debug/comparison `M_fullstate_worldmodel` or coach-mode agents.
2. `WorldModel::update(act, current)` (world_model.cpp:731) — internal per-cycle bookkeeping: saves `M_prev_ball`, advances `M_self`/`M_ball` via dead-reckoning (`M_self.update()`, `M_ball.update()`), decays player object confidence (`p.update()` on every `PlayerObject` in `M_teammates`/`M_opponents`/`M_unknown_players`, pruning invalid ones), clears per-cycle caches (`M_teammates_from_self`, `M_all_players`, kickable pointers), rotates the `ViewArea` ring buffer. Called from inside `updateAfterSenseBody`, or standalone if no sense_body arrived.
3. `PlayerAgent::action()` (:2361) — the decision-time fusion & dispatch, detailed below.

**v20 ball observation contract**: `VisualSensor` is version-aware and preserves the
wire fields without reconstructing or denoising vertical state. For v20, low quality is
`dir z`; high quality without planar changes is `dist dir z`; high quality with changes
is `dist dir dist_chg dir_chg z vz`. The `BallT` presence flags distinguish missing
distance and missing vertical velocity from an observed numeric zero. Protocols v1-v19
retain their historical layouts and legacy low-quality handling. `WorldModel::localizeBall`
commits raw z/vz before planar localization, so low-quality observations can refresh z
without inventing x/y. Fullstate v20 carries x/y/z/vx/vy/vz; legacy four-value fullstate
does not mark z/vz fresh. `BallObject::posZValid()`/`velZValid()` expose the same
confidence-counter semantics as planar state, and consumers treat unavailable/stale z
conservatively. v20 `2d_mode=true` uses the same shapes with zero z/vz.

**`updateJustBeforeDecision()` fusion pipeline** (world_model.cpp:1976-2039), called once per cycle right before `actionImpl()`:
```
update(act, current)                 // if not already updated this cycle
updateBallByHear/updateGoalieByHear/updatePlayerByHear/updatePlayerStaminaByHear
updateBallCollision()                // correct ball pos/vel if a collision is inferred
M_ball.updateByGameMode(); M_ball.updateSelfRelated(); M_self.updateBallInfo()
updatePlayerStateCache(); updatePlayerCard(); updatePlayerType()   // must follow see
updatePlayersCollision()             // must follow player type update
updateOurOffenseLine/DefenseLine/TheirOffenseLine/DefenseLine/PlayerLines
updateLastKicker()
updateInterceptTable()               // delegates to M_intercept_table.update(*this)
updateOffsideLine()
estimateMaybeKickableTeammate()
M_self.updateKickableState(...)      // uses interceptTable().selfStep()/teammateStep()/opponentStep()
```
Ordering is load-bearing — several methods have explicit "have to be called after X" comments (world_model.h:620, `estimateMaybeKickableTeammate()` must follow `updateInterceptTable()`; world_model.cpp:2001/2003 for player type vs collision).

**Localization**: `WorldModel::localizeSelf/localizeBall/localizePlayers` use the injected `std::shared_ptr<Localization> M_localize` (default impl in `localization_default.cpp`, which owns an `ObjectTable M_object_table`). `ObjectTable` (object_table.h:48, class `ObjectTable`) is a precomputed distance/quantization-error lookup table for landmarks and flags used to invert noisy polar (distance, direction) observations into absolute field coordinates — it is a helper consumed by `Localization`, not by `WorldModel` directly.

**Interception prediction**: `InterceptTable` (`intercept_table.h/.cpp`) owns a `std::shared_ptr<InterceptSimulatorSelf> M_self_simulator`, defaulted to **`InterceptSimulatorSelfV17`** (intercept_table.cpp:67,353 — the current/active version). Older `InterceptSimulatorSelfV7`/`V13` classes still exist in the tree (`intercept_simulator_self_v7.cpp`, `intercept_simulator_self_v13.cpp`) but are legacy alternatives, not the default — check `intercept_table.cpp` before assuming which version is live for a given build. All implement the abstract `InterceptSimulatorSelf::simulate(wm, max_step, results)` (intercept_simulator_self.h:61) strategy interface; `InterceptTable::update()` also simulates teammate/opponent intercepts via `intercept_simulator_player.cpp` and exposes `selfStep()`/`teammateStep()`/`opponentStep()` consumed by `WorldModel`.

## Patterns & Conventions
- **Strategy pattern for interception**: swap `InterceptSimulatorSelf` implementations via `InterceptTable`'s constructor/`setInterceptSimulator`-style injection rather than templating `WorldModel`.
- **Confidence/decay model**: `PlayerObject`/`BallObject`/`SelfObject` track a "count since last seen" per attribute (pos, vel, body, face) rather than a single staleness flag — stale data isn't discarded outright but degrades in reliability; `posValid()` gates pruning in `WorldModel::update()`.
- **Two coordinate views per object**: absolute pos (`pos()`) and self-relative (`rpos()`), both maintained in parallel (e.g. `M_ball.rposValid()`).
- **Explicit ordering comments** instead of a dependency graph — read the surrounding comments in `updateJustBeforeDecision` before reordering calls.
- **Pointer-cache containers rebuilt every cycle**: `M_teammates_from_self`, `M_opponents_from_ball`, `M_our_player_array[12]`, `M_all_players`, etc. are `Cont` (pointer container) views into the owning `List` members (`M_teammates`, `M_opponents`), rebuilt in `update()`/`updatePlayerStateCache()`; never store these pointers across cycles.
- **`dlog.addText(Logger::WORLD/SENSOR/SYSTEM, ...)`** is used pervasively for cycle-by-cycle tracing (see Logging).

## Key Abstractions
- `WorldModel` (world_model.h:68) — composes `SelfObject M_self`, `BallObject M_ball` (+ `M_prev_ball`), `PlayerObject::List M_teammates/M_opponents/M_unknown_players`, `InterceptTable M_intercept_table`, `PenaltyKickState`, `AudioMemory`.
- `SelfObject` (self_object.h/.cpp) — self position/velocity/stamina/view-mode/kickable state; updated from `sense_body` (ground truth) + dead-reckoning.
- `BallObject` (ball_object.h/.cpp, extends `BallState`) — ball pos/vel with rpos, collision correction, kick-effect prediction.
- `PlayerObject` (player_object.h/.cpp, extends `AbstractPlayerObject`) — a single tracked teammate/opponent/unknown player; `PlayerObject::List`/`::Cont` typedefs used throughout `WorldModel`.
- `ObjectTable` (object_table.h:48) — precomputed landmark distance-error table consumed by `Localization` implementations (not by `WorldModel` directly).
- `InterceptTable` / `InterceptSimulatorSelf` (+`V7`/`V13`/`V17` impls) / `Intercept` (intercept.h) — interception step prediction subsystem.
- `PlayerAgent` (player_agent.h) — the `SoccerAgent` subclass; `Impl` (pimpl) holds `see_state_`, sensor parsers, and per-message `analyze*` methods; `actionImpl()`/`communicationImpl()` are the pure-virtual hooks a concrete team agent (e.g. helios-base's `SamplePlayer`) must implement.

## Integration Points
- **`client-framework.instructions.md`**: `PlayerAgent` subclasses/uses `SoccerAgent`; version handshake `sendInitCommand()` (player_agent.cpp:1284) / `analyzeInit()` (player_agent.cpp:2060 area, calls `handleServerParam()`/`handleInitMessage()`); `ServerParam::parse()` populates the singleton read throughout `WorldModel` (e.g. `ServerParam::i().synchMode()` in `PlayerAgent::action()`).
- **`geometry.instructions.md`**: all positions/velocities are `Vector2D`; body/neck/view angles are `AngleDeg`.
- **`action-behavior-library.instructions.md`** (if present): action/behavior classes consume `WorldModel` as their sole read-only view of game state — `world_model.h` getters (`self()`, `ball()`, `teammates()`, `interceptTable()`, etc.) form the read API boundary.
- Downstream: **helios-base** (sibling repo) subclasses `PlayerAgent` to implement `actionImpl()`/`communicationImpl()` and is the primary consumer of `WorldModel`'s public query surface.

## Build & Test
Part of the main librcsc CMake/Autotools build (`rcsc/player/CMakeLists.txt`, `Makefile.am` list all 75 files as one static-lib source set — no per-file test target). No unit tests specific to `world_model.cpp` were found in-tree; validate changes by building librcsc and running an actual agent (e.g. helios-base) against `rcssserver`, checking `dlog` output for `Logger::WORLD`/`Logger::SENSOR` traces.

## Logging
Extensive `dlog.addText(Logger::WORLD, ...)`, `Logger::SENSOR`, and `Logger::SYSTEM` calls throughout `world_model.cpp`/`player_agent.cpp` (e.g. world_model.cpp:760, player_agent.cpp:2347/2364) — this is the primary debugging tool for belief-state issues; enable the corresponding debug flags to trace localization/hear/intercept decisions cycle-by-cycle.

## Important Notes
- `WorldModel::update()` guards against double-invocation per cycle (world_model.cpp:738 checks `time() == current` and logs an error) — do not call it manually outside the sensor-analyze / `updateJustBeforeDecision` paths.
- Player/ball state is cleared entirely on `BeforeKickOff`/`AfterGoal_` transitions (world_model.cpp:814-824), including `PlayerObject::reset_player_count()`.
- The current default self-interception simulator is `InterceptSimulatorSelfV17`; do not assume `_v13`/`_v7` files are active just because they exist in the source tree — verify via `intercept_table.cpp` construction sites before modifying interception behavior.
- Ordering violations in `updateJustBeforeDecision()` are a common source of subtle bugs (e.g. calling `updatePlayerType()` before `updatePlayersCollision()` needs it, or `updateInterceptTable()` before offense/defense lines are current) — always check for adjacent comments before reordering.

## See Also
- [client-framework.instructions.md](./client-framework.instructions.md)
- [geometry.instructions.md](./geometry.instructions.md)
- action-behavior-library.instructions.md (if documented)
- helios-base (sibling repo) — subclasses `PlayerAgent`, primary consumer of `WorldModel`

---
Part of: [`librcsc copilot-instructions.md`](../copilot-instructions.md)
