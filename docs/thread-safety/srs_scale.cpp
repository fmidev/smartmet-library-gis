// How well does read-only use of an OGRSpatialReference scale?
//
//   shared : all threads call exportToWkt() on ONE OGRSpatialReference.
//            exportToWkt() takes d->m_mutex unconditionally (GDAL added that
//            because proj_as_wkt() caches inside the PJ), so this serialises.
//   percpu : every thread owns a private Clone(). No sharing, no lock.
//
// Usage: srs_scale <shared|percpu> <threads> [seconds]

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

static void run(OGRSpatialReference *srs, long *count)
{
    long n = 0;
    while (!g_stop)
    {
        char *wkt = nullptr;
        srs->exportToWkt(&wkt);
        CPLFree(wkt);
        n++;
    }
    *count = n;
}

int main(int argc, char **argv)
{
    const bool shared = (argc > 1) && strcmp(argv[1], "shared") == 0;
    const int nthreads = (argc > 2) ? atoi(argv[2]) : 4;
    const int seconds = (argc > 3) ? atoi(argv[3]) : 3;

    OGRSpatialReference base;
    base.SetFromUserInput("EPSG:3067");
    base.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    std::vector<OGRSpatialReference *> owned;
    std::vector<long> counts(nthreads, 0);
    std::vector<std::thread> t;

    for (int i = 0; i < nthreads; i++)
        owned.push_back(shared && i > 0 ? owned[0] : base.Clone());

    for (int i = 0; i < nthreads; i++)
        t.emplace_back(run, owned[i], &counts[i]);

    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    g_stop = true;
    for (auto &th : t)
        th.join();

    long total = 0;
    for (long c : counts)
        total += c;
    printf("%-7s threads=%2d  %10.0f exportToWkt/s\n",
           shared ? "shared" : "percpu", nthreads,
           double(total) / seconds);
    return 0;
}
