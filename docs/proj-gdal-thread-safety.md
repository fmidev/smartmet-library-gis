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

Both are single-threaded. The first figure does not survive concurrency, for a
reason that has nothing to do with GDAL; see *The normal door's remaining cost is
the cache lock* below.

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

#### The normal door's remaining cost is the cache lock

Once the store is warm, `Fmi::SpatialReference(definition string)` does almost no
work of its own: it performs one `Fmi::Cache::Cache::find()` on the master store
and copies a `shared_ptr` to the derived values. That lookup is therefore the
whole steady-state cost of the recommended entry point - and until macgyver
26.8.29 it did not scale.

`Cache::find()` held a `boost::upgrade_lock` for the duration of the lookup. A
`shared_mutex` permits only one upgrade owner at a time, so every lookup in a
shard excluded every other lookup in that shard: a plain hit on an entry already
at the MRU end, where the splice is skipped and nothing is mutated, and a miss,
which touches only a relaxed atomic, both serialised exactly like a write.
Striping across shards spreads that cost only to the extent that the keys spread,
and CRS lookups are the opposite of spread - a handful of definition strings
dominate, so they land on a handful of shards and queue there.

Measured with `test/SpatialReferenceCloneBench` on the same 24-core host, two gis
builds from identical sources differing only in `macgyver/Cache.h`, medians of
three alternating rounds. `Fmi::SpatialReference(string)`, acquisitions/s:

| `Cache::find()` | 1 | 2 | 4 | 8 | 16 |
|---|---:|---:|---:|---:|---:|
| `upgrade_lock` (before) | 4 398 838 | 3 020 907 | 1 327 338 | 519 512 | 73 004 |
| **`shared_lock` (after)** | 4 537 283 | 2 460 893 | 1 522 865 | **774 236** | **221 714** |
| ratio | 1.03x | 0.81x | 1.15x | 1.49x | **3.04x** |

At sixteen threads that is 219 us per construction before and 72 us after,
against 0.23 us uncontended. The fix, in `macgyver` on branch `proj-safety`
alongside this one, is to take a plain shared lock and re-lock the shard
exclusively only to promote an entry that is not already at the MRU end, looking
the key up again after re-locking because it may have been evicted or promoted in
between.

Two caveats worth recording. The 19% loss at two threads reproduced in every
round: it is the price of letting readers genuinely overlap, since they then
contend for the hit-counter cache lines instead of taking turns behind the lock.
And the fix reduces the convoy without removing it - the path still degrades
about 20x from one thread to sixteen, because a shared lock is still an atomic
write to one word per shard. Only per-thread state avoids that entirely.

Which is precisely what the raw-object door already does, and it is the reason
the two doors behave so differently under load. `OGRSpatialReferenceFactory::Create()`
consults the cache only to seed a thread's sample; every later call is answered
from thread-local storage without touching the shared store at all, which is why
its row in the throughput table above rises with threads while this one falls.
If `Fmi::SpatialReference(string)` ever becomes hot enough to matter, giving the
derived values the same per-thread treatment is the remedy - though at 222k
constructions/s across sixteen threads it is far from that today.

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

#### The adversarial battery (2026-08-30)

`test/SpatialReferenceConcurrencyTest.cpp`, 29 tests, attacks every concurrency
surface of the design rather than just the contract, and verifies *values*
bit-exactly against single-threaded baselines computed in the same process -
because the historical failure mode here was silently wrong coordinates, not
crashes. Its sections: sustained correctness under contention (derived values,
forward+inverse transforms, whole-geometry transforms); cold-start races (16
threads hitting the first-ever use of a key at a barrier: the master parse, the
`call_once` derivation, sample seeding, transformation construction - repeated
with synthetic never-seen CRS strings); facade semantics (a shared const
instance read from 16 threads, the lazy-clone `call_once` race, copies getting
private objects, the pinned raw-object exception); mutation storms (eight
mutators using four different mutation paths against eight bit-exact
verifiers); the transformation pool (exclusivity tracking, recycling exactness,
one-slot-pool churn); cache pressure (a two-entry master store under a
sixteen-thread storm, one-entry sample stores, `SetCacheSize`/`SetSampleStoreSize`
oscillating live while verifiers run and a thread polls statistics, held values
surviving eviction); lifecycle (waves of 48 short-lived threads, objects and
pooled transformations created in one thread, used in a second and destroyed in
a third, geometries carrying a CRS across threads, `ReleaseThreadSamples()`
while clones are live); and error paths (three kinds of invalid definition
racing valid work, with failure-residue checks). Every multi-thread test starts
its threads on a barrier, thread counts oversubscribe CI cores on purpose, and
`GIS_THREAD_STRESS=<n>` multiplies iteration counts for soak runs.

