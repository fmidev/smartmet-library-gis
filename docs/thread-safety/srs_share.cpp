// Mirrors what SmartMet does: two OGRSpatialReference objects are created once,
// cached, and then shared by all worker threads, which pass them to
// OGRCreateCoordinateTransformation() and query them with getters.
//
// GDAL clones both SRS internally, so every thread reads (and lazily rebuilds)
// the shared objects concurrently.
//
// Usage: srs_share <mode> [seconds] [threads]
//   mode 0 = plain shared SRS
//   mode 1 = shared SRS built with AssignAndSetThreadSafe()

#include <ogr_spatialref.h>
#include <cpl_conv.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static std::atomic<bool> g_stop{false};
static std::atomic<long> g_iter{0};
static std::atomic<long> g_fail{0};

static void worker(OGRSpatialReference *src, OGRSpatialReference *tgt)
{
    while (!g_stop)
    {
        auto *ct = OGRCreateCoordinateTransformation(src, tgt);
        if (!ct)
        {
            g_fail++;
        }
        else
        {
            double x = 25.0;
            double y = 60.0;
            if (!ct->Transform(1, &x, &y))
                g_fail++;
            delete ct;
        }
        g_iter++;
    }
}

static void getter(OGRSpatialReference *srs)
{
    while (!g_stop)
    {
        const char *unit = nullptr;
        srs->GetLinearUnits(&unit);
        srs->IsProjected();
        srs->GetAttrValue("AUTHORITY", 1);
        srs->EPSGTreatsAsLatLong();
        g_iter++;
    }
}

int main(int argc, char **argv)
{
    const int mode = (argc > 1) ? atoi(argv[1]) : 0;
    const int seconds = (argc > 2) ? atoi(argv[2]) : 5;
    const int nthreads = (argc > 3) ? atoi(argv[3]) : 4;

    OGRSpatialReference a;
    OGRSpatialReference b;
    a.SetFromUserInput("EPSG:4326");
    b.SetFromUserInput("EPSG:3067");
    a.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    b.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    auto *src = new OGRSpatialReference();
    auto *tgt = new OGRSpatialReference();
    if (mode == 1)
    {
        src->AssignAndSetThreadSafe(a);
        tgt->AssignAndSetThreadSafe(b);
    }
    else
    {
        *src = a;
        *tgt = b;
    }

    // Warm up single-threaded.
    delete OGRCreateCoordinateTransformation(src, tgt);
    src->GetAttrValue("AUTHORITY", 1);
    tgt->GetAttrValue("AUTHORITY", 1);

    printf("mode=%d (%s), %d threads, %d s\n", mode,
           mode == 1 ? "AssignAndSetThreadSafe" : "plain shared", nthreads,
           seconds);

    std::vector<std::thread> t;
    for (int i = 0; i < nthreads; i++)
        t.emplace_back(worker, src, tgt);
    t.emplace_back(getter, src);
    t.emplace_back(getter, tgt);

    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    g_stop = true;
    for (auto &th : t)
        th.join();

    printf("iterations : %ld\n", g_iter.load());
    printf("failures   : %ld\n", g_fail.load());
    return g_fail ? 1 : 0;
}
