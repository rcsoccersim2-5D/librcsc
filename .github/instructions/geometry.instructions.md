---
applyTo: 'rcsc/geom/**'
---

# Geometry & Spatial Primitives

## TL;DR
`rcsc/geom/` is librcsc's dependency-free 2D math kernel — every coordinate, angle, and shape used by the agent (`player/`, `action/`, `formation/`) and by `helios-base` flows through these ~30 header/source pairs (plus tests) in `namespace rcsc {}`.
- Core primitives: `Vector2D` ([vector_2d.h](../../rcsc/geom/vector_2d.h)), `AngleDeg` ([angle_deg.h](../../rcsc/geom/angle_deg.h)), `Rect2D`, `Line2D`, `Circle2D`, `Segment2D`, `Ray2D`, `Sector2D`, `Triangle2D`, `Polygon2D`, `Matrix2D`, `Size2D`.
- Polymorphic shape hierarchy: abstract `Region2D` ([region_2d.h](../../rcsc/geom/region_2d.h)) is the base of `Rect2D`, `Circle2D`, `Sector2D`, `Triangle2D`, `Polygon2D`, and composite `UnitedRegion2D`/`IntersectedRegion2D` ([composite_region_2d.h](../../rcsc/geom/composite_region_2d.h)).
- Advanced spatial structures: `DelaunayTriangulation` + `VoronoiDiagram` (used by `rcsc/formation/formation_dt.h` for Delaunay-based formation interpolation).
- **AngleDeg always normalizes to `[-180, 180]` degrees** — never assume radians or `[0, 360)`.
- **Open the full file when:** you need exact operator overload semantics, the Delaunay incremental-insertion algorithm, or Voronoi edge/ray construction from the dual graph.

## Overview
This directory is librcsc's foundational, **dependency-free** (no boost, no external geometry lib) 2D math library. It has zero dependencies on `player/`, `action/`, or `formation/` — dependencies flow strictly the other way. Roughly 30 primitive classes plus 9 `test_*.cpp` unit tests (built via `CMakeLists.txt` / `Makefile.am` in this directory, see [CMakeLists.txt](../../rcsc/geom/CMakeLists.txt)).

Everything is declared in `namespace rcsc { ... }`. This is **structurally parallel to but a completely different, non-interchangeable codebase from** `rcssmonitor`'s own small geometry library (`rcssmonitor/src/vector_2d.h`, global namespace, already documented separately) — do not mix the two `Vector2D`/`AngleDeg` types across repos.

## Architecture
Layering (lower depends on nothing above it):
1. **`AngleDeg`** ([angle_deg.h](../../rcsc/geom/angle_deg.h)) — standalone degree wrapper, no dependency on `Vector2D`.
2. **`Vector2D`** ([vector_2d.h](../../rcsc/geom/vector_2d.h)) — depends only on `AngleDeg` (`#include <rcsc/geom/angle_deg.h>`, line 35).
3. **Line-like primitives** built on `Vector2D`/`AngleDeg`: `Line2D`, `Ray2D`, `Segment2D`, `Circle2D`, `Triangle2D`, `Sector2D`, `Rect2D`, `Polygon2D`.
4. **Abstract shape interface**: `Region2D` (region_2d.h) — pure virtual `area()` and `contains(const Vector2D&)`. `Rect2D` (rect_2d.h:58), `Circle2D` (circle_2d.h:50), `Sector2D` (sector_2d.h:44), `Triangle2D` (triangle_2d.h:48), `Polygon2D` (polygon_2d.h:46) all `: public Region2D`.
5. **Composite regions**: `UnitedRegion2D` (composite_region_2d.h:46, union of shapes) and `IntersectedRegion2D` (composite_region_2d.h:238, intersection of shapes) — both hold `std::vector<std::shared_ptr<const Region2D>>` so heterogeneous shapes can be tested/combined polymorphically.
6. **Spatial partitioning** (top of the stack): `ConvexHull` ([convex_hull.h](../../rcsc/geom/convex_hull.h)) → `DelaunayTriangulation` ([delaunay_triangulation.h](../../rcsc/geom/delaunay_triangulation.h)) → `VoronoiDiagram` ([voronoi_diagram.h](../../rcsc/geom/voronoi_diagram.h), which owns a `DelaunayTriangulation M_triangulation` member at line 58 and derives Voronoi edges from the Delaunay dual). A second triangle-indexed variant lives in `voronoi_diagram_triangle.h/.cpp`. Generic triangulation interfaces are in `triangulation.h/.cpp`.

