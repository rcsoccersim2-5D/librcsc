---
applyTo: 'rcsc/monitor/**,rcsc/color/**,rcsc/param/**,rcsc/time/**,rcsc/util/**,rcsc/rcsc.h,rcsc/types.h,rcsc/game_time.h,rcsc/game_mode.h,rcsc/factory.h,rcsc/math_util.h,rcsc/random.h'
---

# Infrastructure Utilities

## TL;DR
Grab-bag of small, independent support modules used across all of librcsc: generic CLI/config-file parsing (`ParamMap`/`CmdLineParser`/`ConfFileParser`), the referee playmode wrapper (`GameMode`), a generic factory/registry template, debug-draw color helpers, a millisecond `Timer`, and monitor-protocol command classes.
- `rcsc/param/param_map.h` is the **real, confirmed home** of `ParamMap` referenced by `ServerParam` in client-framework — see below.
- `rcsc/factory.h` defines `rcss::Factory<Creator,Index,Compare>` — the generic factory template referenced in rcg-log-replay.instructions.md (namespace is `rcss`, NOT `rcsc`).
- `rcsc/game_mode.h`'s `GameMode::Type` enum is the **client-side mirror** of rcssserver's `PlayMode`; `rcsc/types.h` also has its own `enum PlayMode` (line 130) which is the server-wire-format counterpart — do not confuse the two.
- **Open the full file when:** you need to add a new CLI/config parameter (`param_map.cpp`), add a monitor command (`monitor_command.h/.cpp`), or trace a playmode string parsing bug (`game_mode.cpp`).

## Overview
These directories have no interdependency on each other except through `rcsc/types.h` (shared enums: `SideID`, `PlayMode`, `Card`, `MarkerID`, `LineID`, `BallStatus`) and `rcsc/game_time.h` (`GameTime`, cycle+stopped-cycle pair). Everything here is infrastructure consumed by higher-level modules (agent core, client framework, coach) rather than soccer-domain logic itself, except `util/soccer_math.cpp` and `util/game_mode.cpp` which do encode simple domain formulas/parsing.

## Architecture
- **`param/`** (11 files) — generic, template-based option/registry framework:
  - `param_map.h/.cpp` — `ParamEntity` (one option: long/short name + `std::variant<int*,size_t*,double*,bool*,NegateBool,BoolSwitch,NegateSwitch,std::string*>` value pointer + description) and `ParamMap` (container keyed by long/short name, with nested `Registrar` helper class exposing the `add()( "long","short", &var, "desc" )` fluent syntax used throughout `ServerParam`/`PlayerParam`).
  - `param_parser.h` — abstract `ParamParser` base (`parse(ParamMap&)` pure-virtual).
  - `cmd_line_parser.h/.cpp` — `CmdLineParser : ParamParser`, tokenizes `argc/argv` or a `std::list<std::string>`, separates positional args (`positionalOptions()`) from `--name value` options, tracks `parsedOptionNames()`, `count()`, `failed()`.
  - `conf_file_parser.h/.cpp` — `ConfFileParser : ParamParser`, reads a `key = value` style config file (`M_file_path`, `M_delimiters`, `M_realm` — realm lets one file hold multiple named parameter groups).
  - `rcss_param_parser.h/.cpp` — variant of the config parser matching rcssserver's own conf-file dialect (used to read `server.conf`-style files).
- **`game_mode.h/.cpp`** (top-level, not in `param/`) — `GameMode` class wraps the referee state: `Type` enum (`BeforeKickOff, PlayOn, KickOff_, FreeKick_, PenaltyKick_, ...` — trailing `_` means "side-qualified", combined with `M_side`), `update(mode_str, GameTime)` parses server playmode strings (see the big comment block at lines 102-177 in `game_mode.h` listing every wire string), `getServerPlayMode()` converts back to `rcsc::PlayMode` (the `types.h` enum). Helper predicates: `isServerCycleStoppedMode()`, `isGameEndMode()`, `isPenaltyKickMode()`, `isTeamsSetPlay()/isOurSetPlay()/isTheirSetPlay()`.
- **`factory.h`** (top-level, `namespace rcss`, note: different namespace than `rcsc`!) — `Factory<Creator,Index=const char*,Compare=less<Index>>` is a stack-per-index registry (`reg()`/`dereg()`/`getCreator()`/`list()`/`size()`); `AutoReger<OF>` + `Factory::autoReg()` return a `RegHolder` (`std::unique_ptr<RegHolderImpl>`) enabling static/global auto-registration of creators (comment warns: "Auto registration Cannot be used in dynamic libraries").
- **`color/`** (11 files) — debug-drawing color helpers, no dependency on soccer domain:
  - `rgb_color.h/.cpp` — `RGBColor` (double r/g/b 0.0-1.0, ctor + accessors).
  - `gradation_color_provider.h/.cpp` — `GradationColorProvider`: maps a float in a range to an interpolated `RGBColor` gradient.
  - `gray_scale_provider.h/.cpp` — `GrayScaleProvider`: maps float → grayscale `RGBColor`.
  - `thermo_color_provider.h/.cpp` (+ `thermo_color_test.cpp`) — `ThermoColorProvider`: float → "thermal" (blue→red) color scale, has its own standalone test file (not part of the main test target — check `color/CMakeLists.txt`/`Makefile.am`).
