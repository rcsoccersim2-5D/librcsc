---
applyTo: 'rcsc/coach/**,rcsc/trainer/**,rcsc/clang/**'
---

# Coach & Trainer Agents + Client-Side CLang

## TL;DR
`CoachAgent` (rcsc/coach/, 33 files) and `TrainerAgent` (rcsc/trainer/, 8 files) both subclass `SoccerAgent` and share the same full-field `CoachWorldModel`; the `rcsc/clang/` grammar is used **asymmetrically** — coaches only *build* CLang strings via `operator<<`, while it is `PlayerAgent`/`AudioSensor` (rcsc/player/) that actually *parses* incoming CLang with `CLangParser`.
- `CoachAgent` : `SoccerAgent` and `TrainerAgent` : `SoccerAgent` both declared identically in [coach_agent.h:58-59](../../rcsc/coach/coach_agent.h) and [trainer_agent.h:53-54](../../rcsc/trainer/trainer_agent.h) — confirmed direct sibling subclassing, same `pimpl` idiom (`struct Impl`).
- Both agents embed a `CoachWorldModel M_worldmodel` member (protected) — the **same class** is reused for coach and trainer, unlike per-role `WorldModel` for players.
- `rcsc/clang/CLangParser` (parse-only) is consumed by `rcsc/player/audio_sensor.h:100` (`CLangParser M_clang_parser`) and `player_agent.cpp:118`, **not** by `coach_agent.cpp` — coaches send `(say (clang ...))` built from `clang_action.h`/`clang_condition.h`/`clang_directive.h` `print()`/`operator<<` methods.
- `PlayerTypeAnalyzer` ([player_type_analyzer.h:51](../../rcsc/coach/player_type_analyzer.h)) statistically infers each opponent's hidden `HeteroPlayer` type ID by replaying candidate types against observed turn/kick/tackle deltas and counting `invalid_flags_` mismatches.
- **Open the full file when:** editing coach say/hear message flow (`coach_agent.cpp:900-2000`), CLang grammar semantics (`clang_parser.cpp`, `coach_lang_grammer.txt`), or the opponent-type voting logic (`player_type_analyzer.cpp::analyze()/checkTurn()`).

## Overview
- **`rcsc/coach/`** (33 files) implements the offline/online coach client: `CoachAgent` connects, receives `(see_global ...)`/`(hear ...)`/`(player_type ...)`/`(player_param ...)`/`(server_param ...)` messages, maintains `CoachWorldModel`, and issues `(change_player_type)`, `(say (clang ...))`, `(say (freeform ...))` commands.
- **`rcsc/trainer/`** (8 files) implements the *offline coach* / trainer client used for training/keepaway scripts: `TrainerAgent` reuses `CoachWorldModel` but sends omnipotent commands (`(move (ball ...))`, `(change_mode ...)`, `(recover)`) via `TrainerCommand` (trainer_command.h/.cpp) — it has **no CLang messaging**, that's coach-only.
- **`rcsc/clang/`** (21 files) is the **client-side CLang grammar**: `clang_action.*`, `clang_condition.*`, `clang_directive.*`, `clang_unum.*`, `clang_token.*`, `clang_info_message.*` model the AST; `clang_parser.cpp` (~859 lines, hand-written recursive-descent, not the bison/flex generator rcssserver uses) turns a raw CLang string into a `CLangMessage::ConstPtr`; `coach_lang_grammer.txt` documents the BNF mirrored from the server spec.