`test/SpatialReferenceApiStabilityTest.cpp` pins the public API at compile
time: function pointers and static_asserts with the exact pre-rework signatures
of `OGRSpatialReferenceFactory`, `SpatialReference`, `CoordinateTransformation`
and `OGRCoordinateTransformationFactory` (including the implicit conversions
and the `Ptr` handle type), plus runtime smoke calls. The rule it enforces: the
old API is never changed - if a signature must evolve, a new method is added
and the old one is kept and deprecated. The rework itself complied: the header
diff against master is purely additive (`SetSampleStoreSize`,
`getSampleStoreSize`, `ReleaseThreadSamples`) and nothing was removed or
re-typed, so nothing needed a deprecation mark.

Results on the 24-core host, GDAL 3.12.1 / PROJ 9.7.1:

| run | result |
|---|---|
| battery, default stress | 29/29 in ~4 s |
| battery, `GIS_THREAD_STRESS=20` soak | 29/29 in ~48 s |
| battery, 5 repeats with `--gtest_shuffle` | clean |
| full suite (all test binaries incl. battery + API pins) | green |
| TSan (library instrumented), ownership + battery ×3 at stress 5 | 0 races in gis code |
| ASan + UBSan (library instrumented), ownership + battery + stress-5 battery | 0 errors, 0 leaks, 0 `runtime error:` |

**The battery found one real race, in macgyver, not gis.**
`ResizingCachesMidFlightIsSafe` makes TSan report a data race in the sharded
`Fmi::Cache::Cache`: `resize()` writes `itsMaxSizePerShard` *before* taking any
shard lock (`macgyver/Cache.h:283`, and `:296` in the eviction-reporting
overload), while `statistics()` (`:122`) and `insert()` (`:140`) read it with
no synchronisation - reachable from gis whenever `SetCacheSize()` runs
concurrently with any cache use. On x86 a torn `std::size_t` will not occur in
practice, which is why the test still *passes functionally*; it is still
undefined behaviour and trivially fixable in macgyver by making
`itsMaxSizePerShard` a `std::atomic<std::size_t>` (relaxed) or by hoisting the
member write under the shard locks. Fix belongs on macgyver's `proj-safety`
branch next to the `Cache::find()` shared-lock change; until it lands, TSan
runs of the battery need `race:macgyver/Cache.h` in a suppressions file, and
the four reports it silences are all this one location. The TSan row above was
measured with exactly that suppression and nothing else suppressed.

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


## 8. Re-audit of PROJ master, 2026-08-29

Checked against PROJ 9.9.0-dev, commit `3641df4d02b1d7cea174282666bea15deda00466`
(master, 2026-08-28).

**Nothing has changed.** 41 commits landed since the baseline commit this
document analysed (`620ac36`, 2026-07-23); all of them are new projections
(Hourglass, IVEA, Snyder polyhedral, interrupted variants, TM zoned grid),
EPSG database updates (12.059a → 13.102) and CI work. None touches
synchronisation, `mutable` state, or the context machinery. Every defect above
re-verified present at the same locations:

