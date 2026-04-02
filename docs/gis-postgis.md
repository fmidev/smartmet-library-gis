# PostGIS Integration

## PostGIS Namespace

`#include <gis/PostGIS.h>` (Linux only)

Reads geometries and feature attributes from a PostGIS database via GDAL/OGR.

The caller is responsible for calling `OGRRegisterAll()` (or `GDALAllRegister()`) before any PostGIS reads.

### Read geometries with attributes

```cpp
#include <gis/PostGIS.h>
#include <gis/Host.h>
#include <gis/Types.h>

GDALDataPtr conn = ...; // opened connection (see Host below)

// Specify which attribute columns to fetch
std::set<std::string> fields = {"name", "population"};

// Optional WHERE clause
std::optional<std::string> where = "population > 100000";

// Read features from table "public.cities"
Fmi::Features features = Fmi::PostGIS::read(
    &targetSRS,    // reproject geometries into this SRS (may be nullptr)
    conn,
    "public.cities",
    fields,
    where);

for (const auto& f : features) {
    OGRGeometryPtr geom = f->geom;
    auto name = std::get<std::string>(f->attributes.at("name"));
}
```

### Read geometry only

```cpp
// Returns the union of all matching geometries as a single OGRGeometry
OGRGeometryPtr geom = Fmi::PostGIS::read(
    &targetSRS,
    conn,
    "public.coastlines",
    where);
```

---

## Feature Types

`#include <gis/Types.h>`

```cpp
using Attribute = std::variant<int, double, std::string, Fmi::DateTime>;

struct Feature {
    OGRGeometryPtr geom;
    std::map<std::string, Attribute> attributes;
};

using FeaturePtr = std::shared_ptr<Feature>;
using Features   = std::vector<FeaturePtr>;
```

---

## Host — Database Connection

`#include <gis/Host.h>`

Manages connection parameters for a PostGIS database. Call `connect()` to open a `GDALDataPtr` that is passed to `PostGIS::read`.

```cpp
Fmi::Host host("localhost", "gis", "reader", "secret", 5432 /*port, optional*/);
GDALDataPtr conn = host.connect();

Fmi::Features features = Fmi::PostGIS::read(&srs, conn, "public.cities", fields);
```