## Architecture
```
SoccerAgent (rcsc/common/soccer_agent.h)
 ├── CoachAgent   (rcsc/coach/coach_agent.h)   -- M_worldmodel: CoachWorldModel, M_config: CoachConfig
 └── TrainerAgent (rcsc/trainer/trainer_agent.h) -- M_worldmodel: CoachWorldModel, M_config: TrainerConfig
```
- `CoachAgent::Impl` (pimpl, coach_agent.cpp:60-260) owns `CLangMessage::ConstPtr clang_message_` (queued outgoing message), `std::vector<FreeformMessage::Ptr> freeform_messages_`, and calls `sendCLang()` (coach_agent.cpp:1697) which gates on `world().canSendCLang(type)` (coach_world_model.cpp:1184) before transmitting.
- **Correction — `CLangManager` is NOT the rate-limiter actually used by `sendCLang()`**: the real gate is `CoachWorldModel`'s own `M_clang_capacity[type]` array (coach_world_model.cpp:100-102,512-525), refilled every `ServerParam::clangWinSize()` cycles from `SP.clangDefineWin()/clangMetaWin()/clangAdviceWin()/clangInfoWin()/clangDelWin()/clangRuleWin()` and decremented by `decCLangCapacity()` (coach_world_model.cpp:1215) after each send. `CLangManager` (rcsc/coach/clang_manager.h/.cpp — lives under `coach/` not `clang/`) does track per-team min/max supported CLang version (`M_teammate_clang_ver[11]`) and per-message-type last-sent bookkeeping, but grep confirms `coach_agent.cpp`/`coach_world_model.cpp` never reference it — its only consumer in this tree is the abstract `SoccerAdviser` interface (`soccer_adviser.h`), which has no concrete implementation found in librcsc. Treat `CLangManager` as an orphaned/unused utility class here, not part of the live send path.
- `CoachWorldModel` (coach_world_model.h/.cpp, ~1169 lines) composes `CoachWorldState` snapshots (`CoachWorldState::Ptr M_current_state`), built each cycle in `updateAfterSeeGlobal()` (coach_world_model.cpp:399) from a `CoachVisualSensor & see_global` — **the whole field, all 22 players + ball, every cycle**, no ambiguity/decay modeling. Contrast with player's `WorldModel` (see world-model-and-agent-core.instructions.md) which fuses partial `(see)` cones with `PlayerObject` position/velocity confidence decay across cycles — `CoachWorldModel` has no such decay because `(see_global)` is always complete.
- `PlayerTypeAnalyzer` (member of `CoachWorldModel`, player_type_analyzer.h:76 `const CoachWorldModel & M_world`) runs each cycle via `update()` → `updateLastData()` + `analyze()` (player_type_analyzer.h:113,145,150), tracking per-opponent `Data{turned_, rotation_, kicked_, tackling_, maybe_referee_, maybe_collide_, maybe_kick_, pos_, vel_, body_, invalid_flags_, type_}` and voting counts in `M_opponent_type_used_count`.

## Patterns & Conventions
- **pimpl idiom** throughout: `CoachAgent::Impl`, `TrainerAgent::Impl` hide message-queue/socket state from the header; mirrors the player-side pattern (see client-framework.instructions.md).
- **Command build, don't parse, on the coach side**: `clang_action.h`/`clang_condition.h`/`clang_directive.h` classes implement `virtual std::ostream & print(std::ostream&) const = 0` plus a free `operator<<` (clang_action.h:124-125) — coaches assemble `CLangMessage` objects programmatically and stream them, they do not round-trip parse their own output.
- **CLangParser is parse-only and stateless per call**: `bool parse(const std::string&)` (clang_parser.h:77) populates `M_message`; `clear()` resets. Used exclusively from the player side (`rcsc/player/audio_sensor.h:100`, `player_agent.cpp:118`) to decode CLang heard from the team's *online coach* — i.e., the grammar is bidirectional across the codebase as a whole (coach builds, player parses) but **not** bidirectional within a single class.
- **Rate limiting before send**: every `send*()` in `coach_agent.cpp` (sendCLang, sendFreeform) checks `world().canSendCLang(...)` / capacity counters before transmitting — never bypass this when adding new coach commands.
- **Freeform vs CLang**: freeform messages (`(say (freeform "..."))`) are plain strings for helios-base style custom protocols; CLang messages (`(say (clang ...))`) are the standards-based tactical grammar — both funnel through the same `sendCLang()`/`sendFreeform()` gate in `Impl`.

## Key Abstractions
- `CoachAgent` (coach_agent.h/.cpp ~1800 lines) — main coach loop; `config()`, `debugClient()`, `world()`, `visualSensor()`, `audioSensor()`.
- `TrainerAgent` (trainer_agent.h/.cpp) — main trainer loop; reuses `CoachWorldModel` but issues omnipotent `TrainerCommand`s instead of CLang.
- `CoachWorldModel` (coach_world_model.h ~977 lines / .cpp ~1169 lines) — full-field snapshot model; `M_our_side` becomes `NEUTRAL` when driven by a trainer (coach_world_model.h:73-74 comment).
- `CoachWorldState` (coach_world_state.h/.cpp) — one-cycle immutable snapshot built from `CoachVisualSensor`.
- `PlayerTypeAnalyzer` (player_type_analyzer.h/.cpp) — opponent `HeteroPlayer` ID inference.
- `CLangParser` (clang_parser.h/.cpp, ~859 lines) — hand-rolled recursive-descent parser for CLang strings → `CLangMessage::ConstPtr`.
- `CLangManager` (clang_manager.h/.cpp, under `coach/`) — version/rate-limit bookkeeping data structure; only referenced by the abstract `SoccerAdviser` interface (`soccer_adviser.h`), not by `CoachAgent`/`CoachWorldModel`'s actual send path (see correction in Architecture above) — appears unused/orphaned in this codebase.
- `CLangMessage` / `clang_action.h` / `clang_condition.h` / `clang_directive.h` / `clang_unum.h` / `clang_info_message.h` — the CLang AST node hierarchy (rule, action, condition, directive, unum-set, info message types).

