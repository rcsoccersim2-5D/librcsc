---
applyTo: 'rcsc/action/**'
---

# Action & Behavior Primitive Library

## TL;DR
This directory implements every concrete player behavior (kick/dribble/pass/go-to-point/neck/view/focus/arm) as a small, single-purpose class following an "effector-slot" pattern rooted in `rcsc/player/soccer_action.h`.
- Five parallel effector-slot base classes — `BodyAction`, `NeckAction`, `ViewAction`, `FocusAction`, `ArmAction` — plus `SoccerBehavior` for composite multi-slot behaviors, all deriving from `AbstractAction` (`rcsc/player/soccer_action.h:45,96,134,184,234,283,333`).
- `KickTable` (`kick_table.h` 411 lines / `kick_table.cpp` 2241 lines) is the foundational singleton used by `Body_SmartKick`/`Body_Dribble2008` to search multi-step kick sequences against opponent interference.
- `Body_Dribble2008` is the current (latest) dribble behavior; `obsolete/body_dribble2006.cpp` and `obsolete/body_dribble2007.cpp` are superseded prior-year variants kept for reference/tuning history, not compiled into the active library.
- **Open the full file when:** you need to change kick feasibility math (`kick_table.cpp`), tune dodge/dribble heuristics (`body_dribble2008.cpp`), or trace exactly how a `Body_*` behavior turns world-model geometry into `agent->doDash/doTurn/doKick` calls.

## Overview
`rcsc/action/` (116 files) is librcsc's **behavior primitive layer**: it sits between the belief state (`WorldModel`, documented in `world-model-and-agent-core.instructions.md`) and the low-level protocol effectors (`PlayerAgent::doKick/doDash/doTurn/doTurnNeck/doChangeView/doChangeFocus/doPointto`, declared on `PlayerAgent`). Every class here is a *strategy object*: constructed with parameters (target point, dash power, dodge flag, ...), then invoked once via `execute(PlayerAgent*)`, which reads `agent->world()` and issues exactly the effector command(s) appropriate for that role. There is no persistent behavior tree — callers (helios-base and other clients built on librcsc) compose these primitives cycle-by-cycle in their own decision logic.

## Architecture
Base interfaces live in `rcsc/player/soccer_action.h` (not `rcsc/action/`, but this whole directory implements it):
- `AbstractAction` (line 45): root — pure virtual `execute(PlayerAgent*)`, non-copyable, auto-incrementing `actionObjectID()`.
- `BodyAction` (line 96): body-effector behaviors (dash/turn/kick). **Not** cloneable/queueable — constructed and executed immediately (e.g. `Body_TurnToPoint`, `body_go_to_point.h:45`).
- `NeckAction` (line 134), `ViewAction` (line 184), `FocusAction` (line 234), `ArmAction` (line 283): all add a pure-virtual `clone()` returning `Ptr = std::shared_ptr<T>` — these ARE storable/queueable because agents often set a *default* neck/view/focus/arm behavior to reuse every cycle (e.g. `Neck_TurnToBall::clone()`, `neck_turn_to_ball.h:65`).
- `SoccerBehavior` (line 333): abstract composite behavior slot for multi-effector "meta" behaviors (e.g. `Bhv_GoToPointLookBall`, `Bhv_ScanField`) that internally drive body+neck together.

**Confirmed effector-slot separation**: a single player command cycle independently sets body direction (`BodyAction`), neck direction (`NeckAction`), view width (`ViewAction`), focus point/distance (`FocusAction`), and arm pointing (`ArmAction`) — five orthogonal slots, matching the RoboCup 2D player protocol's independent `dash/turn/kick` + `turn_neck` + `change_view` + `change_focus` + `pointto` commands.