## Patterns & Conventions
- **Header-only inline hot path**: most one-line accessors/operators (`x`, `y`, `assign()`, `setPolar()`, `isValid()`, `rotate()`) are defined inline in the `.h`; only algorithmically heavier code sits in the matching `.cpp` (e.g. `delaunay_triangulation.cpp`, `voronoi_diagram.cpp`).
- **Non-explicit implicit-conversion constructors** are intentional, e.g. `AngleDeg(const double deg)` (angle_deg.h:82) — comment explicitly says "NO explicit... `AngleDeg angle = 3.0;`" is allowed by design.
- **Static sentinel/tolerance members** on nearly every class: `Vector2D::EPSILON`, `Vector2D::ERROR_VALUE`, `Vector2D::INVALIDATED` (vector_2d.h:55-61), `AngleDeg::EPSILON/PI/TWO_PI/DEG2RAD/RAD2DEG` (angle_deg.h:60-69), `DelaunayTriangulation::EPSILON` (delaunay_triangulation.h:52).
- **`isValid()` sentinel pattern** instead of exceptions/optional: `Vector2D::isValid()` (vector_2d.h:89) checks `x != ERROR_VALUE && y != ERROR_VALUE` — callers must check validity rather than expect a thrown error.
- **Boost-operator scaffolding commented out**: both `Vector2D` and `AngleDeg` have `// : public boost::addable<...>` etc. left as comments (vector_2d.h:47-50, angle_deg.h:46-53) — operators are hand-rolled instead, confirming the "no external dependency" design goal.
- **`XYCmp` functor** (vector_2d.h:735, `struct XYCmp`) — X-then-Y ordering predicate used to key `std::set<Vector2D>` containers, e.g. `VoronoiDiagram::Vector2DCont = std::set<Vector2D, Vector2D::XYCmp>` (voronoi_diagram.h:50).
- **Raw-pointer-to-`shared_ptr` adoption idiom**: `UnitedRegion2D(const Region2D* r1, const Region2D* r2)` (composite_region_2d.h:64) takes raw pointers documented as "must be a dynamically allocated object" and immediately wraps them in `std::shared_ptr` — a deliberate ownership-transfer convention, not a leak.

## Key Abstractions
| Class | File:line | Role |
|---|---|---|
| `Vector2D` | [vector_2d.h:46](../../rcsc/geom/vector_2d.h) | 2D point/vector; `x,y` public fields; polar via `setPolar()`/`r()`/`th()`; `rotate()`, `dist()`/`dist2()` |
| `AngleDeg` | [angle_deg.h:45](../../rcsc/geom/angle_deg.h) | Degree-based angle, auto-`normalize()`d to `[-180,180]` on every mutation |
| `Region2D` | [region_2d.h:43](../../rcsc/geom/region_2d.h) | Abstract shape base: `area()`, `contains(Vector2D)` |
| `Rect2D` | [rect_2d.h:58](../../rcsc/geom/rect_2d.h) | Axis-aligned rectangle region |
| `Circle2D` | [circle_2d.h:50](../../rcsc/geom/circle_2d.h) | Circle region; fwd-declares `Line2D`/`Ray2D`/`Segment2D` for intersection helpers |
| `Line2D` | [line_2d.h:47](../../rcsc/geom/line_2d.h) | Infinite line (not a `Region2D`) |
| `Ray2D` | [ray_2d.h:44](../../rcsc/geom/ray_2d.h) | Half-line from an origin point/angle |
| `Segment2D` | [segment_2d.h:46](../../rcsc/geom/segment_2d.h) | Bounded line segment |
| `Sector2D` | [sector_2d.h:44](../../rcsc/geom/sector_2d.h) | Annular/pie-slice region (`: public Region2D`) |
| `Triangle2D` | [triangle_2d.h:48](../../rcsc/geom/triangle_2d.h) | Triangle region |
| `Polygon2D` | [polygon_2d.h:46](../../rcsc/geom/polygon_2d.h) | Arbitrary-vertex polygon region |
| `Matrix2D` | [matrix_2d.h:51](../../rcsc/geom/matrix_2d.h) | 2D affine transform matrix; used by `action/body_go_to_point.cpp`, obsolete `body_go_to_point2010.cpp`, `player/intercept_simulator_self_v13.cpp`/`v17.cpp` |
| `Size2D` | [size_2d.h:44](../../rcsc/geom/size_2d.h) | Simple width/height pair |
| `ConvexHull` | [convex_hull.h:42](../../rcsc/geom/convex_hull.h) | Convex hull of a point set (feeds Delaunay/Voronoi boundary handling) |
| `DelaunayTriangulation` | [delaunay_triangulation.h:49](../../rcsc/geom/delaunay_triangulation.h) | Incremental Delaunay triangulation; nested `Vertex` (line 71) and `Triangle`; `ContainedType` enum (`NOT_CONTAINED/CONTAINED/ONLINE/SAME_VERTEX`, line 59) |
| `VoronoiDiagram` | [voronoi_diagram.h:48](../../rcsc/geom/voronoi_diagram.h) | Voronoi diagram derived from a `DelaunayTriangulation` member; produces `M_vertices`, `M_segments` (finite edges), `M_rays` (unbounded edges outside the convex hull), optionally clipped by `M_bounding_rect` |
| `UnitedRegion2D` / `IntersectedRegion2D` | [composite_region_2d.h:46](../../rcsc/geom/composite_region_2d.h) / line 238 | Compose multiple `Region2D` shapes (union / intersection) via `shared_ptr<const Region2D>` |

