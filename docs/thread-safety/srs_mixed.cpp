// Readers concurrent with a writer on one shared *thread-safe*
// OGRSpatialReference (AssignAndSetThreadSafe, GDAL >= 3.10).
//
// Readers run exportToPCI() - implemented in ogr_srs_pci.cpp, outside the
// file that can take the optional lock, so on unpatched GDAL it is a
// sequence of independently locked calls holding interior pointers across
// the gaps (section 3.1 of ../proj-gdal-thread-safety.md) - plus
// exportToWkt(). The writer alternates between two full CRS definitions
// with importFromEPSG().
//
// On unpatched GDAL this crashes or returns torn results; with the
// whole-operation lock envelope (G2) every reader result corresponds to
// exactly one of the two states.
//
// Usage: srs_mixed [threads=6] [seconds=5]

#include <cpl_conv.h>
#include <gdal.h>
#include <ogr_spatialref.h>
#include <ogr_srs_api.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char **argv)
{
    const int nThreads = argc > 1 ? atoi(argv[1]) : 6;
    const int nSeconds = argc > 2 ? atoi(argv[2]) : 5;

    printf("GDAL %s\n", GDALVersionInfo("RELEASE_NAME"));

    OGRSpatialReference oSource;
    if (oSource.importFromEPSG(32631) != OGRERR_NONE)
    {
        fprintf(stderr, "importFromEPSG(32631) failed\n");
        return 1;
    }
    OGRSpatialReference oSRS;
    oSRS.AssignAndSetThreadSafe(oSource);

    // Reference values for the two states the writer alternates between
    std::string osWkt1, osWkt2, osPCI1, osPCI2;
    for (int i = 0; i < 2; ++i)
    {
        OGRSpatialReference oRef;
        oRef.importFromEPSG(i == 0 ? 32631 : 2393);
        (i == 0 ? osWkt1 : osWkt2) = oRef.exportToWkt();
        char *pszProj = nullptr;
        char *pszUnits = nullptr;
        double *padfParams = nullptr;
        if (oRef.exportToPCI(&pszProj, &pszUnits, &padfParams) == OGRERR_NONE)
            (i == 0 ? osPCI1 : osPCI2) = pszProj;
        CPLFree(pszProj);
        CPLFree(pszUnits);
        CPLFree(padfParams);
    }

    std::atomic<bool> bStop{false};
    std::atomic<long> nReads{0};
    std::atomic<long> nWrites{0};
    std::atomic<long> nTorn{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < nThreads - 1; ++i)
    {
        threads.emplace_back(
            [&]()
            {
                while (!bStop)
                {
                    const std::string osWkt = oSRS.exportToWkt();
                    if (osWkt != osWkt1 && osWkt != osWkt2)
                        ++nTorn;

                    char *pszProj = nullptr;
                    char *pszUnits = nullptr;
                    double *padfParams = nullptr;
                    if (oSRS.exportToPCI(&pszProj, &pszUnits, &padfParams) ==
                        OGRERR_NONE)
                    {
                        if (osPCI1 != pszProj && osPCI2 != pszProj)
                            ++nTorn;
                    }
                    CPLFree(pszProj);
                    CPLFree(pszUnits);
                    CPLFree(padfParams);
                    ++nReads;
                }
            });
    }
    threads.emplace_back(
        [&]()
        {
            int i = 0;
            while (!bStop)
            {
                oSRS.importFromEPSG((i++ % 2) == 0 ? 2393 : 32631);
                ++nWrites;
            }
        });

    std::this_thread::sleep_for(std::chrono::seconds(nSeconds));
    bStop = true;
    for (auto &t : threads)
        t.join();

    printf("reads=%ld writes=%ld torn=%ld -> %s\n", nReads.load(),
           nWrites.load(), nTorn.load(), nTorn == 0 ? "OK" : "TORN RESULTS");
    return nTorn == 0 ? 0 : 1;
}
