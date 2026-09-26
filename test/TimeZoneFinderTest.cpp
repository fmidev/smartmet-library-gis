// ======================================================================
/*!
 * \brief Tests for Fmi::TimeZoneFinder
 *
 * data/timezones.geojson contains small clips of timezone-boundary-builder
 * 2026d (with oceans) around Tornio-Haparanda, Narva-Ivangorod, the Baarle
 * enclaves, the Hebron-Jerusalem overlap and the Bothnian Sea.
 */
// ======================================================================

#include <gtest/gtest.h>

#include "TimeZoneFinder.h"
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>

#ifndef GTEST_SKIP
// RHEL8 ships gtest 1.8.0, GTEST_SKIP is available only since 1.10.0
#define GTEST_SKIP() return GTEST_SUCCESS_("Skipped")
#endif

namespace
{
const char* testdata = "data/timezones.geojson";

const Fmi::TimeZoneFinder& finder()
{
  static const Fmi::TimeZoneFinder f(testdata);
  return f;
}
}  // namespace

TEST(TimeZoneFinder, TwinTowns)
{
  const auto& f = finder();
  EXPECT_EQ(f.zoneName(24.1440, 65.8481), "Europe/Helsinki");   // Tornio
  EXPECT_EQ(f.zoneName(24.1370, 65.8355), "Europe/Stockholm");  // Haparanda
  EXPECT_EQ(f.zoneName(28.1790, 59.3772), "Europe/Tallinn");    // Narva
  EXPECT_EQ(f.zoneName(28.2300, 59.3700), "Europe/Moscow");     // Ivangorod
}

TEST(TimeZoneFinder, Enclaves)
{
  const auto& f = finder();
  EXPECT_EQ(f.zoneName(4.9290, 51.4440), "Europe/Amsterdam");  // Baarle-Nassau
  EXPECT_EQ(f.zoneName(4.9320, 51.4380), "Europe/Brussels");   // Baarle-Hertog
}

TEST(TimeZoneFinder, TerritorialWatersAndOpenSea)
{
  const auto& f = finder();
  EXPECT_EQ(f.zoneName(21.3, 61.5), "Europe/Helsinki");  // off Pori
  EXPECT_EQ(f.zoneName(19.8, 61.8), "Etc/GMT-1");        // mid Bothnian Sea
}

TEST(TimeZoneFinder, Overlaps)
{
  // The West Bank is covered by both zones, the smaller one wins by default
  const auto& f = finder();
  auto all = f.zoneNames(35.2, 31.65);
  ASSERT_EQ(all.size(), 2U);
  EXPECT_EQ(all[0], "Asia/Hebron");
  EXPECT_EQ(all[1], "Asia/Jerusalem");
  EXPECT_EQ(f.zoneName(35.2, 31.65), "Asia/Hebron");
  EXPECT_EQ(f.zoneNames(35.25, 31.35).size(), 1U);
}

TEST(TimeZoneFinder, PreferredZones)
{
  // Explicit preference overrides the area rule
  Fmi::TimeZoneFinder::Options options;
  options.preferred = {"Asia/Jerusalem"};
  Fmi::TimeZoneFinder g(testdata, options);
  EXPECT_EQ(g.zoneName(35.2, 31.65), "Asia/Jerusalem");
  EXPECT_EQ(g.zoneNames(35.2, 31.65)[0], "Asia/Jerusalem");
}

TEST(TimeZoneFinder, NauticalFallback)
{
  // Outside the test data
  const auto& f = finder();
  EXPECT_TRUE(f.zoneNames(0, 0).empty());
  EXPECT_EQ(f.zoneName(0, 0), "Etc/GMT");
  EXPECT_EQ(f.zoneName(-100, 40), "Etc/GMT+7");
  EXPECT_EQ(f.zoneName(179.9, 10), "Etc/GMT-12");
  EXPECT_EQ(f.zoneName(-179.9, 10), "Etc/GMT+12");
  EXPECT_EQ(f.zoneName(360 + 24.1440, 65.8481), "Europe/Helsinki");  // wrapped longitude
}

TEST(TimeZoneFinder, InvalidCoordinates)
{
  const auto& f = finder();
  EXPECT_ANY_THROW(f.zoneName(std::nan(""), 60));
  EXPECT_ANY_THROW(f.zoneName(25, std::numeric_limits<double>::infinity()));
}

TEST(TimeZoneFinder, InvalidSource)
{
  EXPECT_ANY_THROW(Fmi::TimeZoneFinder("data/no-such-file.shp"));
  Fmi::TimeZoneFinder::Options options;
  options.field = "no_such_field";
  EXPECT_ANY_THROW(Fmi::TimeZoneFinder(testdata, options));
}

