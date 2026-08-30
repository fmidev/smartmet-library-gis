// How well does read-only use of an OGRSpatialReference scale?
//
//   shared : all threads call exportToWkt() on ONE OGRSpatialReference.
//            On unpatched GDAL exportToWkt() takes d->m_mutex
//            unconditionally (added because proj_as_wkt() caches inside the
//            PJ), so this serialises. On the thread-safety branch with
//            PROJ >= 9.9 the unconditional lock is gone.
//   tsafe  : like shared, but the object is put in the thread-safe (mutex)
//            mode with AssignAndSetThreadSafe() (GDAL >= 3.10).
//   frozen : like shared, but the object is frozen with Freeze()
//            (thread-safety branch only): lock-free concurrent reads.
//   percpu : every thread owns a private Clone(). No sharing, no lock.
//
// Usage: srs_scale <shared|tsafe|frozen|percpu> <threads> [seconds]

#include <cpl_conv.h>
#include <gdal_version.h>
#include <ogr_spatialref.h>

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
    const char *mode = (argc > 1) ? argv[1] : "percpu";
    const bool percpu = strcmp(mode, "percpu") == 0;
    const int nthreads = (argc > 2) ? atoi(argv[2]) : 4;
    const int seconds = (argc > 3) ? atoi(argv[3]) : 3;

    OGRSpatialReference base;
    base.SetFromUserInput("EPSG:3067");
    base.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    std::vector<OGRSpatialReference *> owned;
    std::vector<long> counts(nthreads, 0);
    std::vector<std::thread> t;

    for (int i = 0; i < nthreads; i++)
        owned.push_back(!percpu && i > 0 ? owned[0] : base.Clone());

    if (strcmp(mode, "tsafe") == 0)
    {
        OGRSpatialReference tmp(*owned[0]);
        owned[0]->AssignAndSetThreadSafe(tmp);
    }
    else if (strcmp(mode, "frozen") == 0)
    {
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(3, 14, 0)
        if (owned[0]->Freeze() != OGRERR_NONE)
        {
            fprintf(stderr, "Freeze() failed\n");
            return 1;
        }
#else
        fprintf(stderr, "frozen mode needs GDAL >= 3.14\n");
        return 1;
#endif
    }

    for (int i = 0; i < nthreads; i++)
        t.emplace_back(run, owned[i], &counts[i]);

    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    g_stop = true;
    for (auto &th : t)
        th.join();

    long total = 0;
    for (long c : counts)
        total += c;
    printf("%-7s threads=%2d  %10.0f exportToWkt/s\n", mode, nthreads,
           double(total) / seconds);
    return 0;
}