- **`time/timer.h/.cpp`** — `TimeStamp` (thin wrapper of `std::chrono::system_clock::time_point`, `isValid()/setNow()/elapsedSince()`) and `Timer` (`restart()`, `elapsed(Type)`/`elapsedReal(Type)` with `Type{MSec,Sec,Min,Hour,Day}`) — used for wall-clock profiling (e.g., think-time budget checks in the agent's decision loop), NOT for `GameTime`/simulation cycles.
- **`util/`** (5 files, no subheaders — `.cpp` only, declarations live in top-level headers included via `rcsc.h`):
  - `game_mode.cpp` — implementation of `GameMode::update()`/`parse()` string-matching logic (large `if`/`else if` chain over the wire strings).
  - `soccer_math.cpp` — free functions: `wind_effect(speed, weight, wind_force, wind_dir, wind_weight, wind_rand, ...)` (server wind-drift formula mirror), `unquantize_error(see_dist, qstep)` and related quantization helpers (comments reference old `VisualSensor_v1`/`v7` and `WorldModel::getMinMaxDistQuantizeValue/invQuantizeMin/invQuantizeMax` — legacy naming, now free functions in `rcsc` namespace, declared in `rcsc/soccer_math.h`).
  - `version.cpp` — `copyright()` and `version()` return the literal `PACKAGE_NAME`/`VERSION`/`PACKAGE`-macro strings (from `config.h`, autoconf/CMake generated) — **do not hand-edit the copyright string**, comment explicitly says so (line 48).
- **`monitor/`** (4 files) — `monitor_command.h/.cpp`: abstract `MonitorCommand` (`Type{INIT,BYE,START,FOUL,PLAYER,DISCARD,CARD,COMPRESSION,ILLEGAL}`, pure-virtual `toCommandString(ostream&)`, `name()`) with concrete subclasses (`MonitorInitCommand` at line 111 takes a protocol `M_version`, plus others for player move/discard/card commands) — these build the text commands a monitor client sends to rcssserver (kickoff, drop-ball/free-kick, move player, card), distinct from the `rcg` replay format handled elsewhere.
- **Loose top-level headers**: `rcsc.h` is the umbrella include (pulls in `rcg.h`, `bpn1.h`, `game_mode.h`, `game_time.h`, `math_util.h`, `random.h`, `soccer_math.h`, `timer.h`, `types.h` — NOT `factory.h`, `param/*`, `color/*`, or `monitor/*`, which must be included explicitly). `types.h` holds the shared enums (`SideID`, `MarkerID`, `LineID`, `PlayMode` at line 130, `BallStatus`, `Card`). `game_time.h` is `GameTime{M_cycle, M_stopped}`. `math_util.h` has `bound()`/`min_max()`/`square()` templates + `EPS = 1.0e-10`. `random.h` has singleton `RandomEngine` (`std::mt19937`, seeded from `std::random_device`, `instance()`/`seed()`).

## Patterns & Conventions
- All classes use the `M_member_name` Hungarian-ish prefix and `-*-c++-*-` emacs mode line + LGPL header boilerplate — keep this when adding new files in these directories.
- Abstract base + concrete subclass pattern repeats: `ParamParser` → `CmdLineParser`/`ConfFileParser`/`RcssParamParser`; `MonitorCommand` → `MonitorInitCommand`/etc.
- Singletons use a private constructor + static local `instance()` (see `RandomEngine`), not Meyers-singleton macros.
- `ParamMap::Registrar::operator()` (fluent chained `add()("name","n",&var,"desc")(...)`) is the idiomatic way parameters are declared — grep for `.add(` in `client-framework.instructions.md`-referenced files (`ServerParam`, `PlayerParam`) to see real usage.

## Key Abstractions
- `rcsc::ParamMap` / `rcsc::ParamEntity` — [param_map.h](../../rcsc/param/param_map.h) — generic typed option registry; confirmed as the actual backing store for `ServerParam`'s and `PlayerParam`'s command-line/config options.
- `rcsc::ParamParser` (abstract) with `CmdLineParser` / `ConfFileParser` / `RcssParamParser` — feed a `ParamMap` from argv or a conf file.
- `rcss::Factory<Creator,Index,Compare>` + `rcss::AutoReger` — [factory.h](../../rcsc/factory.h) — generic creator registry with stack-per-index (supports override/restore via push/pop), used for pluggable action/formation/analyzer creators elsewhere in the codebase.
- `rcsc::GameMode` — [game_mode.h](../../rcsc/game_mode.h) — client-side referee/playmode state machine; distinct from `rcsc::PlayMode` (raw wire enum in `types.h`).
- `rcsc::Timer` / `rcsc::TimeStamp` — [timer.h](../../rcsc/time/timer.h) — wall-clock elapsed-time measurement (profiling/think-time budgets), unrelated to simulation `GameTime`.
- `rcsc::RGBColor` + `GradationColorProvider`/`GrayScaleProvider`/`ThermoColorProvider` — value→color mapping for debug visualization output (e.g., drawing heat-maps/gradients to the monitor via `dlog`/debug client, not part of the monitor wire protocol itself).
- `rcsc::MonitorCommand` hierarchy — [monitor_command.h](../../rcsc/monitor/monitor_command.h) — commands a monitor client sends to rcssserver.

## Integration Points
- `ServerParam`/`PlayerParam` (client-framework) build a `ParamMap` and hand it to `CmdLineParser`/`ConfFileParser` to populate options from `argv` and `.conf` files — this is the confirmed real location of `ParamMap`.
- `rcss::Factory` (this file, `namespace rcss`) matches the factory pattern mentioned in rcg-log-replay.instructions.md for pluggable creators; note the `rcss::` namespace differs from the rest of the library's `rcsc::` namespace.
- `GameMode::update()` is driven by the raw playmode string parsed out of server sense messages elsewhere in the client, then exposed to decision-making code via `WorldModel` (see world-model-and-agent-core.instructions.md).
- `cli-tools.instructions.md` in this same repo likely documents executables that directly instantiate `CmdLineParser`/`ConfFileParser` — check there for concrete tool-level usage.
- Color providers are typically used by debug-drawing/analyzer code that writes to the debug log format consumed by the monitor (see rcg-log-replay.instructions.md for the log/replay side).

## Build & Test
- Each subdirectory has its own `CMakeLists.txt` and `Makefile.am` (autotools + CMake dual build, consistent with rest of librcsc).
- `color/thermo_color_test.cpp` is a standalone test/demo for `ThermoColorProvider` — check its `CMakeLists.txt` entry to see if it's wired into `ctest` or is a manual smoke-test binary.
- `util/version.cpp` pulls `PACKAGE_NAME`/`VERSION` from generated `config.h` (`HAVE_CONFIG_H` guard) — these values come from the build system (autoconf/CMake version substitution), not hardcoded.

## Logging
- No dedicated logging in these modules themselves; `MonitorCommand` subclasses only build the outgoing wire string (`toCommandString`), they don't log. Debug color output is typically consumed by the `dlog`/debug-client logging path documented elsewhere (agent-core / geometry instructions), not here.

## Important Notes
- **Namespace mismatch**: `factory.h` declares everything under `namespace rcss` (not `rcsc`) — easy to miss when grepping for `rcsc::Factory`.
- **Two distinct "playmode" concepts**: `rcsc::PlayMode` (`types.h:130`, raw server wire enum) vs. `rcsc::GameMode::Type` (`game_mode.h:56`, client-side wrapper enum with different member names, e.g. `KickOff_`/`FreeKick_` instead of server terms) — `GameMode::getServerPlayMode()` is the bridge between them.
- **Do not hand-edit** the copyright string literal in `version.cpp` (explicit comment warning at line 48).
- `Timer`/`TimeStamp` (wall-clock, `time/`) must not be confused with `GameTime` (simulation cycle count, top-level `game_time.h`) — they measure fundamentally different things.
- `ThermoColorProvider` has an orphaned-looking `thermo_color_test.cpp` in the same directory as production code — verify build wiring before assuming it runs in CI.

## See Also
- [client-framework.instructions.md](client-framework.instructions.md) — `ServerParam`/`PlayerParam` consumers of `ParamMap`.
- [rcg-log-replay.instructions.md](rcg-log-replay.instructions.md) — factory-pattern context and monitor/replay log format.
- [cli-tools.instructions.md](cli-tools.instructions.md) — executables using `CmdLineParser`/`ConfFileParser`.
- [world-model-and-agent-core.instructions.md](world-model-and-agent-core.instructions.md) — consumer of `GameMode` state.

---
Part of: [`librcsc copilot-instructions.md`](../copilot-instructions.md)