## Patterns & Conventions
- **Naming convention**: `<Slot>_<BehaviorName>` — `Body_GoToPoint`, `Neck_TurnToBall`, `View_Wide`, `Bhv_ScanField` (composite), `Arm_PointToPoint`. File names are lower_snake of the class name (`body_go_to_point.h` → `Body_GoToPoint`).
- **Construction-time parameters, execute-time side effects**: nearly every class stores `const` members set in the constructor (e.g. `Body_GoToPoint`'s `M_dist_thr`, `M_max_dash_power`, `M_dash_speed`, `M_cycle`, `M_dir_thr`, `body_go_to_point.h:48-59`) and does all real work inside `bool execute(PlayerAgent*)`, returning `false` when the action was not needed/failed.
- **Year-versioned evolution**: several behaviors have multiple competition-year implementations (`body_dribble2006/2007/2008`, `body_intercept2007/2008/2009`, `body_go_to_point2007/2009/2010`, `body_clear_ball2007/2009`, `body_advance_ball2007/2009`, `bhv_shoot2008`). The **non-suffixed or highest-year file still referenced by `CMakeLists.txt`/`Makefile.am`** is the active one; everything else lives under `rcsc/action/obsolete/` and is excluded from the build — treat it as historical reference only, never edit it expecting runtime effect.
- **Intention_* classes** (`intention_dribble2008.h`, `intention_time_limit_action.h`) implement multi-cycle *action queues*: `finished(PlayerAgent*)` (line 93) + `execute()` let a behavior spread a planned dash/kick sequence across several cycles, re-invoked each cycle until `finished()` returns true.
- **`Matrix2D` for kinematics**: `body_go_to_point.cpp:242,488` uses `Matrix2D::make_rotation(-wm.self().body())` to transform dash vectors into body-relative space — confirms the geometry.instructions.md cross-reference.
- **Singleton heavy tables**: `KickTable::instance()` (kick_table.h:359) is the only singleton in this directory; other behaviors are stateless value objects, not singletons.

## Key Abstractions
- **`KickTable`** (`kick_table.h/.cpp`): precomputed lookup table of reachable ball positions after 1-3 dash/kick steps (`MAX_DEPTH=2`, `DEST_DIR_DIVS=72` — 5° angle resolution, kick_table.h:56-58). Core entry point `simulate(world, target_point, first_speed, allowable_speed, max_step, Sequence&)` (line 391) runs `simulateOneStep/TwoStep/ThreeStep` → `evaluate()` → returns best `Sequence` of kick commands, factoring in `Flag` bits like `SELF_COLLISION`, `KICKABLE`, `OUT_OF_PITCH`, `KICK_MISS_POSSIBILITY` (lines 64-75). Used by `Body_SmartKick::execute()` (`body_smart_kick.cpp:114` calls `agent->doKick(...)`) and `Body_Dribble2008`.
- **`Body_Dribble2008`** (`body_dribble2008.h:48`, .cpp 2369 lines): advanced dribble with opponent avoidance (`M_dodge_mode`). Nested `KeepDribbleInfo` struct tracks `first_ball_vel_`, `dash_count_`, `min_opp_dist_` per candidate dribble plan. Private helpers `doAction()`, `doDodge()` (implied), `say()` (communicates target via say command) evaluate candidate dash sequences and pick the safest one.
- **`Body_GoToPoint`** (`body_go_to_point.h:45`, .cpp): the canonical point-navigation primitive — parameters include `dist_thr`, `max_dash_power`, `dash_speed`, `cycle`, `save_recovery`, `dir_thr`, `omni_dash_dist_thr`, `use_back_dash`; internally decides omni-directional vs. forward/backward dash based on angle-to-target vs. `dir_thr`.
- **`Body_Pass`** (`body_pass.h:50`): full pass planner — enum `PassType{DIRECT,LEAD,THROUGH}` and `PassRoute` struct (receiver, receive point, first speed, `one_step_kick_`, `score_`) rank candidate teammates before committing to `KickTable`-driven execution.
- **`Neck_TurnToBall` / `View_Wide`** (`neck_turn_to_ball.h:43`, `view_wide.h:43`): minimal examples of the cloneable-slot pattern — trivial `execute()` + `clone()` returning `new T(*this)`-equivalent, meant to be stored as a default `NeckAction::Ptr`/`ViewAction::Ptr` on the agent.
- **`basic_actions.h`**: pure aggregator header (`#include` of arm/bhv/body_turn/neck/view fundamentals) — not a class, just a convenience include for the most primitive turn/view/arm actions.

## Integration Points
- Reads: `WorldModel` (ball/self/opponent positions, `ServerParam::instance()` constants for max speed/power) — see `world-model-and-agent-core.instructions.md`.
- Writes: `PlayerAgent::doDash/doTurn/doKick/doTurnNeck/doChangeView/doChangeFocus/doPointto/doSay` — the five effector command queues flushed once per cycle by the agent core.
- Geometry: `Vector2D`, `AngleDeg`, `Matrix2D` from `rcsc/geom/` (see `geometry.instructions.md`).
- Downstream consumer: **helios-base**'s `basic_actions/` (sibling repo, not this one) builds higher-level team strategies (positioning, formation-aware pass selection) on top of these `Body_*`/`Neck_*`/`Bhv_*` primitives — this library is the reusable base, helios-base's is the team-specific policy layer.

## Build & Test
- Built as an OBJECT library target `rcsc_action` via `rcsc/action/CMakeLists.txt` (also `Makefile.am` for autotools) — only non-`obsolete/` `.cpp` files are listed as sources; `obsolete/` is excluded from both build systems.
- No dedicated unit tests found under this directory; validated indirectly through full-agent integration runs against `rcssserver`.

## Logging
- Debug tracing goes through the shared `rcsc::Logger`/`dlog` macros (same logging backend documented in the client-framework/world-model instructions) — e.g. `KickTable::debugPrintStateCache()` and `debugPrintSequence()` (kick_table.h:342,349) dump candidate kick paths for offline analysis via soccerwindow2 log replay.

## Important Notes
- **Do not confuse year-suffixed active files with `obsolete/` ones** — e.g. `body_go_to_point2010.cpp` exists only in `obsolete/`, while the actively-built point-navigation logic is the un-suffixed `body_go_to_point.cpp`. Always check `CMakeLists.txt`/`Makefile.am` before assuming a numbered variant is live.
- `BodyAction` deliberately has **no** `clone()` — body behaviors are one-shot, decided fresh every cycle, unlike Neck/View/Focus/Arm which may persist as a queued default.
- `KickTable::instance()` must have `createTables()` (or `read()` from a precomputed file) called once at startup before `simulate()` is meaningful.

## See Also
- [world-model-and-agent-core.instructions.md](world-model-and-agent-core.instructions.md)
- [geometry.instructions.md](geometry.instructions.md)
- [client-framework.instructions.md](client-framework.instructions.md)
- Cross-repo: helios-base's `basic_actions/` is the sibling repo's higher-level behavior layer built on top of this one.

---
Part of: [`librcsc copilot-instructions.md`](../copilot-instructions.md)
