# librcsc — Copilot Instructions

## TL;DR
librcsc is the shared C++ CLIENT library for the RoboCup Soccer Simulation 2D league: it provides the network transport, world-model belief-state, command-building, formation/positioning models, and CLang tactical-language plumbing that any agent team (e.g. sibling repo `helios-base`) subclasses to talk to `rcssserver` (the game server, another sibling repo).
- Entry classes: `rcsc::SoccerAgent` (abstract) → `rcsc::PlayerAgent`/`CoachAgent`/`TrainerAgent` (concrete per-role bases) — see [client-framework.instructions.md](instructions/client-framework.instructions.md).
- The single most important file is `rcsc/player/world_model.cpp` (5820 lines) — the belief-state engine reconstructing ball/player positions from partial vision + full sense_body — see [world-model-and-agent-core.instructions.md](instructions/world-model-and-agent-core.instructions.md).
- Dual build: Autotools (`./bootstrap && ./configure && make`) + CMake, plus Doxygen docs and pkg-config packaging.
- **Open a component instruction when:** you are about to touch a specific subsystem — several already cross-checked against the sibling `rcssserver` protocol docs and `rcssmonitor`'s own rcg-format library for consistency.

## Project Overview
librcsc lets a C++ program become a RoboCup 2D soccer agent (player, online coach, or trainer) without reimplementing the UDP protocol, sensory-message parsing, or belief-state tracking from scratch. `helios-base` (a sibling repo, a top-competing reference team) is built entirely on top of it — librcsc supplies the reusable "how do I talk to the server and know where the ball is" plumbing, helios-base supplies the actual tactical decision-making.