| Defect | Status on master |
|---|---|
| §2.1/§5.2 `mutable lastWKT/lastPROJString/lastJSONString`, interior-pointer returns | unchanged (`src/proj_internal.h:672-674`, `c_api.cpp:1708-1709`) |
| §5.2 `mutable PJ_TYPE type`, `gridsNeeded*` | unchanged (`proj_internal.h:675-679`) |
| §2.2/§5.3 ten `NullLock` LRU caches + `mapSqlToStatement_` in `DatabaseContext` | unchanged (`factory.cpp:847,858-877`) |
| §5.3 process-wide unsynchronised default context | unchanged (`ctx.cpp:188-194`) |
| §5.4 five classes documented single-thread-at-a-time | unchanged (`io.hpp:186,405,536,874,1009`) |
| §5.5 mutable grid decode buffers | unchanged (`grids.cpp:207,456-457,2091,2853-2854`) |

Three items this document had not listed, found on this pass. All are minor or
transformation-side, none changes the conclusions:

* `PJCoordOperation::isInstantiableCached` (`proj_internal.h:446`, written
  lazily at `coord_operation.cpp:51-52`) — one more lazy `mutable` inside
  transformation objects; same category as §5.5, covered by the
  one-`PJ`-per-thread rule.
* `proj_info()` (`src/info.cpp:88-130`) fills a file-scope `static PJ_INFO`
  under `core_lock`, `free()`s the previous `searchpath` and returns pointers
  to that static storage — the §5.2 interior-pointer shape one level up, in a
  cold path.
* `src/rtodms.cpp:16-18` — `static double RES, RES60, CONV; static int dolong;`
  written by `set_rtodms()` and read by the public `proj_rtodms()` /
  `proj_rtodms2()` with no synchronisation. Cold, CLI-oriented, but public API.

