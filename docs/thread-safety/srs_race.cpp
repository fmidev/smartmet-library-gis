// Demonstrator: is a *shared* OGRSpatialReference safe for concurrent
// read-only-looking use?
//
// Usage: srs_race <mode> [seconds]
//   mode 0 = plain shared SRS (default construction)
//   mode 1 = shared SRS made "thread-safe" via AssignAndSetThreadSafe()
//
// Every thread only calls const / getter methods. Nothing here mutates the
// CRS from the caller's point of view.

#include <ogr_spatialref.h>
#include <cpl_conv.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static std::atomic<bool> g_stop{false};
static std::atomic<long> g_badAttr{0};
static std::atomic<long> g_badUnits{0};
static std::atomic<long> g_badWkt{0};
static std::atomic<long> g_iter{0};

// Read AUTHORITY code out of the lazily-built OGR_SRSNode tree.
static void attrReader(const OGRSpatialReference *srs)
{
    while (!g_stop)
    {
        const char *v = srs->GetAttrValue("AUTHORITY", 1);
        // The returned pointer is interior to the SRS node tree and is not
        // owned by us. Just touching it is what a normal caller does.
        if (v == nullptr || strcmp(v, "3067") != 0)
            g_badAttr++;
        g_iter++;
    }
}

// Force the node tree to be discarded and rebuilt as WKT2: GetAttrNode()
// special-cases a path containing "CONVERSION" and calls invalidateNodes() +
// refreshRootFromProjObj(true) -- outside of any lock.
static void conversionReader(const OGRSpatialReference *srs)
{
    while (!g_stop)
    {
        srs->GetAttrValue("PROJCRS|CONVERSION", 0);
        srs->GetAttrValue("PROJECTION", 0);
        g_iter++;
    }
}

// Getters that hand back a pointer into a member std::string.
static void unitsReader(const OGRSpatialReference *srs)
{
    while (!g_stop)
    {
        const char *name = nullptr;
        const double toMeter = srs->GetLinearUnits(&name);
        if (name == nullptr || strcmp(name, "metre") != 0 || toMeter != 1.0)
            g_badUnits++;
        g_iter++;
    }
}

static void wktReader(const OGRSpatialReference *srs)
{
    while (!g_stop)
    {
        char *wkt = nullptr;
        if (srs->exportToWkt(&wkt) != OGRERR_NONE || wkt == nullptr ||
            strstr(wkt, "3067") == nullptr)
            g_badWkt++;
        CPLFree(wkt);
        g_iter++;
    }
}

static void cloner(const OGRSpatialReference *srs)
{
    while (!g_stop)
    {
        OGRSpatialReference *c = srs->Clone();
        if (c)
            c->Release();
        g_iter++;
    }
}

int main(int argc, char **argv)
{
    const int mode = (argc > 1) ? atoi(argv[1]) : 0;
    const int seconds = (argc > 2) ? atoi(argv[2]) : 5;

    OGRSpatialReference base;
    if (base.importFromEPSG(3067) != OGRERR_NONE)
    {
        fprintf(stderr, "importFromEPSG(3067) failed\n");
        return 2;
    }
    base.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    OGRSpatialReference shared;
    if (mode == 1)
        shared.AssignAndSetThreadSafe(base);
    else
        shared = base;

    // Warm the object up single-threaded, so the test is about concurrent
    // *reads* of an already-populated object, not about lazy first init.
    (void)shared.GetAttrValue("AUTHORITY", 1);
    {
        char *w = nullptr;
        shared.exportToWkt(&w);
        CPLFree(w);
    }

    printf("mode=%d (%s), %d s\n", mode,
           mode == 1 ? "AssignAndSetThreadSafe" : "plain shared", seconds);

    std::vector<std::thread> t;
    t.emplace_back(attrReader, &shared);
    t.emplace_back(attrReader, &shared);
    t.emplace_back(conversionReader, &shared);
    t.emplace_back(unitsReader, &shared);
    t.emplace_back(wktReader, &shared);
    t.emplace_back(cloner, &shared);

    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    g_stop = true;
    for (auto &th : t)
        th.join();

    printf("iterations   : %ld\n", g_iter.load());
    printf("bad AUTHORITY: %ld\n", g_badAttr.load());
    printf("bad units    : %ld\n", g_badUnits.load());
    printf("bad WKT      : %ld\n", g_badWkt.load());
    const long bad = g_badAttr + g_badUnits + g_badWkt;
    printf("%s\n", bad ? "=> OBSERVED CORRUPTION" : "=> no wrong values seen");
    return bad ? 1 : 0;
}
