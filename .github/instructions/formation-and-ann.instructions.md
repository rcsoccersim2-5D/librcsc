---
applyTo: 'rcsc/formation/**,rcsc/ann/**'
---

# Formation & Positioning Models

## TL;DR
`Formation` is an abstract strategy interface (ball position → 11 player home positions) selected via a name-string factory; only 2 of 9 known implementations are active today, the rest (including all `ann/`-backed ones) live under `obsolete/`.
- Base interface: [`Formation`](../../rcsc/formation/formation.h:52) — pure-virtual `getPosition()`, `getPositions()`, `train()`, `toData()`, `methodName()`.
- Factory: [`Formation::create(name)`](../../rcsc/formation/formation.cpp:238) only wires up `FormationDT::NAME` and `FormationStatic::NAME` — BPN/NGNet/RBF/KNN/CDT/SBSP/UvA are dead code under `rcsc/formation/obsolete/`, unreachable from the factory.
- `ann/` (BPNetwork1, NGNet, RBFNetwork, SIRM/SIRMsModel) are generic numeric models with **no dependency on `formation/`**; the *only* consumers are the obsolete formation subclasses.
- **Open the full file when:** implementing a new interpolation strategy (read `formation.h` + `formation_dt.cpp` as the live template), or reviving an obsolete ann-backed formation (read the matching `obsolete/formation_*.{h,cpp}` + its `ann/*.h`).

## Overview
`rcsc/formation/` models "given the ball position, where should each of my 11 players stand?" — the core of static/dynamic team positioning (home positions), consumed by higher-level strategy code (helios-base `strategy.cpp`, sibling repo). `rcsc/ann/` is a small toolbox of classic function-approximators (feed-forward BPN, RBF network, Normalized Gaussian Network, Sugeno-type SIRM fuzzy model) with zero soccer-specific logic — pure math classes usable from anywhere.

## Architecture
```
Formation (abstract, formation.h)
 ├── FormationDT      (LIVE — formation_dt.h/.cpp)      geom/DelaunayTriangulation + linear interpolation
 ├── FormationStatic  (LIVE — formation_static.h/.cpp)   fixed std::array<Vector2D,11>, ignores focus_point
 └── obsolete/ (compiled out of the main factory, kept for reference/migration)
      ├── FormationBPN    -> rcsc/ann/bpn1.h   (BPNetwork1, 3-layer back-prop net)
      ├── FormationNGNet  -> rcsc/ann/ngnet.h  (NGNet, normalized Gaussian net)
      ├── FormationRBF    -> rcsc/ann/rbf.h    (RBFNetwork, radial basis function net)
      ├── FormationKNN    -> no ann/ dep, own k-nearest-neighbor lookup over samples
      ├── FormationCDT    -> geom/triangulation.h (different from DelaunayTriangulation; constrained DT)
      ├── FormationSBSP   -> geom/rect_2d.h, region/grid-based static positions
      └── FormationUvA    -> no ann/ dep, University-of-Amsterdam style formation
```
Loading pipeline: `FormationParser::create(filepath)` / `FormationParser::parse(filepath)` ([formation_parser.h:109,118](../../rcsc/formation/formation_parser.h:109)) auto-detects a formation config's version/format and dispatches to one of `formation_parser_{csv,json,static,v1,v2,v3}.cpp` — each of these builds a `Formation` via `Formation::create(name)` and then feeds it role/sample data through `Formation::setRole()` / `Formation::train(FormationData)`.

## Patterns & Conventions
- **Factory-by-name string, not polymorphic RTTI**: each concrete class exposes `static const std::string NAME;` and a `static Formation::Ptr create()`; `Formation::create()` is a flat if/else chain (formation.cpp:238-246) — adding a new variant means editing this function AND `#include`-ing its header at the bottom of formation.cpp (see lines 232-233), not just dropping a new file into the directory.
- **1-based player numbers everywhere** (`num` in `[1..11]`), stored 0-based internally in `std::array<T,11>` — every setter validates `num < 1 || 11 < num` and logs to `std::cerr` on failure (see `Formation::setRoleName`, formation.cpp:56).
- **Const-correct dual accessors**: `getPosition(num, focus_point)` for one player vs `getPositions(focus_point, vector&)` for all 11 — both pure virtual, both must be implemented by every subclass.
- **Data/model separation**: `FormationData` (formation_data.h:51) is the plain-old training-sample container (`Data{ball_, players_}` list); `Formation::toData()` converts a *trained* model back into `FormationData` for the editor/export tools; `Formation::train(const FormationData&)` is the inverse (fit model from samples).
- **ann/ classes are header-only-ish numeric utilities** — no `rcsc::` domain types (no `Vector2D` in the ann headers themselves); the formation_* subclass in `obsolete/` is the adapter layer that converts `Vector2D` ball/player coordinates into raw `double` vectors the network consumes and back.