## Integration Points
- **Server protocol**: coaches send `"(say " << *clang_message_ << ')'` (coach_agent.cpp:1715) over the same `(say ...)` channel rcssserver's `OnlineCoach::parseCommand` handles (rcssserver/src/coach.cpp:1141-1300, already documented server-side).
- **`(clang ...)` handshake**: incoming `"(clang "` messages (coach_agent.cpp:1016) trigger `analyzeCLangVer()` (coach_agent.cpp:244,1416) to learn teammates' supported CLang version range, feeding `CLangManager`.
- **Player consumption**: `PlayerAgent`/`AudioSensor` (rcsc/player/) parse coach CLang via `CLangParser M_clang_parser` — the actual tactical-directive interpretation (rule matching, action dispatch) happens client-side in the player, not the coach.
- **Config/param sync**: both `CoachAgent::Impl` and presumably `TrainerAgent::Impl` call `PlayerParam::instance().parse(msg, ...)` and `ServerParam::instance().parse(msg, ...)` (coach_agent.cpp:1382,1396) on `(player_param ...)`/`(server_param ...)` — same singleton pattern used by player agents.

## Build & Test
- Built as part of `librcsc` via `rcsc/coach/CMakeLists.txt`, `rcsc/trainer/CMakeLists.txt`, `rcsc/clang/CMakeLists.txt` (also `Makefile.am` for autotools).
- `rcsc/clang/test_clang_parser.cpp` is a CppUnit fixture (`CLangParserTest : CPPUNIT_NS::TestFixture`, test `testInfoMessage`) — the only unit test found in this subsystem; run via the project's cppunit test target.

## Logging
- Coach uses `agent_.debugClient()` / standard `dlog`-style macros seen at coach_agent.cpp:1720 (`"---- send clang [%s]", clang_message_->typeName()`) — consistent with player-side debug logging conventions.

## Important Notes
- **Do not assume the coach parses its own CLang** — `clang_parser.cpp` exists in `librcsc` but is wired to the *player* side (`audio_sensor.h`, `player_agent.cpp`), not `coach_agent.cpp`. If you need the coach to interpret CLang it receives (e.g., coach-to-coach relay), you must instantiate `CLangParser` yourself; it is not automatically invoked.
- `CoachWorldModel::M_our_side` becomes `NEUTRAL` for trainers (coach_world_model.h:74) — code shared between coach/trainer must not assume a definite side.
- `player_type_analyzer.cpp`'s inference is heuristic/statistical (invalid-flag counting across candidate types), not exact — expect occasional `Unknown` (`setUnknownType()`) especially early in a half or after referee-forced repositioning (`maybe_referee_` flag exists specifically to exclude those samples).
- Empty `RuleIDList` semantics ("ALL rules") documented server-side (rcssserver/src/coach.cpp) apply identically to CLang rules built here — when constructing `clang_directive`/`clang_condition` rule sets client-side, an empty ID list is not "no rules", it's "all rules".

## See Also
- [client-framework.instructions.md](./client-framework.instructions.md) — `SoccerAgent`/`AbstractClient` base hierarchy that `CoachAgent`/`TrainerAgent` subclass.
- [world-model-and-agent-core.instructions.md](./world-model-and-agent-core.instructions.md) — player-side `WorldModel` pattern; contrast with `CoachWorldModel`'s full-field, no-decay design.
- Cross-repo: rcssserver's **coach-language-clang** instruction documents the server-side CLang grammar/parser/builder (`rcss/clang/`) consumed by `OnlineCoach::parseCommand`'s `"say"` handler — this file is its client-side (build side) counterpart. helios-base's `sample_coach.cpp`/`sample_trainer.cpp` are the sibling repo's concrete `CoachAgent`/`TrainerAgent` consumers, once documented.

---
Part of: [`librcsc copilot-instructions.md`](../copilot-instructions.md)