## Integration Points
- **`rcsc/formation/formation_dt.h`** — the primary consumer of `DelaunayTriangulation` outside `geom/`: holds `DelaunayTriangulation M_triangulation` (formation_dt.h:56) and exposes `triangulation()` plus interpolation helpers keyed by `DelaunayTriangulation::Triangle*` (formation_dt.h:122). This implements Delaunay-based formation-role interpolation (a sibling approach to `formation/formation_ngnet.h`'s neural-net based interpolation — see `formation-and-ann.instructions.md`).
- **`Matrix2D`** is used outside `geom/` in `action/body_go_to_point.cpp`, `action/obsolete/body_go_to_point2010.cpp`, and `player/intercept_simulator_self_v13.cpp` / `intercept_simulator_self_v17.cpp` for rotate/translate math in interception and dash-target calculations.
- **`Vector2D`/`AngleDeg`** are used pervasively across `player/` (world model, self/ball/player object state), `action/` (all `Body*`/`Neck*`/`Arm*` behaviors), and `formation/` — effectively every module in librcsc.
- **No reverse dependency**: `rcsc/geom/` never includes headers from `player/`, `action/`, or `formation/` — keep it that way when editing.

## Build & Test
- Built as part of the `rcsc` static/shared library via [CMakeLists.txt](../../rcsc/geom/CMakeLists.txt) and `Makefile.am` in this directory (autotools + CMake dual build, consistent with the rest of librcsc).
- Unit tests are plain `test_*.cpp` files (not gtest) in this directory: `test_vector_2d.cpp`, `test_rect_2d.cpp`, `test_segment_2d.cpp`, `test_triangle_2d.cpp`, `test_polygon_2d.cpp`, `test_convex_hull.cpp`, `test_rundom_convex_hull.cpp` (sic — typo preserved from source), `test_matrix_2d.cpp`, `test_qhull_delaunay.cpp`, `test_qhull_voronoi.cpp`, `test_voronoi_diagram.cpp`. The `qhull`-named tests suggest cross-checking against the external `qhull` library during development, but `qhull` itself is **not** a runtime dependency of `rcsc/geom`.

## Logging
None. This is a pure math library with no logging/`dlog` calls — logging of geometric state (e.g. formation triangles, intercept points) is done by the *consumers* in `player/`/`action/`/`formation/`, not here.

## Important Notes
- **Angle convention**: `AngleDeg` always normalizes into `[-180, 180]` (see `normalize()`, angle_deg.h:104-122, called from every mutating constructor/assignment/arithmetic op). Comparisons like `isLeftOf()`/`isRightOf()` (angle_deg.h ~248-285) rely on this range — do not bypass `normalize()` or feed raw unnormalized degrees into comparison helpers.
- **`ERROR_VALUE`/`INVALIDATED` sentinel, not exceptions**: check `Vector2D::isValid()` before trusting a vector that may come from a failed calculation (e.g. line intersection with no solution) — the geometry classes generally return a sentinel `Vector2D`/angle rather than throwing.
- **Ownership transfer via raw pointer in `UnitedRegion2D`**: passing a stack-allocated or externally-owned `Region2D*` into its constructor is a use-after-free bug — the constructor assumes the pointer is heap-allocated and takes ownership via `shared_ptr`.
- **Do not confuse with `rcssmonitor`'s geometry lib**: `rcssmonitor/src/vector_2d.h` defines a similarly-named but separate, global-namespace `Vector2D`/`AngleDeg` for the monitor GUI — the two are not interchangeable and this instruction file only covers `rcsc::` (librcsc) types.
- **`helios-base`** (sibling repo, not yet documented) builds directly on top of these primitives (`Vector2D`, `AngleDeg`, `Rect2D`, etc.) for its tactical planning code — any breaking change here (e.g. altering `AngleDeg` normalization range or `Vector2D` layout) has a wide blast radius across both `librcsc`'s own `player`/`action`/`formation` code and `helios-base`.
- Two parallel Voronoi/Delaunay implementations exist: the vector-based `voronoi_diagram.h/.cpp` (segments/rays output) and the triangle-indexed `voronoi_diagram_triangle.h/.cpp` — check which one a caller actually includes before assuming behavior.

## See Also
- `world-model-and-agent-core.instructions.md` — consumers of `Vector2D`/`AngleDeg` for self/ball/player state.
- `action-behavior-library.instructions.md` — `Matrix2D` and `Vector2D` usage in `Body*` behaviors (e.g. `body_go_to_point.cpp`).
- `formation-and-ann.instructions.md` — `DelaunayTriangulation` consumer in `formation_dt.h`, contrasted with NGNet-based formation interpolation.

---
Part of: [`librcsc copilot-instructions.md`](../copilot-instructions.md)