**Upstream status.** GitHub has no open issue or PR mentioning thread safety
(the last thread-related fix was #4692, a `localtime()` fix, March 2026).
Neither reproduced crash in §2 has been reported by anyone. The field is clear:
nothing here is being worked on, and nothing will change until someone files it.

**The deprecate-and-replace path is viable with machinery PROJ already has.**
The three formatting functions cannot be fixed in place — their signature
*returns* the interior pointer, so no implementation can make them safe on a
shared object. But PROJ already has the two ingredients the additive plan in
§5.2/§6 needs:

* `PROJ_DEPRECATED(decl, msg)` (`src/proj.h:158-172`), already applied to
  `proj_list_units()` (`:795`) — attribute-based, per-compiler, warning-only.
* The caller-owned-result-plus-destructor convention:
  `proj_string_list_destroy()` (`:1187`), `proj_unit_list_destroy()` (`:1334`).

So the migration is: add the `_alloc` variants (P2), reimplement the old
functions on top of them, mark the old ones `PROJ_DEPRECATED` with a message
naming the replacement, and drop the `mutable` members once a major release has
passed. No ABI break at any point, and GDAL is a motivated first adopter — it
pays two unconditional mutexes today (`ogrspatialreference.cpp:1755-1758`,
`:11755-11761`) purely to work around `proj_as_wkt()`.


## 9. Provenance: why the `mutable` members exist at all

A fair question is whether the mutable caches are pre-C++11 legacy that predates
the "const implies thread-safe" convention. Git archaeology says no — they are
deliberate, modern, and *licensed by a documented contract*. That matters for
the upstream pitch, because it means the fix is an API-contract change, not a
cleanup of forgotten code.

**Three strata with three different explanations:**

*The genuinely pre-C++11 layer.* `pj_ctx` with `last_errno` (the C `errno`
idiom), the static `PJ_INFO` in `info.cpp`, the `rtodms.cpp` statics — proj.4
heritage. Notably, the context itself was Frank Warmerdam's 2010 **fix** for
the previous generation of thread bugs (`ec678c07`, "preliminary implementation
of projCtx API"): one-context-per-thread was the thread-safety *solution* of
2010, and it is still the load-bearing contract today.

*The ISO19111 C++ object model (2018).* RFC 2 (`docs/source/community/rfc/rfc-2.rst:163-168`)
states the intent explicitly: *"all ISO19111 objects are immutable after
creation … Consequently they could possibly [be] used in a thread-safe way.
There are however classes like PROJStringFormatter, WKTFormatter,
DatabaseContext, AuthorityFactory and CoordinateOperationContext whose
instances are mutable and thus can not be used by multiple threads at once."*
And the implementation honours it: `src/iso19111/*.cpp` contains **zero**
`mutable` members (only the vendored nlohmann/json has any). The author knew
the convention and designed for it. One caveat: `DatabaseContext` /
`AuthorityFactory` mutate their caches from `const` methods *without* the
`mutable` keyword, through the pimpl loophole — `d` is a
`std::unique_ptr<Private>`, whose constness is shallow, so
`createUnitOfMeasure(...) const` inserts into `cacheUOM_` with no `mutable`
anywhere (`factory.cpp:1064-1067`). Counting `mutable` therefore *undercounts*
const-mutation; RFC 2's honest class list is the real inventory.

*The C API bridge (where our crashes live).* The caches arrived with RFC 2's
original `PJ_OBJ` struct (`d928db15`, 2018-11-14), which carried its own
explicit contract — *"Should be used by at most one thread at a time"* — and
held `lastWKT` / `lastPROJString` / `gridsNeeded` as **plain members**, since
`proj_obj_as_wkt()` took a non-const pointer. The design serves the GDAL-style
C convention of returning a borrowed `const char*` (no caller `free()`, easy
language bindings): the storage must outlive the call, so it was hung on the
object, with the documented lifetime *"valid … until a next call to
proj_as_wkt() with the same input object"* (`c_api.cpp:1611-1613`) — phrasing
that is single-threaded by construction. The `mutable` keyword appeared two
weeks later (`cf855b24`, 2018-11-28, "C API extensions and renaming") when the
signatures were const-ified to `const PJ*` — i.e. the `const` in today's
signature documents "logically non-modifying", not shareability, and the
`mutable` is what reconciles the cosmetic constness with the cache. The merge
of `PJ_OBJ` into `PJconsts` (`53a81c44`, 2018-12-26) is what wired CRS
descriptions into the context-carrying transformation struct (§5.1). The later
additions are performance memoizations under the same contract:
`mutable PJ_TYPE type` (`6a43bee9`, 2021, for `proj_factors()`, #2965) and
`PJCoordOperation::isInstantiableCached` (`d9503987`, 2023, `proj_trans()`
regression fix).

**Two consequences for the plan in §6.** First, the underlying C++
`exportToWKT()` is already const and pure — the mutation exists *only* in the
wrapper — so the `_alloc` variants (P2) are trivial to implement and the value
classes need no work at all. Second, because the caches are contract-licensed
rather than accidental, upstream is entitled to keep the old functions'
behaviour; the deprecation cycle is the honest route, not a courtesy. On the
"const implies thread safety" premise itself: C++11 requires it only of the
standard library (`[res.on.data.races]`); for user types it is convention
(Sutter's "const means thread-safe", Meyers EMC++ Item 16: mutable members
used in const functions must be internally synchronised). RFC 2 shows PROJ
followed the convention where it designed for it and documented its way out
where it did not — the trap is that a `const PJ*` parameter reads as the
convention while the contract says otherwise.


## 10. Implemented: the full fix, on a local branch (2026-08-30)

Everything §6 asked of PROJ — and considerably more — is now implemented in
`~/hub/PROJ`, branch `thread-safety` (nine commits on master `3641df4`,
2026-08-28). **Local only: not pushed, no pull requests**, per explicit
decision; the branch is the reference implementation for an eventual upstream
conversation.

What the branch changes, in commit order:

1. **Per-thread default context** (P4). `pj_get_default_ctx()` returns a
   thread-local clone of a process-wide template; configuration setters on
   the default context mirror into the template so threads started later
   inherit it. Fixes §2.2 structurally, makes `proj_context_errno(NULL)`
   per-thread, keeps `proj_context_create()` semantics.
2. **`proj_as_wkt_alloc()` / `proj_as_proj_string_alloc()` /
   `proj_as_projjson_alloc()`** (P2) returning caller-owned strings
   (`proj_string_destroy()` already existed upstream, added in 8.1). The old
   three are reimplemented over shared helpers with the per-`PJ` cache behind
   a new mutex — identical-argument concurrent calls now return stable
   pointers — and carry `PROJ_DEPRECATED`. `PJ::type` became
   `std::atomic<PJ_TYPE>` (P3); `gridsNeeded` fills under the same mutex.
3. **Shareable contexts for coordinate-operation paths.** `last_errno`,
   `debug_level`, `forceOver`, `defer_grid_opening`, `epsg_file_exists`,
   `networking.enabled` became atomic; a recursive `lazyMutex` guards
   `lookupedFiles`, proj.ini loading, `cpp_context` creation and
   `lastFullErrorMessage`; the ten `DatabaseContext` LRU caches got the
   `std::mutex` lock policy (P1) and `run()` serializes the whole prepared
   statement lifecycle.
4. **`proj_trans()` on a shared transformation** is bit-identical to
   single-threaded execution: `iCurCoordOp` and the one-shot warning flag are
   atomic, `cached_op_for_proj_factors` publishes by compare-and-exchange,
   `isInstantiableCached` is atomic.
5. **Grid readers** (§5.5, which §6 had written off as thread-affine
   forever): per-grid / per-dataset I/O mutexes for GTX, NTv1, CTable2, NTv2
   and GTiff; `reopen()` *retires* replaced grids and datasets instead of
   destroying them — which also fixed a pre-existing, single-threaded
   use-after-free (`HorizontalShiftGridSet::reopen()` destroyed the freshly
   opened set whose file handle and line cache the stolen NTv2 grids still
   referenced). Deformation-model and TIN-shift evaluators serialize per
   object.
6. **`proj_info()` and `proj_rtodms2()`** moved to thread-local state (the
   new finds from §8).
7. **`docs/source/development/threads.rst`** states the new contract (P5/P6).

Verification, which is where the honest work was:

* `test/unit/test_thread_safety.cpp` — 24 tests, every documented concurrent
  pattern, results compared bit-exactly against single-threaded references;
  covers all five grid formats plus defmodel and tinshift using PROJ's own
  test grids.
* Full suite green in the normal build (71/72 — the one failure is a
  projinfo message text that expects a curl-enabled build).
* **10× repeats clean under both ThreadSanitizer and ASan+UBSan**, run from
  out-of-tree build directories with injected flags — deliberately zero
  build-system changes, since PROJ's CI has only one ASan job and no TSan,
  and a build change would be a separate, contentious conversation.

The sanitizers earned their keep twice. TSan found `forceOver` being flipped
transiently around `pj_obj_create()` — a race class the July analysis had
missed entirely. And ASan caught the one real bug *introduced by the branch*:
the PROJ-string parser installs a transient, stack-capturing error logger via
`proj_log_func()`, which under the new template-mirroring left the template
pointing at a dead stack frame for every later-created thread to inherit —
stack-use-after-return, and the same corruption crashed TSan's own runtime.
The parser now installs and restores the handler by direct assignment. Both
finds are regression-tested.


## 11. The GDAL side, given the PROJ branch (audited 2026-08-30)

Audited against GDAL master `c70081f` (2026-08-28, 3.14.0dev). Every §3
finding is unchanged: the mutating `GetAttrNode` is still unlocked
(`ogrspatialreference.cpp:1250`), the optional lock still covers one file out
of 23 (138 sites in `ogrspatialreference.cpp`, zero elsewhere), `Clone()`
still drops the thread-safe flag (`:1519`), the two unconditional mutexes
still stand (`:1758`, `:11761`), `g_bForkOccurred` is still a plain bool, and
`multithreading.rst` still says nothing about `OGRSpatialReference`.

What changes with the PROJ `thread-safety` branch is the *shape* of the fix,
not the list. Scope below is "projection issues only": the OSR layer, not
GDAL's driver zoo.

**What GDAL gets for free, with no change.** The §2.2 default-context
corruption class is gone even for code that passes null contexts; a
`proj_context_create()` in GDAL's TLS machinery now inherits configuration
applied to the default context before threads started; and the description
`PJ` inside `OGRSpatialReference::Private` is safe to *read* concurrently at
the PROJ level. Every remaining hazard in OSR is GDAL's own lazy state.

**The fix list, reprioritised:**

* **G0 (new, unlocked by the PROJ branch, do first).** Migrate the eleven
  call sites of `proj_as_wkt` / `proj_as_proj_string` / `proj_as_projjson`
  (7 in `ogrspatialreference.cpp`, 2 in `ogrct.cpp`, 1 each in
  `frmts/gtiff/gt_wkt_srs.cpp` and `frmts/hdf5/s100.cpp`) to the `_alloc`
  variants under `#if PROJ_AT_LEAST_VERSION(9,9,0)` — the version-gating
  idiom already used throughout `ogrct.cpp`. Then downgrade the two
  unconditional `std::lock_guard(d->m_mutex)` in `exportToWkt()` and
  `exportToProj4()` to `TAKE_OPTIONAL_LOCK()`: their comments say explicitly
  they exist only because "proj_as_wkt() will cache the result internally".
  This removes permanent serialisation from the two hottest export paths for
  every GDAL user, thread-safe mode or not — and it is also what keeps a
  `-Werror` GDAL building against a PROJ that carries the deprecation
  attributes. Small.
* **G1.** The one-line missing lock in the mutating `GetAttrNode`. Trivial.
* **G2.** Move `Private`, `OptionalLockGuard` and `TAKE_OPTIONAL_LOCK` into
  an internal header and take the guard at the top of each public method in
  the other 22 files (`ogr_srs_esri/pci/usgs/erm/panorama/ozi/isis/dict/xml`,
  `ogr_fromepsg`, ...), giving them the whole-operation envelope the
  recursive mutex was designed for. Mechanical, small-medium.
* **G3.** Propagate the thread-safe flag in `Clone()`; add `SetThreadSafe()`,
  `IsThreadSafe()`, `OSRSetThreadSafe()`. Small.
* **G4.** Value-returning accessors (`GetAttrValueAsString()`,
  `GetAngularUnitsName()`, `GetEPSGCode()` returning `std::optional<int>`,
  ...) so callers stop holding interior pointers into the node tree;
  precedent since 3.9's `std::string exportToWkt()`. Medium.
* **G5 — still the centrepiece.** `Freeze()` / `IsFrozen()`: materialise
  `refreshProjObj()`, the node tree, axis mapping and units eagerly, after
  which const reads need no lock at all and scale linearly instead of the
  measured 12x regression at 8 threads. The PROJ branch removes its last
  obstacle: the materialisation can use the `_alloc` exports, and the frozen
  object's inner `PJ` is genuinely shareable. Medium.
* **G6.** An OSR section in `multithreading.rst`. Small.
* **G7 (later, optional).** Opt-in process-wide frozen-CRS cache to stop N
  threads parsing the same CRS from proj.db independently.
* **G8.** Minors: `g_bForkOccurred` to `std::atomic<bool>`; `std::unique_lock`
  in `OSRGetPROJEnableNetwork()`.

**What should deliberately not change.** `OGRCoordinateTransformation`
remains clone-per-thread: `OGRProjCT` mutates per-object state on the
`Transform()` hot path (`nErrorCount`, `m_differentOperationsUsed`, the
selected-operation bookkeeping), and even at the PROJ level a shared
transformation is serialised, not parallel. The per-thread `PJ_CONTEXT` and
`OSRProjTLSCache` likewise stay — they are the scalable design. The twelve
`proj_assign_context()` sites (§3.6) also stay until PROJ some day decouples
a description `PJ` from its context; they are lifetime management, not a
race.

**Testing, mirroring the PROJ branch:** an OSR concurrency battery
(concurrent const reads on a frozen and on a thread-safe SRS, mixed
readers/writer on a thread-safe SRS exercising the `ogr_srs_*` methods,
concurrent `exportToWkt`/`exportToProj4` with pointer-stability checks,
`Clone()`-propagation, plus concurrent
`OGRCreateCoordinateTransformation()` construction from shared SRS objects),
run under TSan and ASan from out-of-tree builds. GDAL's CI has ASan but no
TSan, same as PROJ had.

Altogether this is a far smaller job than the PROJ branch was — roughly a
tenth of the surface — because GDAL's object model needs no redesign: the
work is one missing lock, one lock-scope move, additive accessors, `Freeze()`
and the `_alloc` migration.
