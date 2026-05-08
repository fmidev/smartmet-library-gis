# Polygon Amalgamation and Simplification

## Overview

Two header-only utilities thin polygon datasets before rendering:

- **`Fmi::GeometryAmalgamator`** — merges nearby polygons by triangulating the gaps between them with a constrained Delaunay triangulation (CDT). Gap triangles whose edges are all shorter than a threshold are accepted as part of the merged outline. The intent: archipelagos and dense coastlines, where individual islands are too small to render but a merged silhouette of "the island cluster" reads correctly. Equivalent in effect to the old shapetools `amalgamate` tool but ~200× faster.
- **`Fmi::GeometrySimplifier`** — Visvalingam-Whyatt vertex removal. Repeatedly drops the vertex whose triangle with its neighbours has the smallest area until the area exceeds a tolerance. Optionally preserves shared boundaries between adjacent polygons (used by the contour engine; not by the amalgamator's per-polygon output).

Both are in `Fmi::` and live alongside the rest of `gis/`.

```cpp
#include <gis/GeometryAmalgamator.h>
#include <gis/GeometrySimplifier.h>

Fmi::GeometryAmalgamator amalg;
amalg.lengthLimit(0.01);              // CRS units (e.g. degrees)
amalg.areaLimit(0.0);                 // CRS units squared (0 disables)
amalg.mainlandArea(1000);             // km²: bypass the cluster CDT for big polygons
amalg.mainlandAmalgamate(true);       // run a per-polygon CDT on bypassed mainlands

std::vector<OGRGeometryPtr> geoms = ...;
amalg.apply(geoms);                    // mutates in place

Fmi::GeometrySimplifier simp;
simp.type(Fmi::GeometrySimplifier::Type::VisvalingamWhyatt);
simp.tolerance(1.5e-4);                // CRS units squared
simp.apply(geoms);
```

The amalgamator's pipeline is:

```
input polygons
  └─> flatten MultiPolygon / GeometryCollection into a flat OGRPolygon list
  └─> mainland mask (geographic km² area >= mainlandArea)
  └─> r-tree on non-mainland envelopes
  └─> union-find: cluster polygons whose envelopes are within lengthLimit
  └─> per-cluster CDT
       └─> classify triangles: inside-polygon, gap (depth 0 with all edges <= lengthLimit), reject
       └─> walk boundary half-edges of accepted region into rings
       └─> separate exteriors / holes (signed area, r-tree-prefiltered point-in-ring)
       └─> emit OGRPolygon per exterior, subject to areaLimit
  └─> mainland: emit unchanged OR run single-polygon CDT (mainlandAmalgamate)
```

---

## Algorithm — why CDT, why not GEOS UnaryUnion

The naive way to merge nearby polygons is to buffer them outward by half the gap distance, run `OGRGeometry::UnaryUnion`, then buffer inward. This is slow on dense input (the Northern-Baltic GSHHG-h test took ~5 minutes and sometimes hung) and produces rounded corners.

Constrained Delaunay does it in one pass:

1. **Constraints**: every polygon edge is fed to the CDT as a constraint, optionally densified so no edge is longer than `lengthLimit / 2`. Densification lets the CDT see more candidate gap triangles in narrow channels.
2. **Triangle classification**: after triangulation, each triangle has a depth (CDT-cpp's `calculateTriangleDepths`). Odd depth means inside a polygon — always accepted. Depth 0 means outside any polygon — accepted only if all three edges are `<= lengthLimit`. Even depths > 0 are inside holes — rejected.
3. **Boundary walk**: instead of building one OGRPolygon per accepted triangle and calling `UnaryUnion` to glue them (the old approach, dominant cost), we walk the half-edges of the accepted region directly. CDT-cpp stores triangle vertices CCW, so for triangle `t` with neighbour `n` across edge `(v[k], v[(k+1)%3])`, that edge has triangle `t` on its left. When `n` is rejected (or doesn't exist), the edge is on the merged outline. Following these half-edges with the accepted triangle on the left produces CCW exterior rings and CW hole rings — both ready to drop straight into an OGRPolygon. No UnaryUnion needed.
4. **Hole assignment**: exteriors and holes come out of the boundary walk unsorted. Each hole is assigned to the smallest exterior containing its first vertex, prefiltered by an r-tree on exterior bounding boxes (point-in-ring is O(n) per ring).

The boundary walk is the structural change that took the algorithm from minutes to seconds; everything else is constant-factor work on top of that.

### Pinch vertices

A vertex shared by several disjoint accepted regions, or by the inner and outer boundary of a thin strip, can have multiple outgoing boundary half-edges. The half-edge map is therefore `vertex -> std::vector<vertex>`, and the walk pops one outgoing edge per visit. Two rings that share a pinch vertex may stitch into one ring with a self-touching point — visually identical at any practical render scale.

### Densified-midpoint cleanup

Edge densification in step 1 inserts midpoints on every polygon edge longer than `lengthLimit / 2`. Many of those midpoints survive into the boundary ring as redundant collinear vertices. A `drop_collinear` pass strips them before emitting.

---

## Mainland bypass and self-amalgamation

On dense archipelago datasets a few huge polygons hold most of the input vertices: in the GSHHG-h Northern-Baltic test (lon 18°–27°, lat 59°–65°, ~17 000 polygons), three polygons hold 62 % of all 226 000 vertices. Those polygons cannot benefit from inter-polygon merging — the merged outline of "Finland + a tiny skerry" is essentially Finland — so dropping them from the cluster CDT shrinks the input dramatically.

`mainlandArea(km²)` switches them to a bypass path: identified by geographic area (Green's theorem on the sphere for geographic CRSs, planar `get_Area()` otherwise), they skip clustering, the global CDT, and `UnaryUnion`. The remaining islands amalgamate as before; mainland polygons are appended unchanged at the end, subject to the same `areaLimit` filter.

`mainlandAmalgamate(true)` adds back what the bypass loses: bays. A coastline emitted unchanged keeps every concavity, which can read as "fingered" or unrealistic at low zoom — the gap-triangle pass would normally close shallow inlets shorter than `lengthLimit`. With this flag, each bypassed mainland polygon is run through the CDT *by itself* (no clustering, no neighbour search, just the polygon's own constraints), so depth-0 triangles inside its concavities with short enough edges get accepted into the merged outline. This restores the bay-closing effect at a fraction of the cost of including the mainland in the global cluster CDT, since there are no inter-polygon triangles to triangulate.

The decision tree:

| Setting | What happens to mainland | Cost on Northern-Baltic test |
|---|---|---|
| `mainlandArea = 0` (default) | included in cluster CDT, full amalgamation | ~5 min, often hangs |
| `mainlandArea = 1000` only | emitted unchanged, bays stay open | 0.34 s |
| `mainlandArea = 1000` + `mainlandAmalgamate = true` | per-polygon CDT, bays close | 1.5 s |

The middle row is the fastest but the resulting coastline keeps every bay; the bottom row is the recommended default for archipelago/coastline rendering.

---

## Optimization journey

The current implementation went through several rounds. Each change is recorded here so future work understands the design constraints.

| Step | Wall time on the test | Speedup | Change |
|---|---|---|---|
| Original `OGRGeometry::UnaryUnion` over per-triangle polygons | ~5 min (often hung) | 1× | One-OGRPolygon-per-triangle then GEOS union — dominant cost in profiles. |
| + envelope clustering + minTotalArea filter | ~2 min | ~2.5× | Union-find on r-tree-overlap envelopes; per-cluster CDT instead of one global CDT. Cluster total-area pre-filter drops thousands of small clusters whose merged result couldn't pass the downstream km² filter anyway. |
| + boundary half-edge walk (no UnaryUnion) | ~6 s | ~50× | The structural change. Walk accepted-region boundary directly out of the CDT's neighbour pointers; skip the OGR/GEOS round-trip entirely. |
| + bbox-prefiltered hole classification | 2.3 s | ~130× | r-tree on exterior bounding boxes drops ~99 % of point-in-ring tests on the dense case. |
| + GeometryProjector hoist out of `PostGIS::read` | (amalgamation unchanged) | upstream win | The PROJ-database CRS-equivalence search in `osgeo::proj::metadata::Identifier::isEquivalentName` was 44 % of CPU on a 17k-feature read; building the projector once outside the per-feature loop dropped baseline from 3.7 s to 0.34 s. |
| + `ankerl::unordered_dense` for hash maps/sets | 1.58 s | ~190× | Vendored single-header MIT-licensed flat hash map. Replaces `std::unordered_{map,set}` in CDTUtils.h (`EdgeUSet`, `TriIndUSet`, `TriIndUMap` — every internal hash container) and in the amalgamator's vertex-dedup `VertexMap` and boundary-walk `next_v` map. Bucket-based `std::unordered_set<CDT::Edge>::find` was 10.97 % self-time; ankerl drops it to 2.66 %. |
| + mainland bypass | 0.34 s | ~880× | Skip the polygons holding most of the vertices. |
| + mainland self-amalgamation | 1.5 s | ~200× *and* better looking | Trade ~1.16 s back to close mainland bays — the only variant that produces a coastline residents recognize. |

`perf record -F 200 --call-graph dwarf -p <pid>` was the workhorse, attached to a long-running PluginTest. Wrapping PluginTest with `perf record` directly lost the `perf.data` file because TestRunner's SIGTERM didn't give perf time to flush; attaching to a running PID worked reliably.

---

## Tuning guide

`apply()` is exposed via three knobs that are usually all you need to set:

### `lengthLimit` — the gap-bridging distance

Maximum edge length of accepted gap triangles, in **CRS coordinate units**. For `EPSG:4326` that means decimal degrees, not metres.

| Use case | Suggested value (geographic) | Suggested value (projected metric) |
|---|---|---|
| Stockholm/Helsinki-style outer archipelago | `0.01` (≈ 0.6 km at 60° N) | `600` |
| Coastal mainland with many small inlets | `0.005`–`0.01` (≈ 300–600 m at 60° N) | `300`–`600` |
| Lake-and-island datasets at a regional scale | `0.05` (≈ 3 km at 60° N) | `3000` |

Larger values dissolve more of the archipelago into solid landmasses; smaller values keep more individual islands visible. The right value depends on the rendering pixel scale: if `lengthLimit` is much smaller than one pixel, the pass is a no-op.

### `areaLimit` — pre-filter inside the amalgamation

Drop polygons whose area is below this threshold, in **CRS coordinate units squared**. Applied as part of the amalgamation pipeline (singleton clusters that fall below this are skipped; merged outlines too). Mainly useful when you want a single area cutoff rolled into the amalgamation step.

For most callers it is more natural to use the WMS plugin's `minarea` (km²), which is applied *after* the amalgamation. `areaLimit` is independent of CRS and harder to reason about; prefer `minarea` for archipelago rendering.

### `mainlandArea` — speedup threshold

Polygons whose individual *geographic* area meets or exceeds this km² value bypass the cluster CDT. Default 0 disables. Pick a value that separates "the mainland" from individual islands — for European coastline data, 1000 km² is a good default (Finland ≈ 338 000 km², the largest individual islands like Saaremaa or Gotland are 500–2 700 km² and benefit from being in the cluster, while Åland archipelago islands are < 700 km² each).

### `mainlandAmalgamate` — bay-closing on bypassed mainland

Boolean, default false. Has no effect unless `mainlandArea` is set. When true, each above-threshold polygon goes through a per-polygon CDT pass so its bays close up below the gap-triangle threshold. Costs ~220 ms extra on the test bbox; for archipelago/coastline rendering it is the recommended default — the visual difference is immediately obvious to anyone familiar with the coast.

### Putting it together

For archipelago or dense-coastline rendering on geographic CRSs:

```cpp
amalg.lengthLimit(0.01);            // 0.6 km gap at 60° N
amalg.mainlandArea(1000);           // bypass huge polygons
amalg.mainlandAmalgamate(true);     // close mainland bays
amalg.apply(geoms);
// then in the WMS plugin: minarea = 2 km² to remove un-merged slivers
```

Followed optionally by Visvalingam-Whyatt at `tolerance = 1`–`3` pixels (the WMS plugin converts pixels to a CRS-units² area threshold once per request) to thin redundant vertices on the merged outlines.

---

## See also

- [WMS plugin reference: Map amalgamation and simplification](../../brainstorm/plugins/wms/docs/reference.md#map-amalgamation-and-simplification) — caller-side documentation, gallery PNGs, and the `minarea` / `mindistance` filters that complement the amalgamator.
- `gis/CDT/` — the vendored CDT-cpp constrained Delaunay implementation, with `CDTUtils.h` aliased to `ankerl::unordered_dense` for hot hash containers.
- `include/ankerl/unordered_dense.h` — the vendored single-header flat hash map.
