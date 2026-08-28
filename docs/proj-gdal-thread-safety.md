# Thread safety of spatial references in PROJ and GDAL

Analysis of what would have to change upstream for spatial references to be
safely usable from many threads, and what SmartMet Server can do in the
meantime.

Sources analysed (fetched 2026-07-26):

| Project | Version | Commit |
|---|---|---|
| PROJ | 9.9.0-dev | `620ac364f0463756b173e763501ed6a38991e5ab` (2026-07-23) |
| GDAL | 3.14.0-dev | `decb67c35ec249c1bae55f53238d5a69e7eff153` (2026-07-24) |

Runtime experiments were run against the packaged builds actually installed
here, PROJ 9.7.1 (`proj97`) and GDAL 3.12.1 (`gdal312`). Reproducers are in
[`thread-safety/`](thread-safety/); `make run` there reproduces every measured
claim below.

All line numbers refer to the two commits above.


## 1. Summary

The two libraries have opposite problems and they need opposite fixes.

**PROJ is thread-affine by design.** Its documented contract is one
`PJ_CONTEXT` per thread and no sharing of anything
(`docs/source/development/reference/datatypes.rst:54-56`: *"All operations
within a context should be performed in the same thread"*). Five public C++
classes carry an explicit single-thread-at-a-time warning in
`include/proj/io.hpp`. Only three mutexes exist in the whole library. The
problem is not that locks are missing in places — it is that *reads mutate*:
`proj_as_wkt()` writes a cache inside the object it is asked to describe, and
returns a pointer into it. No amount of caller-side locking fixes that, because
the returned pointer is invalidated by the next call from any thread.

**GDAL is structurally lockable but the contract leaks.** GDAL 3.10 added an
opt-in per-object recursive mutex (`AssignAndSetThreadSafe()`), and 3.9 added a
value-returning `std::string exportToWkt()`. The direction is right, but the
mode is incomplete: the lock covers only the methods defined in one of 23 files
that implement `OGRSpatialReference`, one mutating path bypasses it entirely,
and several accessors hand out raw pointers into internal storage after
releasing the lock. And because the safe mode is a mutex, it does not scale:
measured below at 12× worse throughput than private copies at 8 threads.

So the useful ask upstream is not "add locks". It is:

* **PROJ** — make read operations on CRS description objects genuinely `const`,
  and add caller-owned-result variants of the three formatting entry points.
  Then a CRS object becomes shareable, and PROJ can *document* it as such.
* **GDAL** — add a way to freeze an `OGRSpatialReference` so all its lazy state
  is materialised once, after which const accessors need no lock; and add
  value-returning accessors for the ones that currently leak interior pointers.

Both are purely additive. No existing signature has to change.


## 2. What was reproduced

Two defects reproduce in seconds and are worth reporting upstream as-is.

### 2.1 Concurrent `proj_as_wkt()` on a shared `PJ` corrupts the heap

`thread-safety/proj_race.cpp` test 1. Four threads, **each with its own
`PJ_CONTEXT`** as PROJ requires, calling nothing but `proj_as_wkt()` on one
shared `PJ*` obtained from `proj_create(ctx, "EPSG:3067")`:

```
$ ./proj_race 1 4 4
PROJ 9.7.1
shared PJ EPSG:3067, expected WKT is 1128 bytes
double free or corruption (top)
Aborted (core dumped)

#8  __GI___libc_free (mem=<optimized out>) at malloc.c:3398
#9  proj_as_wkt () from /usr/proj97/lib64/libproj.so.25
```

Cause, `src/proj_internal.h:671-679`:

```cpp
    // cached results
    mutable std::string lastWKT{};
    mutable std::string lastPROJString{};
    mutable std::string lastJSONString{};
    mutable bool gridsNeededAsked = false;
    mutable std::vector<NS_PROJ::operation::GridDescription> gridsNeeded{};
    mutable PJ_TYPE type = PJ_TYPE_UNKNOWN;
```

and `src/iso19111/c_api.cpp:1708-1709`:

```cpp
        obj->lastWKT = iWKTExportable->exportToWKT(formatter.get());
        return obj->lastWKT.c_str();
```

Two threads assigning the same `std::string` is an unsynchronised write, and
each returns a pointer into the buffer the other is about to reallocate.
`proj_as_proj_string()` (`:1803`) and `proj_as_projjson()` (`:1873`) have the
identical shape.

GDAL already knows. `ogr/ogrspatialreference.cpp:1755-1758`:

```cpp
    // In the past calling this method was thread-safe, even if we never
    // guaranteed it. Now proj_as_wkt() will cache the result internally,
    // so this is no longer thread-safe.
    std::lock_guard oLock(d->m_mutex);
```

That is one of exactly two places in GDAL that lock **unconditionally** rather
than optionally — the other is `exportToProj4()` at `:11755-11761`, for the same
reason. GDAL is paying a permanent serialisation cost to work around a PROJ
implementation detail.

### 2.2 `proj_create(nullptr, ...)` from several threads corrupts PROJ's caches

`thread-safety/proj_race.cpp` test 2. Four threads calling
`proj_create(nullptr, "EPSG:nnnn")` over a spread of twelve real EPSG codes.
Passing a null context is legal and is the convenient default; it resolves to
`pj_get_default_ctx()`.

First, silently wrong answers — note these are nonsense, EPSG:6326 is the WGS 84
ensemble and has neither an engineering datum nor mismatched ellipsoids:

```
proj_create: cannot build projectedCRS EPSG:3857: cannot build geodeticCRS EPSG:4326:
             cannot build geodetic reference frame EPSG:6326:
             ensemble should have datums with identical ellipsoid
proj_create: cannot build projectedCRS EPSG:32635: cannot build geodeticCRS EPSG:4326:
             cannot build geodetic reference frame EPSG:6326:
             engineering datum not found
```

Then all four threads abort in the same place:

```
#4  std::__glibcxx_assert_fail(...)
#5  std::__detail::_List_node_base::_M_transfer(...)
#6  osgeo::proj::io::AuthorityFactory::createUnitOfMeasure(...) const
#7  osgeo::proj::io::AuthorityFactory::Private::createUnitOfMeasure(...)
#8  osgeo::proj::io::AuthorityFactory::createConversion(...) const
#9  osgeo::proj::io::AuthorityFactory::Private::createProjectedCRSEnd(...)
#10 osgeo::proj::io::AuthorityFactory::createProjectedCRS(...) const
```

`_M_transfer` is the intrusive-list splice an LRU cache performs to mark an
entry most-recently-used. The list is `DatabaseContext::Private::cacheUOM_`,
one of ten caches declared at `src/iso19111/factory.cpp:858-877`:

```cpp
    using LRUCacheOfObjects = lru11::Cache<std::string, util::BaseObjectPtr>;
    static constexpr size_t CACHE_SIZE = 128;
    LRUCacheOfObjects cacheUOM_{CACHE_SIZE};
    LRUCacheOfObjects cacheCRS_{CACHE_SIZE};
    ...
```

`lru11::Cache`'s third template parameter defaults to `NullLock`
(`include/proj/internal/lru_cache.hpp:46-56`), i.e. no synchronisation. The
chain that makes them shared is:

```
pj_get_default_ctx()            src/ctx.cpp:188-194   process-wide singleton
  -> pj_ctx::get_cpp_context()  src/ctx.cpp:120-125   lazy, unlocked
    -> projCppContext           include/proj/internal/io_internal.hpp:189
      -> DatabaseContext        one sqlite3 handle, one prepared-statement map
                                (factory.cpp:847), ten unlocked LRU caches
```

`pj_get_default_ctx()` returns a function-local `static pj_ctx`. C++11
guarantees its *initialisation* is thread-safe; nothing guarantees its *use* is.
Everything in `pj_ctx` (`src/proj_internal.h:799-861`) is plain mutable state:
`last_errno`, `lastFullErrorMessage`, `lookupedFiles`, `search_paths`,
`cpp_context`, two recursion counters.

The libstdc++ assertion is what turned this into a clean abort. Without
`_GLIBCXX_ASSERTIONS` it is silent list corruption.

**This one does not affect SmartMet.** Nothing in `~/hub` calls the PROJ C API
directly — a grep for `proj_create|proj_as_wkt|proj_context_create|proj_trans|
proj_destroy` across the whole workspace hits only comments in
`gis/gis/OGRSpatialReferenceFactory.cpp`. All PROJ use goes through GDAL, and
GDAL always supplies a per-thread context
(`ogr/ogr_proj_p.cpp:233`, `thread_local OSRPJContextHolder`). It matters
upstream because it is the *documented easy path* for every other PROJ consumer.

### 2.3 What did **not** reproduce

`thread-safety/srs_race.cpp` and `srs_share.cpp` share one
`OGRSpatialReference` between six threads doing getter mixes and
`OGRCreateCoordinateTransformation()` + `Transform()`. In 5-second runs on
GDAL 3.12.1 neither produced a crash or a wrong value, in either plain or
`AssignAndSetThreadSafe` mode (148M / 0.6M iterations for `srs_race`, 70M / 51M
for `srs_share`).

That is worth stating plainly: GDAL's two unconditional locks cover the hot
export paths, and the remaining accessors mostly read fields that are already
populated. The defects in §3 are established by inspection, not by a crash.
They are still real — they are undefined behaviour by contract, and §4.1 is a
live one — but they are latent, and one GDAL or PROJ upgrade away from
surfacing, exactly as `exportToWkt()` stopped being accidentally safe.


## 3. GDAL: `OGRSpatialReference` and its factories

### 3.1 The opt-in thread-safe mode covers one file out of 23

`ogr/ogrspatialreference.cpp:64-196` defines `OGRSpatialReference::Private` with
`bool m_bIsThreadSafe` (`:95`), `std::recursive_mutex m_mutex` (`:122`), an
`OptionalLockGuard` (`:175-190`) and the macro that uses it (`:203`):

```cpp
#define TAKE_OPTIONAL_LOCK()                                                   \
    auto lock = d->GetOptionalLockGuard();                                     \
    CPL_IGNORE_RET_VAL(lock)
```

The macro is file-local, and so is the type it needs. `ogr_spatialref.h:155-156`
only forward-declares the implementation class:

```cpp
    struct Private;
    std::unique_ptr<Private> d;
```

`Private` is *defined* in `ogrspatialreference.cpp:64-196`. So
`d->GetOptionalLockGuard()` requires a complete type that no other translation
unit has. Counting lock sites per file that defines `OGRSpatialReference::`
methods:

```
140  ogr/ogrspatialreference.cpp
  0  ogr/ogr_srs_esri.cpp        importFromESRI, ImportFromESRIStatePlaneWKT
  0  ogr/ogr_srs_xml.cpp         importFromXML, exportToXML
  0  ogr/ogr_srs_pci.cpp         importFromPCI, exportToPCI
  0  ogr/ogr_srs_usgs.cpp        importFromUSGS, exportToUSGS
  0  ogr/ogr_srs_erm.cpp         importFromERM, exportToERM
  0  ogr/ogr_srs_panorama.cpp    importFromPanorama, exportToPanorama, ...
  0  ogr/ogr_srs_ozi.cpp         importFromOzi
  0  ogr/ogr_srs_isis.cpp        importFromISISPVL
  0  ogr/ogr_srs_dict.cpp        importFromDict, lookupInDict
  0  ogr/ogr_fromepsg.cpp        AutoIdentifyEPSG, FindBestMatch, SetStatePlane
  0  ogr/ogrct.cpp, ogrgeometry.cpp, ogrgeometryfactory.cpp, ... (10 more)
```

This is not a case of someone forgetting the macro — those files *cannot* take
the lock, and for the same reason none of them touches `d` at all (verified: 0
`d->` references across all ten `ogr_srs_*.cpp`). They compose **public** methods
instead: `GetAttrValue()`, `GetLinearUnits()`, `GetNormProjParm()`, `IsLocal()`,
`GetSemiMajor()`. Every one of those *does* take the optional lock individually.

So the consequence is not a low-level data race inside any single call. It is
that **the lock is per call, not per operation**, and the recursive mutex was
chosen precisely to avoid that. `ogrspatialreference.cpp:171-174`:

> The lock is not just for a single call to `OGRSpatialReference::Private`, but
> for the series of calls done by a `OGRSpatialReference` method. We need a
> recursive mutex, because some `OGRSpatialReference` methods may call other
> ones.

Methods in `ogrspatialreference.cpp` get that envelope: the outer method locks
once and every nested call re-enters the same recursive mutex. Methods in the
other 22 files get no envelope — they are a *sequence* of independently locked
operations with gaps in between. Two consequences follow.

**(a) Non-atomic composition.** A concurrent mutation lands in one of the gaps,
so the result mixes pre- and post-mutation state. No single call misbehaves; the
composite answer is simply not a snapshot of any one state of the object.

**(b) Interior pointers held across the gaps.** This is the serious one, and
`exportToPCI()` is the clearest example. `ogr/ogr_srs_pci.cpp:744`:

```cpp
    const char *pszProjection = GetAttrValue("PROJECTION");
```

That is a pointer into `d->m_poRoot`, GDAL's lazily built `OGR_SRSNode` tree
(§3.3). The lock taken inside `GetAttrValue()` is released before the pointer is
used. It is then dereferenced repeatedly down a ~50-branch `else if` chain, with
dozens of separately locked calls interleaved, and last read at **line 1031** —
287 lines later. `pszDatum`, taken at line 1013, is last read at line 1159.

If any thread invalidates the node tree during those spans, both pointers
dangle. The invalidation path exists and is §3.2's unlocked
`GetAttrNode("...CONVERSION...")`, which does `delete m_poRoot`. Note the
interaction: fixing §3.2 by adding the missing guard makes the *delete* orderly
with respect to other locked calls, but does nothing for `pszProjection` —
`exportToPCI` holds no lock at any point, so a correctly locked delete in another
thread still frees the tree under it. The two defects have to be fixed together.

In fairness: `exportToPCI` itself is read-only, so a workload where every thread
only reads a shared thread-safe SRS is fine today. The exposure is readers
composed against any writer, or against the unlocked path in §3.2.

**Fix:** move `Private`, `OptionalLockGuard` and `TAKE_OPTIONAL_LOCK` into an
internal header (`ogr/ogrspatialreference_private.h`) and take the guard once at
the top of each method in the other files. Mechanical, no API change, and it
gives those methods the same whole-operation envelope the recursive mutex was
designed to provide. It does not fix the interior pointers — that needs §3.3 or,
better, §3.5.

### 3.2 One mutating path bypasses the lock

`ogr/ogrspatialreference.cpp:1247-1254`, the non-const `GetAttrNode`:

```cpp
OGR_SRSNode *OGRSpatialReference::GetAttrNode(const char *pszNodePath)
{
    if (strstr(pszNodePath, "CONVERSION") && !d->m_bNodesWKT2)
    {
        d->invalidateNodes();
        d->refreshRootFromProjObj(/* bForceWKT2 = */ true);
    }
```

No `TAKE_OPTIONAL_LOCK()`. `invalidateNodes()` (`:567-572`) does
`delete m_poRoot; m_poRoot = nullptr;`. The const overload (`:1304-1311`)
reaches it through a `const_cast`, so `GetAttrValue("...CONVERSION...")` — a
const call — deletes and rebuilds the node tree outside the mutex, while
another thread may be walking it.

**Fix:** add the guard. One line.

### 3.3 Const accessors return unowned interior pointers

This is the structural one, and the reason locking alone can never make the
class safe.

`GetRoot() const` (`:1190-1199`) takes the optional lock, lazily builds
`d->m_poRoot`, and returns the raw pointer — after the guard has gone out of
scope. The caller then walks a tree that any other thread may replace.

`GetAttrValue() const` (`:1333-1359`) returns
`poNode->GetChild(iAttr)->GetValue()`, a `const char*` interior to that tree.

`GetAngularUnits(const char**) const` (`:2737-2745`) returns
`d->m_osAngularUnits.c_str()`; `GetTargetLinearUnits` does the same for
`m_osLinearUnits`. GDAL's own doc comment concedes the lifetime problem:

> The returned value remains internal to the OGRSpatialReference and should not
> be freed, or modified. It may be invalidated on the next OGRSpatialReference
> call.

"The next call" is single-threaded phrasing. With sharing there is no "next".

**Fix — additive value-returning accessors.** GDAL already set this precedent
in 3.9 with `std::string exportToWkt(const char *const *papszOptions) const`
(`ogr/ogr_spatialref.h:208`). Extend it:

```cpp
// all safe to call concurrently on a frozen (see 3.5) or thread-safe SRS
std::string           GetAttrValueAsString(const char *pszName, int iAttr = 0) const;
std::string           GetAngularUnitsName() const;
std::string           GetLinearUnitsName() const;
std::string           exportToPROJString(const char *const *options = nullptr) const;
std::optional<int>    GetEPSGCode() const;   // see 4.1
```

`GetEPSGCode()` deserves special mention: every consumer that wants the EPSG
code today has to walk the node tree by hand (SmartMet does — §4.1). A
value-returning accessor removes the only reason most callers ever touch
`GetRoot()`.

### 3.4 `Clone()` silently drops thread-safety

`ogr/ogrspatialreference.cpp:1516-1533`:

```cpp
OGRSpatialReference *OGRSpatialReference::Clone() const
{
    OGRSpatialReference *poNewRef = new OGRSpatialReference();   // not thread-safe
    TAKE_OPTIONAL_LOCK();
    ...
```

Cloning a thread-safe SRS yields a non-thread-safe SRS. Also, the only way to
enter the mode is `AssignAndSetThreadSafe()` (`:1045`); there is no
`SetThreadSafe()`, no `IsThreadSafe()`, and no C API entry point at all —
`ogr/ogr_spatialref.h` mentions the word "thread" exactly once, on line 182.

**Fix:** propagate the flag in `Clone()`; add `SetThreadSafe()`,
`IsThreadSafe() const`, and `OSRSetThreadSafe(OGRSpatialReferenceH)`.

### 3.5 A mutex is the wrong mechanism: it does not scale

`thread-safety/srs_scale.cpp`, `exportToWkt()` per second, 24-core host:

| threads | private `Clone()` per thread | one shared object |
|---:|---:|---:|
| 1 | 124 057 | 95 398 |
| 2 | 277 212 | 118 247 |
| 4 | 612 170 | 154 233 |
| 8 | 1 244 459 | 105 800 |

Private copies scale linearly. The shared object saturates around 4 threads and
then *regresses* — classic mutex convoying on the recursive mutex. At 8 threads
it is 11.8× slower, and slower in absolute terms than a single thread using
private copies.

The reason a lock is needed at all is that the object is lazily populated:
`refreshProjObj()` (`:354-393`), `refreshRootFromProjObj()` (`:395-432`),
`refreshAxisMapping()` (`:460`) and the `m_os*Units` / `dfToMeter` /
`bNormInfoSet` fields are all filled on first use. Nothing after construction
is conceptually mutable.

**Fix — freeze instead of lock.** Add:

```cpp
/** Materialise all lazily-computed state, then forbid modification.
 *  After this call every const method is safe to call concurrently from any
 *  thread without locking, and every mutator returns OGRERR_FAILURE.
 *  @since GDAL 3.15 */
OGRErr OGRSpatialReference::Freeze();
bool   OGRSpatialReference::IsFrozen() const;
```

`Freeze()` would call `refreshProjObj()`, `refreshRootFromProjObj(false)`,
`refreshAxisMapping()`, populate the units/prime-meridian/eccentricity fields,
and set a flag that makes the const paths skip their lazy branches entirely.
Combined with §3.3, a frozen SRS is an immutable value: shareable, lock-free,
and it scales like the "private clone" column above with none of the memory
cost.

This is the single most valuable addition for a server, and it is strictly
additive.

### 3.6 Context rebinding on every operation

Every access re-binds the PROJ object to the calling thread's context.
`Private::getPROJContext()` (`:158-161`) returns `OSRGetProjTLSContext()`, and
`clear()` (`:277`) / `setPjCRS()` (`:336`) / `~Private()` (`:252-268`) call
`proj_assign_context()` with it. The destructor carries the comment:

> In case we destroy the object not in the thread that created it, we need to
> reassign the PROJ context. Having the context bundled inside `PJ*` deeply
> sucks...

That is a fair summary of §5.1. It is a PROJ design consequence GDAL cannot fix
locally; it is listed here because it is why a shared SRS can never be purely
read-only at the PROJ level, and therefore why §3.5's `Freeze()` must
materialise everything up front rather than lock.

### 3.7 Per-thread PROJ contexts duplicate the database layer

`ogr/ogr_proj_p.cpp:233` gives each thread a `PJ_CONTEXT`, hence its own
`DatabaseContext`, its own prepared-statement map, and its own ten 128-entry
LRU caches. `OSRProjTLSCache` (`:317-369`) adds a per-thread WKT→`PJ` and
EPSG→`PJ` cache on top, `proj_clone()`ing on every hit.

Measured incremental cost after warming eight CRSes per context
(`thread-safety/ctx_cost.cpp`): 16.0 MB at 1 thread, 25.0 MB at 24 — about
390 kB per thread. Modest. The duplicated *work* matters more: N threads each
parse the same CRS from `proj.db` independently. The underlying `sqlite3`
handle is shared and `SQLITE_OPEN_FULLMUTEX`
(`src/iso19111/factory.cpp:369-372`), so those parses also serialise against
each other.

**Fix (lower priority):** an opt-in process-wide, internally-synchronised cache
of *frozen* CRSes shared by all threads, e.g. `OSRSetSharedCRSCacheSize(size_t)`.
Only becomes worthwhile once §3.5 exists.

### 3.8 Documentation

`doc/source/user/multithreading.rst` mentions `OSR` exactly once, in a list of
cleanup functions. It says nothing about `OGRSpatialReference` or
`OGRCoordinateTransformation`. The thread-safety mode added in 3.10 is not
documented there at all.

**Fix:** a section stating, per type: `OGRSpatialReference` — thread-affine
unless `AssignAndSetThreadSafe()`; `OGRCoordinateTransformation` — always
thread-affine, use `Clone()` per thread; the `OSRSetPROJ*()` globals — safe,
internally locked.

### 3.9 Two minor items

* `ogr/ogr_proj_p.cpp:63` — `static bool g_bForkOccurred`, written from a
  `pthread_atfork` handler (`:65-68`) and read at `:244` from every thread with
  no atomic. Should be `std::atomic<bool>`.
* `ogr/ogr_proj_p.cpp:511-518` — `OSRGetPROJEnableNetwork()` takes a
  `std::lock_guard`, then manually `unlock()`s and `lock()`s the same mutex
  inside the guarded scope. Balanced today, but fragile; use a
  `std::unique_lock` instead.


## 4. What SmartMet should change now

### 4.1 `SpatialReference::getEPSG()` lazily builds GDAL's node tree on a shared object

`gis/gis/SpatialReference.cpp:342-372`:

```cpp
std::optional<int> SpatialReference::getEPSG() const
{
  const auto &crs = *impl->m_data->crs;
  const auto *root = crs.GetRoot();          // <-- lazy build, unsynchronised
  std::string prefix = root->GetValue();
  ...
```

`impl->m_data->crs` is the `std::shared_ptr<OGRSpatialReference>` handed out by
`OGRSpatialReferenceFactory::Create()`, i.e. a *globally cached, shared* object.
`Impl::init()` (`:124-161`) materialises WKT, PROJ string, `IsGeographic`, the
axis-mapping flags and the hash — but never the node tree, because
`OGR::exportToWkt` goes through `proj_as_wkt()` and does not touch
`m_poRoot`. So the first `getEPSG()` call on a given CRS builds the tree, from
whatever request thread gets there first, under no lock (`GetRoot()` takes only
the *optional* lock, which is off for these objects).

Two threads racing that first call both see `m_poRoot == nullptr`, both build a
tree, and both assign `d->m_poRoot`: an unsynchronised pointer write read by the
other thread, plus a leaked tree. It is also the one place in `gis` that holds a
raw interior pointer (`root`, `node`, `value`) across several statements.

`getEPSG()` is live in production — `brainstorm/engines/contour/contour/Engine.cpp:418`
calls it per request.

**Fix:** compute the EPSG code in `Impl::init()`, while the object is still
thread-private and behind the factory's parse mutex, and store it in `ImplData`
next to `wkt`, `hashvalue` and `is_axis_swapped`. `getEPSG()` then returns a
cached `std::optional<int>` and touches GDAL not at all. This matches what
`init()` already does for every other derived property, and removes the last
lazy GDAL path in the shared-object hot code.

### 4.2 The abstraction leaks the shared raw pointer

`SpatialReference` exposes `get()`, `operator*`, `operator OGRSpatialReference&`
and `operator OGRSpatialReference*` (`gis/gis/SpatialReference.cpp:258-304`).
Every caller in every other repo can therefore invoke arbitrary GDAL methods on
the globally shared object, and the analysis above cannot bound what they do.
`OGRCoordinateTransformationFactory::Create()`
(`gis/gis/OGRCoordinateTransformationFactory.cpp:143`) does exactly this:

```cpp
    auto *ptr = OGRCreateCoordinateTransformation(src.get(), tgt.get());
```

GDAL clones both inputs internally (`ogr/ogrct.cpp:1542-1547`), so this reads
the shared objects concurrently from every worker thread. It did not misbehave
in testing (§2.3) and `Clone()` does take the optional lock — but it is out of
contract.

**Options, in order of preference:**

1. Once GDAL grows `Freeze()` (§3.5), call it in
   `OGRSpatialReferenceFactory::make_crs()` right after
   `SetAxisMappingStrategy()`. That is the clean end state and costs nothing at
   runtime.
2. Until then, call `AssignAndSetThreadSafe()` on the cached object when
   building against GDAL ≥ 3.10. This is correct for the methods in §3.1 and
   costs the serialisation measured in §3.5 — acceptable here precisely because
   `gis` already caches WKT, PROJ string and the flags, so the hot paths do not
   re-export. Note it does *not* protect §3.2 or the `ogr_srs_*.cpp` methods.
3. Independently of both: audit the callers of `SpatialReference::get()` and
   the implicit conversions across `~/hub` and narrow the interface to the
   handful of GDAL calls actually needed.

The existing mitigations in `gis` are sound and should stay: the parse mutex in
`make_crs()` (`OGRSpatialReferenceFactory.cpp:125-132`), the check-out /
check-in pool for transformations (which correctly gives each thread exclusive
ownership of an `OGRCoordinateTransformation` — mandatory, see §5.4), and the
`StaticCleanup` teardown ordering.

### 4.3 Keep the "no direct PROJ C API" property

Nothing in `~/hub` calls PROJ directly today, which is why §2.2 does not bite.
Worth keeping deliberately: if PROJ ever has to be called directly, the rules
are one `PJ_CONTEXT` per thread, never a null context, and never a shared `PJ*`.


### 4.4 Implemented: give every caller its own spatial reference

Option 3 of §4.2 was taken further than "narrow the interface": as of this change
`OGRSpatialReferenceFactory` has no shared object left to narrow access to.
`Create()` returns a **fresh clone that the caller owns outright** and may read,
mutate, hand to a geometry, or keep for as long as it likes. Nothing is shared,
nothing is handed back, and nothing has to be trusted not to modify what it
borrowed. The signature is unchanged, so no caller in any repo needed editing.

Cloning is what makes this affordable, but `Clone()` *reads* (and lazily rebuilds)
the object it copies, so the source has to be private to the cloning thread too.
Hence two levels:

* a **master** object per definition string, parsed once and never handed out.
  Cloned only under `OGRSpatialReferenceFactory::mutex()`, and only to seed a
  thread's sample.
* a **thread-local sample** per definition string, cloned once from the master.
  Every later `Create()` clones that, with no lock at all, because no other
  thread can reach it. Bounded by a small per-thread LRU
  (`SetSampleStoreSize()`, default 64); overflowing it costs one extra
  clone-under-lock, never an error.

Why per-thread rather than one shared sample: cloning a single shared sample
needs a lock on every call, and that measurably *regresses* with threads.

Measured on a 24-core host, GDAL 3.12.1 / PROJ 9.7.1
(`test/SpatialReferenceCloneBench`), acquisitions/s:

| design | 1 | 2 | 4 | 8 | 16 |
|---:|---:|---:|---:|---:|---:|
| shared cached object (before) | see below | | | | |
| check-out pool (intermediate) | 2 372 881 | 782 568 | 1 241 595 | 919 029 | 130 272 |
| Clone from one shared sample + lock | 220 180 | 177 944 | 166 648 | 135 981 | 108 949 |
| **Clone from thread-local sample (implemented)** | 205 712 | 385 779 | 1 192 712 | **1 972 234** | **1 198 640** |

The implemented path is within ~1% of a hand-written thread-local clone at eight
threads, 2.1x the check-out pool there, and 9.2x the pool at sixteen threads,
where the pool's single mutex convoys. The pool is ~11x faster on *one* thread
(0.42 us versus 4.8 us per acquisition) and that is the trade accepted: 4.8 us is
nothing next to the work any real request does, and it buys away all of the
pool's machinery.

The far bigger cost sits elsewhere, and it is why `Fmi::SpatialReference` and not
`OGRSpatialReference` is the thing worth copying:

| operation | rate | per call |
|---|---:|---:|
| `Fmi::SpatialReference(definition string)` - derived values cached | 5 039 480/s | 0.2 us |
| `Fmi::SpatialReference(const OGRSpatialReference&)` - re-derives everything | **581/s** | **1.7 ms** |

The second re-runs `exportToWkt`, `exportToProj`, a `ProjInfo` parse and the
`GetRoot()` walk: roughly 4000x a clone. So `ImplData` holds only immutable
derived values (WKT, PROJ string, EPSG code, axis flags) and is shared by copies,
while each instance clones its own `OGRSpatialReference` lazily - and only if
someone actually calls `get()`/`operator*`. Copying a `Fmi::SpatialReference` is
therefore free, its accessors never touch GDAL, and the 1.7 ms path is reached
only by callers that hand in a raw `OGRSpatialReference`. Those are worth
converting: `newbase/NFmiGdalArea.cpp:217-219` is one, and it runs per area.

Consequences elsewhere in `gis`:

* `SpatialReference::Impl::init()` derives from a private clone, so it no longer
  needs the factory mutex - including `get_epsg()`, whose `GetRoot()` builds a
  node tree inside the object (§4.1).
* `OGRCoordinateTransformationFactory::Create()` no longer locks around
  `OGRCreateCoordinateTransformation()`: both inputs are private clones.

#### A destruction-order trap worth knowing about

Handing out clones means a thread keeps GDAL objects in thread-local storage
until it exits, and that leaks one `PJ_CONTEXT` per thread unless the ordering is
forced. `thread_local` objects are destroyed in reverse order of the completion
of their construction; GDAL keeps each thread's context in a `thread_local` of
its own; and `~OGRSpatialReference` reassigns a context to the object before
destroying it (§3.6). If GDAL's goes first, every sample destroyed afterwards
makes GDAL create a replacement context that nothing ever frees.

Measured with a 48-thread probe: 48 leaked contexts, one per thread. It was
invisible before this change only because a warm `Create()` used to be a pure
cache lookup that never touched PROJ at all - the worker threads never acquired a
context to leak.

The fix is to touch GDAL once in the sample store's constructor, which puts
GDAL's `thread_local` *ahead* of ours in construction order and therefore behind
it in destruction order. Costs one `proj.db` lookup per thread and shows up
nowhere in the throughput table above. `ReleaseThreadSamples()` is also exposed
for applications that have an explicit worker-thread teardown hook, but is no
longer required.

#### One store, and which door to use

`OGRSpatialReferenceFactory` and `SpatialReference` used to keep two caches keyed
by the same definition string - a parsed object in one (limit 1000), the values
derived from it in the other (limit 10000) - evicting independently and reported
as two separate statistics lines. One could evict while the other retained, so a
`SpatialReference` that still had its derived values could be forced to re-parse
the CRS just to produce an object.

They are now a single store, `CrsRegistry` (`gis/CrsRegistry.h`, implemented in
the factory translation unit, which is where the parsing already lived). One
entry per definition string holds the master object and, behind a
`std::once_flag`, the derived values - so a caller that only wants an object
never pays for the derivation, which is the expensive half. `is_axis_swapped()`
and `get_epsg()` moved there with it, and `SpatialReference.cpp` is now a facade.
Both `getCacheStats()` functions report that one store.

With that in place the two entry points have distinct, documented jobs:

* **`Fmi::SpatialReference(definition string)`** - the normal door. Derived values
  are shared, copying is free, accessors never enter GDAL.
* **`OGRSpatialReferenceFactory::Create()`** - the raw-object door, for code that
  must hand a mutable `OGRSpatialReference` to GDAL itself
  (`OGRCreateCoordinateTransformation`, `assignSpatialReference`, a dataset).
  Narrowed to that role the name is accurate, so it keeps it.

The cost of using the wrong door is not small. `newbase/NFmiGdalArea.cpp` held its
datum as a `std::shared_ptr<OGRSpatialReference>` and passed `*datum` into
`CoordinateTransformation`, which takes `const SpatialReference&`: that implicit
conversion re-derived everything, once per direction, twice per area
construction. Holding a `Fmi::SpatialReference` instead:

| NFmiGdalArea("FMI", "EPSG:2393", ...) | per construction | rate |
|---|---:|---:|
| datum as raw `OGRSpatialReference` | 1.754 ms | 570/s |
| datum as `Fmi::SpatialReference` | **0.015 ms** | **65 901/s** |

117x, measured over 300 constructions against the installed gis, so the gain is
independent of the factory rework above. Areas are constructed per querydata file
and on every `Clone()`. The comment at that call site had chosen the factory
specifically to avoid a per-construction `proj.db` parse - it avoided the parse
and then paid more for the derivation.

#### Verification

`test/SpatialReferenceOwnershipTest.cpp`, 15 tests. Against the pre-change
implementation **10 of them fail, and the process then dies with
`double free or corruption (out)` and SIGSEGV** in
`ConcurrentMutatorsDoNotDisturbEachOther` - eight threads each modifying the CRS
the factory handed them. That is worth recording plainly: §2.3 concluded the
shared-object defects were latent because concurrent *reads* did not misbehave,
and that still holds, but concurrent *mutation* of the shared object corrupts the
heap outright, and the old API invited exactly that by returning a mutable
pointer to a process-wide object.

Sanitisers, with `libsmartmet-gis.so` itself built `ASAN=yes` / `TSAN=yes` so the
factory code is instrumented and not just the test translation unit:

| build | result |
|---|---|
| ASan + UBSan, full suite (90 tests) | 0 errors, 0 `runtime error:`, 0 leaks |
| TSan, ownership tests + 8-thread stress | 0 data races |

Neither TSan nor Helgrind reports anything on the **pre-change** code either -
GDAL and PROJ are linked uninstrumented from `/usr/gdal312` and `/usr/proj97`,
and a sanitiser cannot see accesses inside them. Do not expect a sanitiser to
demonstrate this class of defect; the crash above and the deterministic ownership
assertions are the evidence.

## 5. PROJ: the deeper problems

Listed roughly in order of how much they block sharing.

### 5.1 A `PJ` is bound to a context, and rebinding mutates it

`src/ctx.cpp:68-78`:

```cpp
void proj_assign_context(PJ *pj, PJ_CONTEXT *ctx) {
    if (pj == nullptr) return;
    pj->ctx = ctx;
    if (pj->reassign_context) pj->reassign_context(pj, ctx);
    for (const auto &alt : pj->alternativeCoordinateOperations)
        proj_assign_context(alt.pj, ctx);
}
```

Because a `PJ` carries its context, and errors/logging/database access all go
through that context, using one `PJ` from two threads requires rebinding it back
and forth — a write, so the sharing is unsound before any cache is involved.
This is the root cause of GDAL's `proj_assign_context()` calls in §3.6 and of
the "deeply sucks" comment.

**Fix:** for the subset of `PJ` that are pure *descriptions* — `proj_is_crs()`
true, i.e. a wrapper over an immutable `iso_obj` — the context should be needed
only as a *parameter* to the calls that use it, never as object state. Every
relevant C entry point already takes `PJ_CONTEXT *ctx` explicitly. Once §5.2
removes the caches, `pj->ctx` is no longer read on those paths and CRS objects
become genuinely shareable. That is the enabling change for everything else.

### 5.2 Formatting entry points mutate the object and return interior pointers

Reproduced in §2.1. `proj_as_wkt()`, `proj_as_proj_string()`,
`proj_as_projjson()` each write a `mutable std::string` member and return
`.c_str()`.

**Fix — additive, caller owns the result:**

```c
/* Returns a newly allocated string, or NULL on error.
 * Free with proj_string_destroy(). Safe to call concurrently on the same
 * object from multiple threads and contexts. */
char *proj_as_wkt_alloc(PJ_CONTEXT *ctx, const PJ *obj, PJ_WKT_TYPE type,
                        const char *const *options);
char *proj_as_proj_string_alloc(PJ_CONTEXT *ctx, const PJ *obj,
                                PJ_PROJ_STRING_TYPE type,
                                const char *const *options);
char *proj_as_projjson_alloc(PJ_CONTEXT *ctx, const PJ *obj,
                             const char *const *options);
void  proj_string_destroy(char *str);
```

Note the `const PJ *` — currently impossible. The three existing functions stay,
implemented on top, and get a documented "not safe on a shared object" note.
Once no caller uses them, `lastWKT` / `lastPROJString` / `lastJSONString` can go.

The same pattern applies to the other two mutable caches:

* `mutable PJ_TYPE type` (`src/proj_internal.h:679`, written at
  `src/iso19111/c_api.cpp:1342`) — a `PJ`'s type never changes, so compute it in
  `pj_obj_create()` and drop the `mutable`.
* `gridsNeededAsked` / `gridsNeeded` (`:675-676`, written at
  `src/iso19111/c_api.cpp:7922-7923`) — same treatment, or an `_alloc` variant.

The `projCppContext` return buffers have the same defect one level up, in the
*context* rather than the object: `lastDbPath_`, `lastDbMetadataItem_`,
`lastUOMName_`, `lastGridFullName_`, `lastGridPackageName_`, `lastGridUrl_`
(`include/proj/internal/io_internal.hpp:200-205`, used at
`src/iso19111/c_api.cpp:408-409, 460-461, 922-923, 975-988`). With a
per-thread context these are merely surprising; with a shared one they race.

### 5.3 The default context is a process-wide singleton with no synchronisation

Reproduced in §2.2. `src/ctx.cpp:188-194`:

```cpp
PJ_CONTEXT *pj_get_default_ctx()
{
    // C++11 rules guarantee a thread-safe instantiation.
    static pj_ctx default_context(pj_ctx::createDefault());
    return &default_context;
}
```

The comment is about construction only. Reached implicitly whenever `ctx` is
null (`proj_create`, `proj_context_errno`, `pj_get_ctx`), and *explicitly* from
inside the C++ API at `src/iso19111/operation/singleoperation.cpp:531-536`:

```cpp
CoordinateTransformer::~CoordinateTransformer() {
    if (d->pj_) {
        proj_assign_context(d->pj_, pj_get_default_ctx());
        proj_destroy(d->pj_);
    }
}
```

So destroying a `CoordinateTransformer` on any thread touches the shared default
context unconditionally, even for a program that scrupulously created its own
contexts everywhere.

**Fix, two layers:**

*Immediate hardening, one line each.* Give the ten LRU caches in
`DatabaseContext::Private` (`src/iso19111/factory.cpp:858-877`) an explicit
`std::mutex` lock type. `lru11::Cache` already supports it —
`include/proj/internal/lru_cache.hpp:87-89`: *"passing Lock=std::mutex will make
it thread-safe"*. One uncontended mutex per cache operation is negligible next
to a `proj.db` query, and it converts the reproduced heap corruption in §2.2
into a correct result. The same applies to `mapSqlToStatement_`
(`factory.cpp:847`).

*Proper fix.* Make the default context per-thread. `proj_context_create()`
already does `new pj_ctx(*pj_get_default_ctx())` (`src/ctx.cpp:305-307`), so the
machinery exists: keep the current singleton as a configuration *template*, and
have `pj_get_default_ctx()` return a `thread_local` clone of it. Consequences to
work through: `proj_context_set_*` called on the default context must update the
template as well as the caller's copy, and `proj_context_errno()` becomes
per-thread — arguably a fix in itself, since today two threads sharing the
default context overwrite each other's `last_errno` and
`lastFullErrorMessage`.

### 5.4 Five public classes are documented single-thread, and nothing enforces it

`include/proj/io.hpp`:

| Line | Class | Note |
|---|---|---|
| 186 | `WKTFormatter` | *"can only be used by a single thread at a time"* |
| 405 | `PROJStringFormatter` | same |
| 536 | `JSONFormatter` | same |
| 874 | `DatabaseContext` | *"should be used only by one thread at a time"* |
| 1009 | `AuthorityFactory` | same |

`DatabaseContext` is the important one: it owns the `sqlite3` handle, the
prepared-statement map and the ten caches. Sharing it is the §2.2 crash. The
formatters are short-lived per call and are less of a problem in practice.

The whole library contains three mutexes outside the vendored code:
`src/mutex.cpp:33` (`core_lock`, used only by `initcache.cpp` and `info.cpp`),
`src/iso19111/factory.cpp:625` (`SQLiteHandleCache::sMutex_`) and the grid-name
maps in `src/transformations/{gridshift,hgridshift,vgridshift}.cpp`.

Note what `SQLiteHandleCache` (`factory.cpp:620-691`) does and does not
protect. It is a *process-global* cache of `sqlite3*` keyed by path, so two
different contexts get the **same** connection. `sMutex_` protects the lookup
only. Concurrent use of the connection is safe because it is opened
`SQLITE_OPEN_FULLMUTEX` (`:369-372`, with the comment *"as this will be used
from concurrent threads"*) — which also means every thread's `proj.db` queries
serialise on SQLite's connection mutex. That is the contention the existing
comment in `gis/gis/OGRSpatialReferenceFactory.cpp:116-124` describes.

### 5.5 Grid readers hold mutable read buffers

`src/grids.cpp:207, 456-457, 2091, 2853-2854` — `mutable std::vector<float>
m_buffer`, `mutable uint32_t m_bufferBlockId`, `mutable std::string m_type`.
These live in the grid objects owned by a transformation `PJ`, so a shared `PJ`
used for `proj_trans()` from two threads races on the decode buffer, silently
producing wrong coordinates. This is a *transformation* concern rather than a
CRS one, and it is a fair consequence of the documented one-`PJ`-per-thread
rule. It is listed so the rule's cost is explicit: transformation objects can
never be shared, only pooled — which is what
`gis/gis/OGRCoordinateTransformationFactory.cpp` correctly does.

### 5.6 Documentation

One sentence, `docs/source/development/reference/datatypes.rst:54-56`. For a
library this widely embedded that is not enough.

**Fix:** a `docs/source/development/threads.rst` classifying every public type
as (a) immutable and shareable, (b) thread-affine, or (c) internally
synchronised — and saying explicitly that a null `ctx` means the shared default
context and is therefore not safe from multiple threads.


## 6. Suggested upstream sequencing

Ordered so each step is useful on its own and none blocks on the next.

**PROJ**

| # | Change | Effort | Value |
|---|---|---|---|
| P1 | `std::mutex` lock type for the ten `DatabaseContext` LRU caches + `mapSqlToStatement_` | trivial | turns §2.2 heap corruption into correct behaviour |
| P2 | `proj_as_wkt_alloc()` / `proj_as_proj_string_alloc()` / `proj_as_projjson_alloc()` + `proj_string_destroy()` | small | fixes §2.1; lets GDAL drop two unconditional locks |
| P3 | Drop `mutable` from `PJ::type` and `gridsNeeded*` (compute eagerly) | small | completes "reads don't mutate" |
| P4 | Per-thread default context (template + `thread_local` clone) | medium | fixes §5.3 and per-thread errno |
| P5 | `docs/source/development/threads.rst` | small | states the contract |
| P6 | Document CRS-description `PJ` as immutable and shareable once P2/P3 land | small | the actual enabling guarantee |

**GDAL**

| # | Change | Effort | Value |
|---|---|---|---|
| G1 | Add `TAKE_OPTIONAL_LOCK()` to the mutating `GetAttrNode` (§3.2) | trivial | closes a real hole in the existing safe mode |
| G2 | Move the lock machinery to an internal header; use it in the other 22 files (§3.1) | small | makes the safe mode actually safe |
| G3 | `SetThreadSafe()`, `IsThreadSafe()`, `OSRSetThreadSafe()`; propagate through `Clone()` (§3.4) | small | usable API |
| G4 | Value-returning accessors, incl. `GetEPSGCode()` (§3.3) | medium | removes the interior-pointer hazard |
| G5 | **`Freeze()` / `IsFrozen()`** (§3.5) | medium | lock-free shared reads, 12× better scaling |
| G6 | Multithreading docs for OSR (§3.8) | small | |
| G7 | Optional shared frozen-CRS cache (§3.7) | larger | removes N× duplicated parsing |

G5 is the one worth pushing hardest for. It is the smallest change that gives a
server what it actually needs — an immutable, shareable, lock-free CRS — and it
needs nothing from PROJ, because freezing materialises the lazy state while the
object is still thread-private.

**SmartMet**, independent of upstream: §4.1 (`getEPSG()` — a live hazard, fix
now), then §4.2 option 2 as interim hardening and option 1 when `Freeze()`
exists.


## 7. Reproducers

[`thread-safety/`](thread-safety/) — `make run` builds and runs everything.
Override `PROJ_PREFIX` / `GDAL_PREFIX` to test other versions.

| File | Demonstrates |
|---|---|
| `proj_race.cpp 1` | §2.1 shared `PJ` + concurrent `proj_as_wkt()` → heap corruption |
| `proj_race.cpp 2` | §2.2 `proj_create(nullptr,…)` from N threads → wrong results, then abort in the LRU cache |
| `srs_race.cpp` | §2.3 shared `OGRSpatialReference`, getter mix, both GDAL modes |
| `srs_share.cpp` | §2.3 shared SRS passed to `OGRCreateCoordinateTransformation()`, as `gis` does |
| `srs_scale.cpp` | §3.5 read scaling, private clones vs one shared object |
| `ctx_cost.cpp` | §3.7 resident cost per `PJ_CONTEXT` |

`proj_race` is expected to abort. The others exit 0 on the versions tested
here; keep them as regression canaries for future PROJ/GDAL upgrades.
