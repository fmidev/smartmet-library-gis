// Demonstrators for two PROJ-level thread-safety defects.
//
//   test 1 (shared PJ, read-only calls): several threads call proj_as_wkt()
//           on ONE shared PJ*. proj_as_wkt() stores its result in the mutable
//           PJ::lastWKT member and returns lastWKT.c_str(), so concurrent
//           "readers" write the same std::string and hand out pointers into
//           each other's buffer.
//
//   test 2 (shared default context): several threads call proj_create(nullptr,
//           "EPSG:nnnn"). A null context resolves to the process-wide
//           pj_get_default_ctx() singleton, whose lazily created
//           projCppContext / DatabaseContext / prepared-statement map and
//           10 LRU caches have no synchronisation at all.
//
// Usage: proj_race <1|2> [seconds]

#include <proj.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static std::atomic<bool> g_stop{false};
static std::atomic<long> g_iter{0};
static std::atomic<long> g_bad{0};
static std::atomic<long> g_null{0};

// ---------------------------------------------------------------- test 1

static void wktReader(PJ *shared, PJ_CONTEXT *ctx, const std::string &expected)
{
    while (!g_stop)
    {
        const char *wkt = proj_as_wkt(ctx, shared, PJ_WKT2_2019, nullptr);
        if (!wkt)
        {
            g_null++;
        }
        else
        {
            // Copy immediately, exactly as a well-behaved caller would.
            const std::string got(wkt);
            if (got != expected)
                g_bad++;
        }
        g_iter++;
    }
}

static int test_shared_pj(int seconds, int nthreads)
{
    PJ_CONTEXT *mainCtx = proj_context_create();
    PJ *crs = proj_create(mainCtx, "EPSG:3067");
    if (!crs)
    {
        fprintf(stderr, "proj_create failed\n");
        return 2;
    }
    const std::string expected(proj_as_wkt(mainCtx, crs, PJ_WKT2_2019, nullptr));
    printf("shared PJ EPSG:3067, expected WKT is %zu bytes\n", expected.size());

    // One context per thread, as PROJ documents. Only the PJ is shared.
    std::vector<PJ_CONTEXT *> ctxs;
    std::vector<std::thread> t;
    for (int i = 0; i < nthreads; i++)
        ctxs.push_back(proj_context_create());
    for (int i = 0; i < nthreads; i++)
        t.emplace_back(wktReader, crs, ctxs[i], expected);

    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    g_stop = true;
    for (auto &th : t)
        th.join();

    printf("iterations   : %ld\n", g_iter.load());
    printf("null returns : %ld\n", g_null.load());
    printf("wrong WKT    : %ld\n", g_bad.load());

    for (auto *c : ctxs)
        proj_context_destroy(c);
    proj_destroy(crs);
    proj_context_destroy(mainCtx);
    return (g_bad || g_null) ? 1 : 0;
}

// ---------------------------------------------------------------- test 2

static void defaultCtxCreator(int seed)
{
    // A spread of real EPSG codes so the per-context LRU caches actually churn.
    static const int codes[] = {4326, 3067, 3857, 32635, 2393, 4258,
                                3035, 25835, 27700, 31467, 2154, 5514};
    int i = seed;
    while (!g_stop)
    {
        const int code = codes[(i++) % (sizeof(codes) / sizeof(codes[0]))];
        char buf[32];
        snprintf(buf, sizeof(buf), "EPSG:%d", code);
        PJ *p = proj_create(nullptr, buf);  // nullptr => shared default context
        if (!p)
            g_bad++;
        else
            proj_destroy(p);
        g_iter++;
    }
}

static int test_default_context(int seconds, int nthreads)
{
    printf("proj_create(nullptr, ...) from %d threads (shared default "
           "context)\n",
           nthreads);
    std::vector<std::thread> t;
    for (int i = 0; i < nthreads; i++)
        t.emplace_back(defaultCtxCreator, i);

    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    g_stop = true;
    for (auto &th : t)
        th.join();

    printf("iterations   : %ld\n", g_iter.load());
    printf("failures     : %ld\n", g_bad.load());
    return g_bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    const int which = (argc > 1) ? atoi(argv[1]) : 1;
    const int seconds = (argc > 2) ? atoi(argv[2]) : 5;
    const int nthreads = (argc > 3) ? atoi(argv[3]) : 4;

    printf("PROJ %d.%d.%d\n", PROJ_VERSION_MAJOR, PROJ_VERSION_MINOR,
           PROJ_VERSION_PATCH);

    const int rc = (which == 1) ? test_shared_pj(seconds, nthreads)
                                : test_default_context(seconds, nthreads);
    printf("%s\n", rc ? "=> OBSERVED FAILURE" : "=> nothing observed");
    return rc;
}
