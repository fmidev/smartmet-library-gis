# Geographic Interrupts and Projection Discontinuities

Many map projections have singularities or seams where geographic coordinates cannot be meaningfully projected, or where projecting naively produces wildly incorrect geometry. The library provides a structured mechanism — *interrupts* — to pre-process geometries before projection so they do not cross these problematic areas.

## The Problem

When projecting a polygon that crosses a projection seam (such as the antimeridian at ±180° longitude, or the back hemisphere in an azimuthal projection), a naive coordinate-by-coordinate transform produces incorrect geometry:

- Polygons gain huge spurious edges that span the entire map.
- Lines wrap around the globe in the wrong direction.
- Polygons appear inside-out near poles.

The solution is to *cut* the geometry at the seam before projecting, so each piece projects correctly on its own.

---

## The Interrupt Structure

`#include <gis/Interrupt.h>`

```cpp
namespace Fmi {

struct Interrupt {
  std::list<Box> cuts;                         // rectangular cut regions
  std::shared_ptr<OGRGeometry> andGeometry;    // intersect with this (keep what is inside)
  std::shared_ptr<OGRGeometry> cutGeometry;    // cut with this (remove what is inside)
  std::list<Shape_sptr> shapeClips;            // clip to these shapes
  std::list<Shape_sptr> shapeCuts;             // cut with these shapes

  bool empty() const;
};

// Build the interrupt geometry for a given spatial reference
Interrupt interruptGeometry(const SpatialReference& srs);

// Estimated envelope of the valid projection area
OGREnvelope interruptEnvelope(const SpatialReference& srs);

}
```

### Applying an Interrupt

The typical usage pattern before projecting:

```cpp
Fmi::SpatialReference targetSRS(proj_string);
Fmi::Interrupt interrupt = Fmi::interruptGeometry(targetSRS);

OGRGeometry* geom = ...; // source geometry in WGS84

if (!interrupt.empty()) {
    // 1. Apply rectangular box cuts
    for (const auto& box : interrupt.cuts) {
        OGRGeometry* cut = Fmi::OGR::polycut(*geom, box);
        delete geom;
        geom = cut;
    }

    // 2. Intersect with andGeometry (keep only what is inside valid area)
    if (interrupt.andGeometry) {
        OGRGeometry* clipped = geom->Intersection(interrupt.andGeometry.get());
        delete geom;
        geom = clipped;
    }

    // 3. Cut away cutGeometry (remove singularity region)
    if (interrupt.cutGeometry) {
        OGRGeometry* cut = geom->Difference(interrupt.cutGeometry.get());
        delete geom;
        geom = cut;
    }

    // 4. Shape clips
    for (auto& shape : interrupt.shapeClips) {
        OGRGeometry* clipped = Fmi::OGR::polyclip(*geom, shape);
        delete geom;
        geom = clipped;
    }

    // 5. Shape cuts
    for (auto& shape : interrupt.shapeCuts) {
        OGRGeometry* cut = Fmi::OGR::polycut(*geom, shape);
        delete geom;
        geom = cut;
    }
}

// Now project safely
Fmi::CoordinateTransformation ct(wgs84, targetSRS);
OGRGeometry* projected = ct.transformGeometry(*geom);
```

---

## Projection-Specific Interrupts

The `interruptGeometry` function inspects the PROJ string of the spatial reference and builds the appropriate interrupt for that projection family.

### Geographic (Lat/Lon) — `+proj=longlat`

Cuts at the antimeridian. Any geometry spanning ±180° is split into two pieces that are then individually valid. The cut is a thin rectangle straddling 180°.

### Lambert Azimuthal Equal-Area — `+proj=laea`

Creates a small disc cut at the antipodal point of the projection centre. The antipodal point is at `(lon_0 + 180°, -lat_0)`. Points near the antipode project to infinity in the LAEA; removing a small disc (radius ≈ 1°) prevents projection failures.

The cut disc radius can be thought of as:

