#include "Box.h"
#include "TestDefs.h"

#include <fmt/format.h>
#include <regression/tframe.h>

#include <gdal_version.h>
#include <iostream>
#include <ogr_spatialref.h>

using namespace std;

namespace Tests
{
// ----------------------------------------------------------------------

#define BOX_TRANSFORM(fromx, fromy, tox, toy)                                                     \
  {                                                                                               \
    double x = fromx, y = fromy;                                                                  \
    box.transform(x, y);                                                                          \
    if (x != tox || y != toy)                                                                     \
      TEST_FAILED(fmt::format(                                                                    \
          "Failed to transform {},{} to {},{}, got {},{} instead", fromx, fromy, tox, toy, x, y)) \
  };

void transform()
{
  Fmi::Box box(0, 0, 1, 1, 100, 100);

  BOX_TRANSFORM(0, 0, 0, 100);
  BOX_TRANSFORM(0, 1, 0, 0);
  BOX_TRANSFORM(1, 1, 100, 0);
  BOX_TRANSFORM(0.25, 0.75, 25, 25);

  TEST_PASSED();
}

#define BOX_ITRANSFORM(fromx, fromy, tox, toy)                                                     \
  {                                                                                                \
    double x = fromx, y = fromy;                                                                   \
    box.itransform(x, y);                                                                          \
    if (x != tox || y != toy)                                                                      \
      TEST_FAILED(fmt::format(                                                                     \
          "Failed to itransform {},{} to {},{}, got {},{} instead", fromx, fromy, tox, toy, x, y)) \
  };

void itransform()
{
  Fmi::Box box(0, 0, 1, 1, 100, 100);

  BOX_ITRANSFORM(0, 100, 0, 0);
  BOX_ITRANSFORM(0, 0, 0, 1);
  BOX_ITRANSFORM(100, 0, 1, 1);
  BOX_ITRANSFORM(25, 25, 0.25, 0.75);

  TEST_PASSED();
}

#define TEST_CLOSE(a, b) (std::abs(a - b) < 0.01)

void set_swap(OGRCoordinateTransformation& trans, bool& swap_input, bool& swap_output)
{
#if GDAL_VERSION_MAJOR < 2
  swap_input = false;
  swap_output = false;
#else
  bool src_latlong = trans.GetSourceCS()->EPSGTreatsAsLatLong();
  bool tgt_latlong = trans.GetTargetCS()->EPSGTreatsAsLatLong();
  bool src_ne = trans.GetSourceCS()->EPSGTreatsAsNorthingEasting();
  bool tgt_ne = trans.GetTargetCS()->EPSGTreatsAsNorthingEasting();

  swap_input = (src_latlong || src_ne);   // is input order swapped?
  swap_output = (tgt_latlong || tgt_ne);  // is output order swapped?

#if 0
      std::cout
      << fmt::format("\nSI:{} SO:{}   IN: {} {}  OUT: {} {}\n",
                     swap_input,
                     swap_output,
                     src_latlong,
                     src_ne,
                     tgt_latlong,
                     tgt_ne);
#endif
#endif
}

// ----------------------------------------------------------------------

void wgs84_and_epsg2393()
{
  std::unique_ptr<OGRSpatialReference> latlon(new OGRSpatialReference);
  auto err = latlon->SetFromUserInput("WGS84");
  if (err != OGRERR_NONE)
    TEST_FAILED("Failed to create WGS84 spatial reference");

  std::unique_ptr<OGRSpatialReference> epsg(new OGRSpatialReference);
  err = epsg->importFromEPSG(2393);
  if (err != OGRERR_NONE)
    TEST_FAILED("Failed to create EPSG:2393 spatial reference");

  std::unique_ptr<OGRCoordinateTransformation> transformation(
      OGRCreateCoordinateTransformation(latlon.get(), epsg.get()));
  if (transformation == nullptr)
    TEST_FAILED("Failed to create coordinate transformation");

  std::unique_ptr<OGRCoordinateTransformation> itransformation(
      OGRCreateCoordinateTransformation(epsg.get(), latlon.get()));
  if (itransformation == nullptr)
    TEST_FAILED("Failed to create inverse coordinate transformation");

  bool swap_input, swap_output;
  set_swap(*transformation, swap_input, swap_output);

  double x1 = 10;
  double y1 = 50;
  double x2 = 40;
  double y2 = 70;

  if (latlon->EPSGTreatsAsLatLong())
  {
    std::swap(x1, y1);
    std::swap(x2, y2);
  }

  if (transformation->Transform(1, &x1, &y1) == 0 || transformation->Transform(1, &x2, &y2) == 0)
    TEST_FAILED("Failed to create bbox");

  if (latlon->EPSGTreatsAsLatLong())
  {
    std::swap(x1, y1);
    std::swap(x2, y2);
  }

  // std::cout << fmt::format("\nBBOX = {} {} - {} {}\n", x1, y1, x2, y2);

  Fmi::Box box(x1, y1, x2, y2, 100, 200);

  {
    double x = 25;
    double y = 60;

    if (latlon->EPSGTreatsAsLatLong())
      std::swap(x, y);
    if (transformation->Transform(1, &x, &y) == 0)
      TEST_FAILED("Failed to project 25,6");
    if (latlon->EPSGTreatsAsLatLong())
      std::swap(x, y);

    // std::cout << fmt::format("PROJECTED = {} {}\n", x, y);
    box.transform(x, y);
    // std::cout << fmt::format("PIXEL = {} {}\n", x, y);

    if (!TEST_CLOSE(x, 64.6069) || !TEST_CLOSE(y, 108.93))
      TEST_FAILED(
          fmt::format("Failed to transform 25,60 to 64.6069,108.93, got {},{} instead", x, y));
  }

  {
    double x = 26;
    double y = 65;

    if (latlon->EPSGTreatsAsLatLong())
      std::swap(x, y);
    if (transformation->Transform(1, &x, &y) == 0)
      TEST_FAILED("Failed to project 26,65");
    if (latlon->EPSGTreatsAsLatLong())
      std::swap(x, y);
    box.transform(x, y);

    if (!TEST_CLOSE(x, 68.3769) || !TEST_CLOSE(y, 56.995))
      TEST_FAILED(
          fmt::format("Failed to transform 26,65 to 68.3769,56.995, got {},{} instead", x, y));
  }

  {
    double x = 64.6069;
    double y = 108.93;

    box.itransform(x, y);

    if (swap_output)
      std::swap(x, y);
    if (itransformation->Transform(1, &x, &y) == 0)
      TEST_FAILED("Failed to inverse project 25,60");
    if (swap_input)
      std::swap(x, y);

    if (!TEST_CLOSE(x, 25) || !TEST_CLOSE(y, 60))
      TEST_FAILED(
          fmt::format("Failed to transform 64.6069,108.93 to 25,60, got {},{} instead", x, y));
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void epsg4326_and_epsg2393()
{
  std::unique_ptr<OGRSpatialReference> latlon(new OGRSpatialReference);
  auto err = latlon->importFromEPSG(4326);
  if (err != OGRERR_NONE)
    TEST_FAILED("Failed to create EPSG:4326 spatial reference");

  std::unique_ptr<OGRSpatialReference> epsg(new OGRSpatialReference);
  err = epsg->importFromEPSG(2393);
  if (err != OGRERR_NONE)
    TEST_FAILED("Failed to create EPSG:2393 spatial reference");

  std::unique_ptr<OGRCoordinateTransformation> transformation(
      OGRCreateCoordinateTransformation(latlon.get(), epsg.get()));
  if (transformation == nullptr)
    TEST_FAILED("Failed to create coordinate transformation");

  bool swap_input, swap_output;
  set_swap(*transformation, swap_input, swap_output);

  double x1 = 10;
  double y1 = 50;
  double x2 = 40;
  double y2 = 70;

  if (latlon->EPSGTreatsAsLatLong())
  {
    std::swap(x1, y1);
    std::swap(x2, y2);
  }
  if (transformation->Transform(1, &x1, &y1) == 0 || transformation->Transform(1, &x2, &y2) == 0)
    TEST_FAILED("Failed to create bbox");
  if (latlon->EPSGTreatsAsLatLong())
  {
    std::swap(x1, y1);
    std::swap(x2, y2);
  }

  Fmi::Box box(x1, y1, x2, y2, 100, 200);

  {
    double x = 25;
    double y = 60;
    if (latlon->EPSGTreatsAsLatLong())
      std::swap(x, y);
    if (transformation->Transform(1, &x, &y) == 0)
      TEST_FAILED("Failed to project 25,6");
    if (latlon->EPSGTreatsAsLatLong())
      std::swap(x, y);
    box.transform(x, y);

    if (!TEST_CLOSE(x, 64.6069) || !TEST_CLOSE(y, 108.93))
      TEST_FAILED(
          fmt::format("Failed to transform 25,60 to 64.6069,108.93, got {},{} instead", x, y));
  }

  {
    double x = 26;
    double y = 65;
    if (latlon->EPSGTreatsAsLatLong())
      std::swap(x, y);
    if (transformation->Transform(1, &x, &y) == 0)
      TEST_FAILED("Failed to project 26,65");
    if (latlon->EPSGTreatsAsLatLong())
      std::swap(x, y);
    box.transform(x, y);

    if (!TEST_CLOSE(x, 68.3769) || !TEST_CLOSE(y, 56.995))
      TEST_FAILED(
          fmt::format("Failed to transform 26,65 to 68.3769,56.995, got {},{} instead", x, y));
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void epsga4326_and_epsg2393()
{
  std::unique_ptr<OGRSpatialReference> latlon(new OGRSpatialReference);
  auto err = latlon->importFromEPSGA(4326);
  if (err != OGRERR_NONE)
    TEST_FAILED("Failed to create EPSGA:4326 spatial reference");

  std::unique_ptr<OGRSpatialReference> epsg(new OGRSpatialReference);
  err = epsg->importFromEPSG(2393);
  if (err != OGRERR_NONE)
    TEST_FAILED("Failed to create EPSG:2393 spatial reference");

  std::unique_ptr<OGRCoordinateTransformation> transformation(
      OGRCreateCoordinateTransformation(latlon.get(), epsg.get()));
  if (transformation == nullptr)
    TEST_FAILED("Failed to create coordinate transformation");

  bool swap_input, swap_output;
  set_swap(*transformation, swap_input, swap_output);

  double x1 = 10;
  double y1 = 50;
  double x2 = 40;
  double y2 = 70;
  if (latlon->EPSGTreatsAsLatLong())
  {
    std::swap(x1, y1);
    std::swap(x2, y2);
  }
  if (transformation->Transform(1, &x1, &y1) == 0 || transformation->Transform(1, &x2, &y2) == 0)
    TEST_FAILED("Failed to create bbox");
  if (latlon->EPSGTreatsAsLatLong())
  {
    std::swap(x1, y1);
    std::swap(x2, y2);
  }

  Fmi::Box box(x1, y1, x2, y2, 100, 200);

  {
    double x = 25;
    double y = 60;
    if (latlon->EPSGTreatsAsLatLong())
      std::swap(x, y);
    if (transformation->Transform(1, &x, &y) == 0)
      TEST_FAILED("Failed to project 25,6");
    if (latlon->EPSGTreatsAsLatLong())
      std::swap(x, y);
    box.transform(x, y);

    if (!TEST_CLOSE(x, 64.6069) || !TEST_CLOSE(y, 108.93))
      TEST_FAILED(
          fmt::format("Failed to transform 25,60 to 64.6069,108.93, got {},{} instead", x, y));
  }

  {
    double x = 26;
    double y = 65;
    if (latlon->EPSGTreatsAsLatLong())
      std::swap(x, y);
    if (transformation->Transform(1, &x, &y) == 0)
      TEST_FAILED("Failed to project 26,65");
    if (latlon->EPSGTreatsAsLatLong())
      std::swap(x, y);
    box.transform(x, y);

    if (!TEST_CLOSE(x, 68.3769) || !TEST_CLOSE(y, 56.995))
      TEST_FAILED(
          fmt::format("Failed to transform 26,65 to 68.3769,56.995, got {},{} instead", x, y));
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void epsga4326_and_epsga2393()
{
  std::unique_ptr<OGRSpatialReference> latlon(new OGRSpatialReference);
  auto err = latlon->SetFromUserInput("EPSGA:4326");
  if (err != OGRERR_NONE)
    TEST_FAILED("Failed to create EPSGA:4326 spatial reference");

  std::unique_ptr<OGRSpatialReference> epsg(new OGRSpatialReference);
  err = epsg->SetFromUserInput("EPSGA:2393");
  if (err != OGRERR_NONE)
    TEST_FAILED("Failed to create EPSGA:2393 spatial reference");

  std::unique_ptr<OGRCoordinateTransformation> transformation(
      OGRCreateCoordinateTransformation(latlon.get(), epsg.get()));
  if (transformation == nullptr)
    TEST_FAILED("Failed to create coordinate transformation");

  bool swap_input, swap_output;
  set_swap(*transformation, swap_input, swap_output);

  double x1 = 10;
  double y1 = 50;
  double x2 = 40;
  double y2 = 70;
  if (latlon->EPSGTreatsAsLatLong())
  {
    std::swap(x1, y1);
    std::swap(x2, y2);
  }
  if (transformation->Transform(1, &x1, &y1) == 0 || transformation->Transform(1, &x2, &y2) == 0)
    TEST_FAILED("Failed to create bbox");
  if (latlon->EPSGTreatsAsLatLong())
  {
    std::swap(x1, y1);
    std::swap(x2, y2);
  }

  Fmi::Box box(x1, y1, x2, y2, 100, 200);

  {
    double x = 25;
    double y = 60;
    if (latlon->EPSGTreatsAsLatLong())
      std::swap(x, y);
    if (transformation->Transform(1, &x, &y) == 0)
      TEST_FAILED("Failed to project 25,6");
    if (latlon->EPSGTreatsAsLatLong())
      std::swap(x, y);
    box.transform(x, y);

    if (!TEST_CLOSE(x, 64.6069) || !TEST_CLOSE(y, 108.93))
      TEST_FAILED(
          fmt::format("Failed to transform 25,60 to 64.6069,108.93, got {},{} instead", x, y));
  }

  {
    double x = 26;
    double y = 65;
    if (latlon->EPSGTreatsAsLatLong())
      std::swap(x, y);
    if (transformation->Transform(1, &x, &y) == 0)
      TEST_FAILED("Failed to project 26,65");
    if (latlon->EPSGTreatsAsLatLong())
      std::swap(x, y);
    box.transform(x, y);

    if (!TEST_CLOSE(x, 68.3769) || !TEST_CLOSE(y, 56.995))
      TEST_FAILED(
          fmt::format("Failed to transform 26,65 to 68.3769,56.995, got {},{} instead", x, y));
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void wgs84_and_stere()
{
  std::unique_ptr<OGRSpatialReference> latlon(new OGRSpatialReference);
  auto err = latlon->SetFromUserInput("WGS84");
  if (err != OGRERR_NONE)
    TEST_FAILED("Failed to create WGS84 spatial reference");

  std::unique_ptr<OGRSpatialReference> epsg(new OGRSpatialReference);
  err = epsg->SetFromUserInput(
      "+proj=stere +lat_0=90 +lat_ts=60 +lon_0=20 +k=1 +x_0=0 +y_0=0 +a=6371220 +b=6371220 "
      "+units=m +no_defs");
  if (err != OGRERR_NONE)
    TEST_FAILED("Failed to create stere spatial reference");

  std::unique_ptr<OGRCoordinateTransformation> transformation(
      OGRCreateCoordinateTransformation(latlon.get(), epsg.get()));
  if (transformation == nullptr)
    TEST_FAILED("Failed to create coordinate transformation");

  bool swap_input, swap_output;
  set_swap(*transformation, swap_input, swap_output);

  double x1 = 10;
  double y1 = 50;
  double x2 = 40;
  double y2 = 70;

  if (swap_input)
  {
    std::swap(x1, y1);
    std::swap(x2, y2);
  }
  if (transformation->Transform(1, &x1, &y1) == 0 || transformation->Transform(1, &x2, &y2) == 0)
    TEST_FAILED("Failed to create bbox");
  if (swap_output)
  {
    std::swap(x1, y1);
    std::swap(x2, y2);
  }

  Fmi::Box box(x1, y1, x2, y2, 100, 200);

  {
    double x = 25;
    double y = 60;
    if (swap_input)
      std::swap(x, y);
    if (transformation->Transform(1, &x, &y) == 0)
      TEST_FAILED("Failed to project 25,60");
    if (swap_output)
      std::swap(x, y);
    // std::cout << fmt::format("PROJ: {} {}\n", x, y);
    box.transform(x, y);

    if (!TEST_CLOSE(x, 70.0801) || !TEST_CLOSE(y, 105.046))
      TEST_FAILED(
          fmt::format("Failed to transform 25,60 to 70.0801,105.046, got {},{} instead", x, y));
  }

  {
    double x = 26;
    double y = 65;
    if (swap_input)
      std::swap(x, y);
    if (transformation->Transform(1, &x, &y) == 0)
      TEST_FAILED("Failed to project 26,65");
    if (swap_output)
      std::swap(x, y);
    box.transform(x, y);

    if (!TEST_CLOSE(x, 69.9345) || !TEST_CLOSE(y, 56.8485))
      TEST_FAILED(
          fmt::format("Failed to transform 26,65 to 69.9345,56.8485, got {},{} instead", x, y));
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void sphere_and_epsg2393()
{
  std::unique_ptr<OGRSpatialReference> latlon(new OGRSpatialReference);
  auto err = latlon->SetFromUserInput("+proj=longlat +R=6731229 +no_defs +towgs84=0,0,0");
  if (err != OGRERR_NONE)
    TEST_FAILED("Failed to create spherical spatial reference");

  std::unique_ptr<OGRSpatialReference> epsg(new OGRSpatialReference);
  err = epsg->importFromEPSG(2393);
  if (err != OGRERR_NONE)
    TEST_FAILED("Failed to create EPSG:2393 spatial reference");

  std::unique_ptr<OGRCoordinateTransformation> transformation(
      OGRCreateCoordinateTransformation(latlon.get(), epsg.get()));
  if (transformation == nullptr)
    TEST_FAILED("Failed to create coordinate transformation");

  std::unique_ptr<OGRCoordinateTransformation> itransformation(
      OGRCreateCoordinateTransformation(epsg.get(), latlon.get()));
  if (itransformation == nullptr)
    TEST_FAILED("Failed to create inverse coordinate transformation");

  bool swap_input, swap_output;
  set_swap(*transformation, swap_input, swap_output);

  double x1 = 10;
  double y1 = 50;
  double x2 = 40;
  double y2 = 70;

  if (swap_input)
  {
    std::swap(x1, y1);
    std::swap(x2, y2);
  }

  if (transformation->Transform(1, &x1, &y1) == 0 || transformation->Transform(1, &x2, &y2) == 0)
    TEST_FAILED("Failed to create bbox");

  // std::cout << fmt::format("BBOX: {} {} {} {}\n", x1, y1, x2, y2);

  if (swap_output)
  {
    std::swap(x1, y1);
    std::swap(x2, y2);
  }

  Fmi::Box box(x1, y1, x2, y2, 100, 200);

  {
    double x = 25;
    double y = 60;

    if (swap_input)
      std::swap(x, y);
    if (transformation->Transform(1, &x, &y) == 0)
      TEST_FAILED("Failed to project 25,60");
    if (swap_output)
      std::swap(x, y);

    // std::cout << fmt::format("PROJ: {} {}\n", x, y);

    box.transform(x, y);

    if (!TEST_CLOSE(x, 64.6468) || !TEST_CLOSE(y, 108.84))
      TEST_FAILED(
          fmt::format("Failed to transform 25,60 to 64.6468,108.84, got {},{} instead", x, y));
  }

  {
    double x = 26;
    double y = 65;

    if (swap_input)
      std::swap(x, y);
    if (transformation->Transform(1, &x, &y) == 0)
      TEST_FAILED("Failed to project 26,65");
    if (swap_output)
      std::swap(x, y);
    box.transform(x, y);

    if (!TEST_CLOSE(x, 68.4165) || !TEST_CLOSE(y, 56.9242))
      TEST_FAILED(
          fmt::format("Failed to transform 26,65 to 68.4165,56.9242, got {},{} instead", x, y));
  }

  {
    double x = 64.6468;
    double y = 108.84;

    box.itransform(x, y);

    if (swap_output)
      std::swap(x, y);
    if (itransformation->Transform(1, &x, &y) == 0)
      TEST_FAILED("Failed to inverse project 25,60");
    if (swap_input)
      std::swap(x, y);

    if (!TEST_CLOSE(x, 25) || !TEST_CLOSE(y, 60))
      TEST_FAILED(
          fmt::format("Failed to transform 64.6468,108.84 to 25,60, got {},{} instead", x, y));
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void sphere_and_stere()
{
  std::unique_ptr<OGRSpatialReference> latlon(new OGRSpatialReference);
  auto err = latlon->SetFromUserInput("+proj=longlat +R=6371229 +no_defs +towgs84=0,0,0");
  if (err != OGRERR_NONE)
    TEST_FAILED("Failed to create spherical spatial reference");

  std::unique_ptr<OGRSpatialReference> epsg(new OGRSpatialReference);
  err = epsg->SetFromUserInput(
      "+proj=stere +lat_0=90 +lat_ts=60 +lon_0=20 +k=1 +x_0=0 +y_0=0 +a=6371220 +b=6371220 "
      "+units=m +no_defs");
  if (err != OGRERR_NONE)
    TEST_FAILED("Failed to create stere spatial reference");

  std::unique_ptr<OGRCoordinateTransformation> transformation(
      OGRCreateCoordinateTransformation(latlon.get(), epsg.get()));
  if (transformation == nullptr)
    TEST_FAILED("Failed to create coordinate transformation");

  bool swap_input, swap_output;
  set_swap(*transformation, swap_input, swap_output);

  double x1 = 10;
  double y1 = 50;
  double x2 = 40;
  double y2 = 70;

  if (swap_input)
  {
    std::swap(x1, y1);
    std::swap(x2, y2);
  }
  if (transformation->Transform(1, &x1, &y1) == 0 || transformation->Transform(1, &x2, &y2) == 0)
    TEST_FAILED("Failed to create bbox");
  if (swap_output)
  {
    std::swap(x1, y1);
    std::swap(x2, y2);
  }

  Fmi::Box box(x1, y1, x2, y2, 100, 200);

  {
    double x = 25;
    double y = 60;
    if (swap_input)
      std::swap(x, y);
    if (transformation->Transform(1, &x, &y) == 0)
      TEST_FAILED("Failed to project 25,60");
    if (swap_output)
      std::swap(x, y);
    box.transform(x, y);

    if (!TEST_CLOSE(x, 70.0801) || !TEST_CLOSE(y, 105.046))
      TEST_FAILED(
          fmt::format("Failed to transform 25,60 to 70.0801,105.046, got {},{} instead", x, y));
  }

  {
    double x = 26;
    double y = 65;
    if (swap_input)
      std::swap(x, y);
    if (transformation->Transform(1, &x, &y) == 0)
      TEST_FAILED("Failed to project 26,65");
    if (swap_output)
      std::swap(x, y);
    box.transform(x, y);

    if (!TEST_CLOSE(x, 69.9345) || !TEST_CLOSE(y, 56.8485))
      TEST_FAILED(
          fmt::format("Failed to transform 26,65 to 69.9345,56.8485, got {},{} instead", x, y));
  }

  TEST_PASSED();
}

// Test driver
class tests : public tframe::tests
{
  // Overridden message separator
  virtual const char* error_message_prefix() const { return "\n\t"; }
  // Main test suite
  void test()
  {
    TEST(transform);
    TEST(itransform);

    TEST(wgs84_and_epsg2393);
    TEST(epsg4326_and_epsg2393);
    TEST(epsga4326_and_epsg2393);
    TEST(epsga4326_and_epsga2393);
    TEST(wgs84_and_stere);
    TEST(sphere_and_epsg2393);
    TEST(sphere_and_stere);
  }

};  // class tests

}  // namespace Tests

int main(void)
{
  cout << endl << "Box tester" << endl << "==========" << endl;
  Tests::tests t;
  return t.run();
}
