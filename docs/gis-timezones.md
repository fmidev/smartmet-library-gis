# Coordinate to timezone resolution

`#include <gis/TimeZoneFinder.h>`

`Fmi::TimeZoneFinder` resolves the IANA timezone of any coordinate on the
globe from timezone polygons. It replaces the 1 km `timezone.shz` raster that
was used earlier, which gave wrong answers near borders and elsewhere: the
centre of Tornio resolved to `Europe/Stockholm` instead of `Europe/Helsinki`,
and 0,0 in the Gulf of Guinea to `Europe/Lisbon` (with summer time) instead of
`Etc/GMT`.

```cpp
#include <gis/TimeZoneFinder.h>

Fmi::TimeZoneFinder finder;  // reads Fmi::TimeZoneFinder::default_source

const std::string& tz = finder.zoneName(24.1440, 65.8481);  // "Europe/Helsinki"
std::vector<std::string> all = finder.zoneNames(35.2, 31.65);  // {"Asia/Hebron", "Asia/Jerusalem"}
```

Constructed objects are immutable and hence thread safe. All implementation
details are hidden in the `.cpp` file.

## Data source

**The recommended data is
[timezone-boundary-builder](https://github.com/evansiroky/timezone-boundary-builder),**
which post-processes OpenStreetMap boundary data into polygons for every IANA
timezone. Use its "with-oceans" release (`timezones-with-oceans.shapefile.zip`
on the [releases page](https://github.com/evansiroky/timezone-boundary-builder/releases)),
which covers the entire globe:

* Land zones include the territorial waters, the open sea gets the nautical
  `Etc/GMT±N` zones. See [Semantics at sea](#semantics-at-sea).
* The full "with-oceans" variant keeps every IANA zone. The "now" and "1970"
  variants merge zones that share rules since then, which loses historical
  information that SmartMet needs.

**The default source is the shapefile installed by the smartmet-timezones
RPM:**

```
/usr/share/smartmet/timezones/timezones-with-oceans.shp  (Fmi::TimeZoneFinder::default_source)
```

The default constructor reads it. There is no fallback of any kind: if the
source cannot be read, the constructor throws.

Any other GDAL/OGR vector source works as well, for example a GeoPackage or a
PostGIS table, as long as it has polygons in WGS84 longitude/latitude and an
attribute with the IANA zone name (`tzid` by default):

```cpp
Fmi::TimeZoneFinder::Options options;
options.field = "tzid";                 // attribute with the IANA zone name
options.preferred = {"Asia/Shanghai"};  // winners in disputed areas
Fmi::TimeZoneFinder a("/data/timezones.gpkg", options);             // first layer
Fmi::TimeZoneFinder b("/data/timezones.gpkg", "zones", options);    // named layer

// PostGIS, see gis-postgis.md for Fmi::Host
auto conn = Fmi::Host(host, database, user, password, port).connect();
Fmi::TimeZoneFinder c(*conn->GetLayerByName("public.timezones"), options);
```

| Option | Default | Meaning |
|---|---|---|
| `field` | `tzid` | attribute holding the IANA zone name |
| `max_vertices` | 256 | maximum vertices in a polygon piece; affects only speed and memory |
| `preferred` | empty | zones that win in overlapping (disputed) areas, in order |
| `make_valid` | true | repair invalid geometries with `GEOSMakeValid` |
| `threads` | 0 | build threads, 0 = hardware concurrency |
| `verbose` | false | print statistics and invalid geometries |
| `area` | none | load area `{west, south, east, north}` in degrees, see below |

### Load area

Reading the whole globe takes about 0.7 seconds, which matters for command
line tools. With `Options::area` only the polygons intersecting the area are
read, which takes a few hundredths of a second for a small area:

```cpp
Fmi::TimeZoneFinder::Options options;
options.area = Fmi::TimeZoneFinder::Options::Area{24.0, 65.7, 24.3, 65.95};
Fmi::TimeZoneFinder local(Fmi::TimeZoneFinder::default_source, options);

local.contains(24.1440, 65.8481);  // true
local.zoneName(24.1440, 65.8481);  // "Europe/Helsinki", same as with the whole globe
local.zoneName(28.1790, 59.3772);  // throws, outside the area
```

Every polygon covering a point inside the area intersects the area, hence
results inside the area are identical to those of the whole globe. Searches
outside the area throw instead of returning a wrong zone, and `contains()`
tells whether a search is possible. Areas crossing the antimeridian are not
supported.

All zone names must be known to the tzdata used by SmartMet. The 444 zones of
release 2026d all resolve with tzdata 2026c. New releases may add zones, so
keep tzdata at least as new as the polygons.

### Users

* The [geonames engine](https://github.com/fmidev/smartmet-engine-geonames)
  owns a finder, configured with its `timezones` setting (default source,
  another file, or PostGIS). Plugins use it through
  `getTimeZoneName(lon, lat)` and `getTimeZone(lon, lat)`.
* `qdpoint` in [smartmet-qdtools](https://github.com/fmidev/smartmet-qdtools)
  uses it for local times (option `-z`). It reads the polygons only when a
  local time is first needed, and only for a small area around each
  location; runs with many locations read the whole globe once.

### PostGIS import

```sh
ogr2ogr -f PostgreSQL PG:"host=... dbname=... user=..." \
    /usr/share/smartmet/timezones/timezones-with-oceans.shp -nln public.timezones \
    -nlt PROMOTE_TO_MULTI -lco GEOMETRY_NAME=geom -lco FID=gid -lco PRECISION=NO
```

```sql
select count(*) from timezones where not st_isvalid(geom);  -- expect 0
```

No spatial index is needed, since the whole table is read once. Do not
simplify the geometries or snap them to a coarser grid. Coordinates off the
1e-7 grid are rounded to it (about 1 cm), and shared edges stay consistent
because both sides are rounded identically.

## Validity of the geometries (release 2026d)

The geometries were checked with GEOS 3.13 (`GEOSisValidDetail`) from both the
shapefile and the GeoJSON release. They were checked again in PostGIS 3
(`ST_IsValid`, `ST_IsSimple`) after importing with `ogr2ogr`.

| check | result |
|---|---|
| features / polygons / vertices | 444 / 1355 / 8 254 651 |
| invalid geometries (GEOS, shapefile and GeoJSON) | 0 |
| invalid geometries (PostGIS `ST_IsValid`) | 0 |
| non-simple geometries (PostGIS `ST_IsSimple`) | 0 |
| vertices on the 1e-7 degree OSM grid | 100 % |
| gaps (sum of areas minus 360×180, minus overlaps) | 0 |
| overlapping zone pairs | 134 |

**The individual geometries are valid. The coverage is not a strict
partition, because zones overlap:**

* **Intentional overlaps in disputed areas.** The overlapping area is
  179.7 deg² in total, and almost all of it is disputed territory:
  Xinjiang (`Asia/Shanghai`/`Asia/Urumqi`, 175 deg²), Abyei
  (`Africa/Juba`/`Africa/Khartoum`), South Ossetia/Abkhazia
  (`Asia/Tbilisi`/`Europe/Moscow`), the West Bank
  (`Asia/Hebron`/`Asia/Jerusalem`), the Dollard (`Europe/Amsterdam`/`Europe/Berlin`),
  and a few others.
* **Numerical slivers.** 110 of the overlaps are smaller than 1e-6 deg²
  (under about 1 m² at the equator). They only matter for points exactly on
  a border.

Point searches are therefore well defined, except that a point in an overlap
has more than one answer. `zoneName` picks the first zone listed in
`preferred`, or otherwise the zone with the smallest total area. That gives
the more specific zone, for example `Asia/Urumqi` in Xinjiang and
`Asia/Hebron` in the West Bank. `zoneNames` returns all of them.

Even invalid input would not make the searches meaningless. The crossing
number test gives even-odd answers for self-intersecting rings, which are
wrong only inside the self-intersection. The cutting done while building the
index does need valid input, however. Hence invalid geometries are repaired
with `GEOSMakeValid` by default, and the count is reported in the statistics
and, with `Options::verbose`, on the terminal. Earlier releases of the data set
have had invalid polygons, so the check is worth keeping.

Some zones span the full 360 degrees: America/Adak, Asia/Anadyr and
Antarctica/McMurdo are split at the antimeridian. This is correct in planar
lon/lat. **Store the data as `geometry(MultiPolygon, 4326)`, never as
`geography`.** Geography interprets the edges as great circle arcs, which
changes the polygons.

## Semantics at sea

The data follows the legal convention. Land zones include the territorial
waters (12 nautical miles). Beyond them the zones are the nautical
`Etc/GMT±N` bands, which are 15° wide and have no DST. The old
`timezone.shz` raster assigned named land zones to the seas instead.

| location | timezone.shz | boundary-builder | UTC offset winter/summer |
|---|---|---|---|
| Kvarken, Åland Sea, Utö, off Vaasa | Europe/Helsinki / Mariehamn | same | +2/+3 → +2/+3 |
| Bothnian Sea, mid (19.8E 61.8N) | Europe/Helsinki | Etc/GMT-1 | +2/+3 → +1/+1 |
| Bothnian Bay, mid (22.8E 64.8N) | Europe/Paris | Etc/GMT-2 | +1/+2 → +2/+2 |
| Gulf of Finland, mid | Europe/Helsinki | Etc/GMT-2 | +2/+3 → +2/+2 |
| Baltic Proper east of Gotland | Europe/Paris | Etc/GMT-1 | +1/+2 → +1/+1 |
| North Sea, mid | Europe/London | Etc/GMT | +0/+1 → +0/+0 |
| Gulf of Guinea (0E 0N) | Europe/Lisbon | Etc/GMT | +0/+1 → +0/+0 |

In a sample of 2 million uniformly distributed points:

* In open sea, 47 % of the points get a different UTC offset. About half of
  those differ only in summer, because there is no DST at sea.
* On land and in territorial waters, 10 % of the points get a different UTC
  offset. Most of these are improvements: Antarctic research stations instead
  of `Etc/GMT`, borders, and Xinjiang.

Whether local time at sea should follow the nautical zones is a product
decision. If not, possible remedies for the future are:

1. An additional override layer with a higher priority, for example polygons
   assigning the Gulf of Bothnia to Europe/Helsinki and Europe/Stockholm.
   This needs source priorities in `Fmi::TimeZoneFinder` in addition to the
   zone name priorities.
2. For points that resolve to `Etc/*`, use the nearest non-`Etc` zone within
   a configured distance.

## Search structure

1. **Cutting.** Each zone is cut recursively with world aligned dyadic
   rectangles (`GEOSClipByRect`). Cutting stops when a piece is at most one
   grid cell (0.176°) in size and has at most `max_vertices` vertices.
   Rectangles completely inside a zone are stored without vertices.
2. **Coordinates.** Coordinates are stored as `int32` in units of 1e-7
   degrees. This is the native OSM resolution of the data, so the conversion
   is lossless. The point-in-polygon test is an exact integer crossing number
   test, with 128-bit products. Neighbouring zones share identical edges, and
   the half-open rule (left and bottom edges inside, right and top edges
   outside) assigns a point on a shared edge to exactly one zone. The full
   rectangles and the bounding boxes follow the same rule.
3. **Search grid.** A 2048×1024 grid (0.176°) stores either the zone of the
   cell, when a single full rectangle covers every integer point of the cell,
   or a list of the pieces intersecting the cell. 96 % of the cells are
   uniform.

The build uses all cores, since each zone is cut independently. Command line
tools pay the build time on every run, which is what the load area is for.

### Measurements (24 cores, release 2026d with oceans, max_vertices = 256)

| | |
|---|---|
| build from shapefile / from PostGIS | 0.7 s / 1.35 s |
| memory | 87 MB (9.2 M vertices, 280 k pieces) |
| random global points | 46–50 ns/query |
| random points in a box around Finland | 19–30 ns/query |
| random points within about 1 m of a border (worst case, cold cache) | 0.9–1.1 µs/query |
| agreement with GEOS brute force (200 k uniform + 200 k border points) | exact, except 7 points lying exactly on a border (1e-16° away), where either answer is correct |

### Rejected alternatives

* **A Boost.Geometry R-tree over the pieces.** Queries took 2.2–2.8 µs,
  whatever the parameters (`rstar`, `linear`, `quadratic`, node sizes 4–32,
  packing). Profiling showed that 70 % of the time went to tree traversal and
  under 2 % to the point-in-polygon tests. Once the pieces are aligned to a
  common grid, the grid lookup is 40–50× faster than the tree.
* **Cutting each zone starting from its own envelope.** The tree then mixes
  huge and tiny boxes that are not aligned, and queries become several times
  slower.
* **Smaller pieces** (`max_vertices` 16–64). More memory, and no faster.