```
antipodal_lon = lon_0 + 180°
antipodal_lat = -lat_0
cut_radius    = 1.0°  (empirically sufficient)
```

### Oblique Transverse Mercator — `+proj=ob_tran`

The seam location depends on the sub-projection and the oblique rotation parameters:

- If `o_lat_p ≈ 90°` (transverse), the seam is at `lon_0 + o_lon_p + 180°`.
- Otherwise, the standard antimeridian cut is used.

### Lambert Conformal Conic — `+proj=lcc`

Cuts at the antimeridian band and removes the south-pole region. LCC projections have a well-defined northern or southern focus; geometry near the opposite pole is invalid.

### Near-Side Perspective — `+proj=nsper`

Used for satellite-view maps. Only the hemisphere visible from a satellite at height *h* above the Earth's surface (radius *R*) is valid. The visible disc has half-angle:

```
θ = arccos(R / (R + h))
```

The interrupt creates a circular clip region with radius slightly smaller than θ (reduced by 0.1%) to avoid projection failures at the exact horizon.

### Nicolosi Globular — `+proj=nicol`

Cuts at the antimeridian (±180°). The projection is valid only for one hemisphere centred on `lon_0`.

### Transverse Central Cylindrical — `+proj=tcc`

Cuts at specific longitude bands where the projection becomes undefined.

---

## Antimeridian Wrapping

For projections whose valid domain spans the antimeridian (e.g. a Pacific-centred map), geometries in geographic coordinates may have longitudes outside [−180, 180]. The `CoordinateTransformation::transformGeometry` method handles this by:

1. Getting the geometry envelope.
2. If `envelope.MinX < -180`: extract the part in [−540, −180], translate it by +360° to move it back to [−180, 180], and merge.
3. If `envelope.MaxX > 180`: extract the part in [180, 540], translate it by −360°, and merge.

This ensures that geometries that cross the antimeridian are split and each half is placed in the canonical [−180, 180] range before projection.

---

## Pole Handling in Spherical Clipping

When a polygon boundary crosses a pole in geographic coordinates, the clip algorithm explicitly steps along the 90°N or −90°S latitude line to close the ring correctly:

```
For north-pole closure:
  step poleward in latitude until reaching 90°N
  step along 90°N from the exit longitude to the entry longitude
  step equatorward to the entry latitude

For south-pole closure: symmetric, stepping to -90°S
```

The step size along the pole is 10° of longitude, keeping boundary rings smooth enough to project without visible artifacts.

---

## Densification and Interrupt Interaction

`GeometryProjector` (see [gis-projection.md](gis-projection.md)) applies interrupt cuts internally before densifying and projecting. This means you typically do not need to apply `interruptGeometry` manually if you use `GeometryProjector`.

When constructing a `GeometryProjector` you may still want to call `interruptEnvelope` to get a bounding box for `setProjectedBounds`, which tells the projector where to clip results:

```cpp
Fmi::SpatialReference targetSRS(proj_string);
OGREnvelope env = Fmi::interruptEnvelope(targetSRS);

Fmi::GeometryProjector projector(wgs84.get(), targetSRS.get());
projector.setProjectedBounds(env.MinX, env.MinY, env.MaxX, env.MaxY);
```

---

## Debugging Interrupts

To inspect what interrupt a spatial reference produces:

```cpp
Fmi::SpatialReference srs("+proj=laea +lat_0=52 +lon_0=10");
Fmi::Interrupt interrupt = Fmi::interruptGeometry(srs);

std::cout << "box cuts: " << interrupt.cuts.size() << "\n";
std::cout << "shape clips: " << interrupt.shapeClips.size() << "\n";
std::cout << "shape cuts: " << interrupt.shapeCuts.size() << "\n";
std::cout << "has andGeometry: " << (interrupt.andGeometry != nullptr) << "\n";
std::cout << "has cutGeometry: " << (interrupt.cutGeometry != nullptr) << "\n";
```