## Architecture
1. **Connection & agent framework** — `SoccerAgent` (abstract) owns an `AbstractClient`, implemented by `OnlineClient` (live UDP) or `OfflineClient` (log replay). `ServerParam::instance()` (a lazy singleton) is populated from the server's handshake reply, never independently configured. See [client-framework.instructions.md](instructions/client-framework.instructions.md).
2. **World model & per-cycle loop** — `PlayerAgent::parse()` routes incoming `see`/`sense_body`/`hear` messages; `WorldModel::updateJustBeforeDecision()` fuses them into absolute positions via `Localization`+`ObjectTable`; `PlayerAgent::action()` then calls the team-specific `actionImpl()`. See [world-model-and-agent-core.instructions.md](instructions/world-model-and-agent-core.instructions.md).
3. **Action/behavior primitives** — a Body/Neck/View/Focus/Arm independent-effector-slot pattern (`AbstractAction` → `BodyAction`/`NeckAction`/... in `soccer_action.h`), with `KickTable` as the foundational kick-feasibility engine. See [action-behavior-library.instructions.md](instructions/action-behavior-library.instructions.md).
4. **Formation/positioning models** — `Formation` abstract base + name-string factory; only `FormationDT` (Delaunay-triangulation interpolation) and `FormationStatic` are actually wired into the build — many named variants (BPN/NGNet/RBF/KNN/CDT) exist only under `formation/obsolete/`. See [formation-and-ann.instructions.md](instructions/formation-and-ann.instructions.md).
5. **Coach/trainer + CLang** — `CoachAgent`/`TrainerAgent` both subclass `SoccerAgent` and share one `CoachWorldModel` (full-field `(see_global)` snapshot, no partial-view fusion). The client-side CLang parser is asymmetric: coaches only *build* outgoing CLang, players *parse* CLang heard from their coach. See [coach-trainer-clang.instructions.md](instructions/coach-trainer-clang.instructions.md).
6. **Geometry** — `Vector2D`/`AngleDeg`/`Rect2D`/`Matrix2D`/Voronoi/Delaunay (namespace `rcsc`, NOT interchangeable with `rcssmonitor`'s own geometry library). See [geometry.instructions.md](instructions/geometry.instructions.md).
7. **RCG log format & compressed I/O** — same lineage as `rcssmonitor`'s `rcss/rcg/` parser (matching class names), but librcsc additionally ships a write-side `Serializer`. `rcsc/gz/` mirrors rcssserver's `rcss/gzip/`. See [rcg-log-replay.instructions.md](instructions/rcg-log-replay.instructions.md).
8. **CLI tools & examples** — standalone `.rcg` inspector/converter binaries in `src/`, plus teaching examples in `example/`. See [cli-tools.instructions.md](instructions/cli-tools.instructions.md).
9. **Infrastructure utilities** — `ParamMap` (the actual config-registry class behind `ServerParam`), `GameMode` (client-side wrapper mirroring the server's `PlayMode`), `rcss::Factory` template, color/timer/math helpers. See [infrastructure-utils.instructions.md](instructions/infrastructure-utils.instructions.md).

## Build, Test, Run
- **Autotools**: `./bootstrap && ./configure && make && make install` — produces the `librcsc` static/shared library plus the standalone CLI tools in `src/`.
- **CMake**: `cmake . && make` (dual support, same module layout).
- **Packaging**: `librcsc.pc.in` (pkg-config), `librcsc.spec.in` (RPM), `librcsc-config.in`/`librcscenv.in` (legacy discovery scripts) — `helios-base` and other consumer teams discover librcsc via these.
- **Docs**: `Doxyfile.in` generates API docs from source comments — a useful secondary reference alongside these instructions.
- Focused CppUnit coverage is available for the v20 ball-observation parser when configured with `--enable-unit-test`; validate changes by building the library and standalone CLI tools, running that test, and (when touching agent-facing code) building `helios-base` against the local library.

## Conventions
- **Singletons for cross-cutting config**: `ServerParam::instance()`, `KickTable::instance()` — populated once, shared process-wide; do not add per-agent copies.
- **Effector-slot action pattern**: new behaviors should implement `BodyAction`/`NeckAction`/`ViewAction`/`FocusAction`, not a monolithic "do everything" action — see action-behavior-library.
- **Obsolete-but-present code**: many year-suffixed or historically-named variants (`body_dribble2007.cpp`, `formation/obsolete/*` BPN/NGNet/RBF/KNN/CDT) remain in the tree but are excluded from the build — always cross-check `CMakeLists.txt`/`Makefile.am` before assuming a file is live.
- **`rcss::Factory` vs `rcsc::` namespace**: the generic factory template lives in `namespace rcss` (`factory.h`) even though almost everything else in this library is `namespace rcsc` — an easy mix-up.

## Instruction Index
Read the scoped instruction for the area you are working in (they are NOT auto-loaded — open them on demand):

| Instruction | Covers (applyTo) | Summary | Path |
|-------------|------------------|---------|------|
| Client & Agent Network Framework | `rcsc/common/**, rcsc/net/**` | `SoccerAgent`/`AbstractClient`/`OnlineClient`/`OfflineClient` hierarchy; `ServerParam` handshake population; say/hear messages | [client-framework.instructions.md](instructions/client-framework.instructions.md) |
| World Model & Agent Core | `rcsc/player/**` | `WorldModel` belief-state fusion (5126 lines), `PlayerAgent`'s per-cycle dispatch, interception prediction (`InterceptSimulatorSelfV17`) | [world-model-and-agent-core.instructions.md](instructions/world-model-and-agent-core.instructions.md) |
| Action & Behavior Primitive Library | `rcsc/action/**` | Body/Neck/View/Focus/Arm effector-slot pattern; `KickTable`; dribble/pass/go-to-point behaviors | [action-behavior-library.instructions.md](instructions/action-behavior-library.instructions.md) |
| Formation & Positioning Models | `rcsc/formation/**, rcsc/ann/**` | `Formation` abstract base + factory; only `FormationDT`/`FormationStatic` are build-wired; BPN/NGNet/RBF variants are obsolete | [formation-and-ann.instructions.md](instructions/formation-and-ann.instructions.md) |
| Geometry & Spatial Primitives | `rcsc/geom/**` | `Vector2D`/`AngleDeg`/`Rect2D`/`Matrix2D`, Voronoi/Delaunay spatial structures | [geometry.instructions.md](instructions/geometry.instructions.md) |
| Coach & Trainer Agents + Client-Side CLang | `rcsc/coach/**, rcsc/trainer/**, rcsc/clang/**` | `CoachAgent`/`TrainerAgent` share one `CoachWorldModel`; asymmetric CLang build-vs-parse; `PlayerTypeAnalyzer` for opponent type inference | [coach-trainer-clang.instructions.md](instructions/coach-trainer-clang.instructions.md) |
| RCG Log Format & Compressed I/O | `rcsc/rcg/**, rcsc/gz/**` | Same lineage as rcssmonitor's rcg parser, plus a write-side `Serializer`; `rcsc/gz/` mirrors rcssserver's gzip wrappers | [rcg-log-replay.instructions.md](instructions/rcg-log-replay.instructions.md) |
| CLI Tools & Example Usage | `src/**, example/**` | Standalone `.rcg` inspector/converter binaries; `example/` API usage demos | [cli-tools.instructions.md](instructions/cli-tools.instructions.md) |
| Infrastructure Utilities | `rcsc/monitor/**, rcsc/color/**, rcsc/param/**, rcsc/time/**, rcsc/util/**, plus top-level headers` | `ParamMap` config registry, `GameMode` (mirrors server `PlayMode`), `rcss::Factory` template, color/timer/math helpers | [infrastructure-utils.instructions.md](instructions/infrastructure-utils.instructions.md) |

## Related Repositories (sibling directories under `D:\workspace\robo\ss2d`, NOT part of this unit)
| Repo | Path | Role |
|------|------|------|
| rcssserver | `../../rcssserver` | The game server this library's clients connect to (UDP 6000 players/monitor, 6002 online coach). Its `monitor-protocol`/`networking-io`/`config-params`/`coach-language-clang` instructions are the server-side counterparts referenced throughout this unit's docs. |
| rcssmonitor | `../../rcssmonitor` | Visualization client with its OWN `rcss/rcg/` parser — same lineage/class names as this library's `rcsc/rcg/`, confirmed during generation of `rcg-log-replay.instructions.md`. |
| helios-base | `../../helios-base` | Sample/reference agent team built entirely on top of this library — subclasses `PlayerAgent`/`CoachAgent`/`TrainerAgent`, consumes `Formation`, builds on `action/` primitives. (Documented separately, see its own `.github/copilot-instructions.md`.) |

**Default ports** (consistent across the ecosystem): `6000` UDP — players/trainer/monitor; `6002` — online coach; `6032` — debug/logging side-channel.

## Where to Look
- Component instructions: `.github/instructions/` (indexed above).
- No skills directory in this unit by design (instructions only, per project scope).
- Doxygen API docs: generate via `Doxyfile.in` for exhaustive per-symbol reference alongside these architectural instructions.