## Key Abstractions
- [`Formation`](../../rcsc/formation/formation.h:52) — abstract base. Key members: `M_role_names/M_role_types/M_position_pairs` (`std::array<_,11>`), `M_version`. Key virtuals: `methodName()`, `getPosition()`, `getPositions()`, `train()`, `toData()`, `printData()`.
- [`FormationDT`](../../rcsc/formation/formation_dt.h:45) — LIVE default. Holds `std::vector<FormationData::Data> M_points` (samples) + `DelaunayTriangulation M_triangulation` (rcsc/geom/delaunay_triangulation.h). `getPosition()` locates the triangle containing `focus_point` and does `interpolate()` (barycentric-style) between the triangle's 3 sample points (private helper, formation_dt.h:120).
- [`FormationStatic`](../../rcsc/formation/formation_static.h:44) — LIVE fallback. `std::array<Vector2D,11> M_positions`; `getPosition()` ignores `focus_point` entirely and returns the fixed slot.
- `BPNetwork1` (rcsc/ann/bpn1.h:154) — classic 3-layer (in/hidden/out) back-prop net, `<array>`/`<numeric>` based, no external deps.
- `NGNet` (rcsc/ann/ngnet.h:48) — normalized Gaussian RBF-style network, used by `FormationNGNet`.
- `RBFNetwork` (rcsc/ann/rbf.h:49) — radial basis function net; note it depends on `<boost/array.hpp>` (the only ann/ header with a Boost dependency — check Boost is linked if reviving `FormationRBF`).
- `SIRM` / `SIRMsModel` (rcsc/ann/sirm.h:35, rcsc/ann/sirms_model.h:41) — single-input rule-module fuzzy model; **not referenced by any formation_* class** (grep confirms no `#include <rcsc/ann/sirm` in formation/ or obsolete/) — currently an orphaned utility, presumably used by external/helios-base code or unused entirely.
- `RoleType` (rcsc/formation/role_type.h:43) — `enum Type{Goalie,Defender,MidFielder,Forward,Unknown}` + `enum Side{Center=0,Left=-1,Right=1}`, attached per-position-number to a `Formation` via `setRoleType()`.
- `FormationData::Data` (formation_data.h:58) — one training sample: `ball_` (Vector2D) + `players_` (`vector<Vector2D>`, 11 entries) + `index_`.

## Integration Points
- **Input**: a focus point `Vector2D` (per doc comments "usually ball position", sourced from `WorldModel::ball().pos()` per world-model-and-agent-core.instructions.md) plus a target `num` (uniform number).
- **Output**: `Vector2D` home/target position for that player (or all 11 via `getPositions`), used by higher-level strategy/positioning decision code to set each player's move target.
- **Config loading**: `FormationParser` reads on-disk formation config files (see `rcsc/formation/test/example-*.conf` for v1/v2/v3/csv/json/static sample formats) and builds+trains the right `Formation` subclass.
- **Geometry dependency**: `FormationDT` directly depends on `rcsc::DelaunayTriangulation` (rcsc/geom/delaunay_triangulation.h) — confirmed link to geometry.instructions.md's documented DT usage.
- **Cross-repo**: helios-base (sibling repo, not read here) is the actual consumer — its `strategy.cpp` / formation config files (`formation-dt.conf` etc.) select and drive `Formation::create()` + `getPosition()` at decision time.

## Build & Test
- Build via `rcsc/formation/CMakeLists.txt` / `Makefile.am` — only lists the LIVE files (`formation.cpp`, `formation_data.cpp`, `formation_dt.cpp`, `formation_static.cpp`, `formation_parser*.cpp`, `role_type.cpp`); `obsolete/*.cpp` are excluded from the build (confirmed no ann/bpn1.h etc. reachable from a default build).
- `rcsc/formation/test/` holds sample config files, not unit tests: `example-{csv-dt,csv-static,json,static,v1,v2,v3}.conf` — use these to exercise `FormationParser::parse()` manually.
- No dedicated test binary found for `ann/`; validate BPN/RBF/NGNet manually if reviving `obsolete/formation_*`.

## Logging
- Setter failures (`setRoleName`, `setRoleType`, `setPositionPair`) print directly to `std::cerr` with a `(Formation::<method>) <message>` prefix (formation.cpp:61,82,100) — no `rcsc::Logger`/flag-based logging in this subsystem.

## Important Notes
- **Do not assume BPN/NGNet/RBF/KNN/CDT/SBSP/UvA are live** — they exist only under `rcsc/formation/obsolete/` and are unreachable via `Formation::create()`; treat them as reference implementations, not runtime behavior, unless the build files are changed to include them and the factory function is edited.
- `FormationCDT` uses `rcsc/geom/triangulation.h` — a **different** class from `DelaunayTriangulation` used by the live `FormationDT` (rcsc/geom/delaunay_triangulation.h); don't conflate the two when reading geometry.instructions.md.
- `rcsc/ann/rbf.h` pulls in `<boost/array.hpp>` — the only Boost dependency in `ann/`; a build-system consideration if `FormationRBF` is ever revived.
- Position numbers are validated as `[1,11]` inclusive everywhere; passing `0` or `12+` silently returns a default-constructed value (not an exception) from const accessors like `roleName()`/`roleType()`/`pairedNumber()` (formation.h:130-159), but setters explicitly reject and log.
- `SIRM`/`SIRMsModel` in `ann/` have no discovered caller inside `librcsc` — likely dead code here or consumed only by an external project.

## See Also
- [world-model-and-agent-core.instructions.md](world-model-and-agent-core.instructions.md) — source of the `focus_point` (ball position) fed into `getPosition()`.
- [geometry.instructions.md](geometry.instructions.md) — `DelaunayTriangulation`, `Vector2D`, `Rect2D` types consumed/returned by this subsystem.
- Cross-repo: helios-base's `strategy.cpp` and its `formations-dt` / `*.conf` formation configs are the sibling repo's actual runtime consumer of `Formation::create()` + `getPosition()`.

---
Part of: [`librcsc copilot-instructions.md`](../copilot-instructions.md)