TEST(TimeZoneFinder, LoadArea)
{
  Fmi::TimeZoneFinder::Options options;
  options.area = Fmi::TimeZoneFinder::Options::Area{24.0, 65.7, 24.3, 65.95};
  Fmi::TimeZoneFinder f(testdata, options);

  // Only the polygons near Tornio are read, results inside the area are unchanged
  EXPECT_LT(f.statistics().features, finder().statistics().features);
  EXPECT_EQ(f.zoneName(24.1440, 65.8481), "Europe/Helsinki");        // Tornio
  EXPECT_EQ(f.zoneName(24.1370, 65.8355), "Europe/Stockholm");       // Haparanda
  EXPECT_EQ(f.zoneName(24.0, 65.7), finder().zoneName(24.0, 65.7));  // area corner

  EXPECT_TRUE(f.contains(24.1440, 65.8481));
  EXPECT_TRUE(f.contains(24.3, 65.95));
  EXPECT_FALSE(f.contains(24.31, 65.8));
  EXPECT_FALSE(f.contains(28.1790, 59.3772));
  EXPECT_TRUE(finder().contains(28.1790, 59.3772));  // no area

  // Searches outside the area throw instead of returning a wrong zone
  EXPECT_ANY_THROW(f.zoneName(28.1790, 59.3772));
  EXPECT_ANY_THROW(f.zoneNames(28.1790, 59.3772));
  EXPECT_ANY_THROW(f.contains(std::nan(""), 60));
}

TEST(TimeZoneFinder, InvalidArea)
{
  Fmi::TimeZoneFinder::Options options;
  options.area = Fmi::TimeZoneFinder::Options::Area{25, 60, 24, 61};  // west > east
  EXPECT_ANY_THROW(Fmi::TimeZoneFinder(testdata, options));
  options.area = Fmi::TimeZoneFinder::Options::Area{24, 60, 25, 91};  // north > 90
  EXPECT_ANY_THROW(Fmi::TimeZoneFinder(testdata, options));
  options.area = Fmi::TimeZoneFinder::Options::Area{-1, -1, 1, 1};  // no test data there
  EXPECT_ANY_THROW(Fmi::TimeZoneFinder(testdata, options));
}

TEST(TimeZoneFinder, Statistics)
{
  const auto& stats = finder().statistics();
  EXPECT_EQ(stats.features, 10U);
  EXPECT_EQ(stats.zones, 9U);
  EXPECT_EQ(finder().zones().size(), 9U);
  EXPECT_EQ(stats.invalid_features, 0U);
  EXPECT_GT(stats.pieces, 0U);
}

TEST(TimeZoneFinder, DefaultSource)
{
  // The full data set from the smartmet-timezones RPM, if installed
  if (!std::filesystem::exists(Fmi::TimeZoneFinder::default_source))
    GTEST_SKIP() << Fmi::TimeZoneFinder::default_source << " is not installed";

  Fmi::TimeZoneFinder f;
  EXPECT_EQ(f.statistics().invalid_features, 0U);
  EXPECT_EQ(f.zoneName(24.94, 60.17), "Europe/Helsinki");
  EXPECT_EQ(f.zoneName(24.1440, 65.8481), "Europe/Helsinki");  // Tornio
  EXPECT_EQ(f.zoneName(-30.0, 30.0), "Etc/GMT+2");             // Mid-Atlantic

  // The Gulf of Guinea, the old timezone.shz raster claimed Europe/Lisbon here
  EXPECT_EQ(f.zoneName(0, 0), "Etc/GMT");
  EXPECT_EQ(f.zoneNames(0, 0), std::vector<std::string>{"Etc/GMT"});
  EXPECT_EQ(f.zoneName(-0.001, -0.001), "Etc/GMT");
  EXPECT_EQ(f.zoneName(0.001, 0.001), "Etc/GMT");
  EXPECT_FALSE(f.zoneNames(0, 90).empty());   // North pole
  EXPECT_FALSE(f.zoneNames(0, -90).empty());  // South pole
}

TEST(TimeZoneFinder, DefaultSourceWithArea)
{
  if (!std::filesystem::exists(Fmi::TimeZoneFinder::default_source))
    GTEST_SKIP() << Fmi::TimeZoneFinder::default_source << " is not installed";

  Fmi::TimeZoneFinder::Options options;
  options.area = Fmi::TimeZoneFinder::Options::Area{24.0, 65.7, 24.3, 65.95};
  Fmi::TimeZoneFinder f(Fmi::TimeZoneFinder::default_source, options);
  EXPECT_LT(f.statistics().features, 10U);
  EXPECT_EQ(f.zoneName(24.1440, 65.8481), "Europe/Helsinki");   // Tornio
  EXPECT_EQ(f.zoneName(24.1370, 65.8355), "Europe/Stockholm");  // Haparanda

  options.area = Fmi::TimeZoneFinder::Options::Area{-0.1, -0.1, 0.1, 0.1};
  Fmi::TimeZoneFinder g(Fmi::TimeZoneFinder::default_source, options);
  EXPECT_EQ(g.zoneName(0, 0), "Etc/GMT");
}
