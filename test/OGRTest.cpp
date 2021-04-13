#include "Box.h"
#include "CoordinateTransformation.h"
#include "OGR.h"
#include "SpatialReference.h"
#include "TestDefs.h"
#include <regression/tframe.h>
#include <memory>
#include <ogr_geometry.h>

using namespace std;

const int precision = 2;

namespace Tests
{
// ----------------------------------------------------------------------

void exportToWkt_spatialreference()
{
  std::unique_ptr<OGRSpatialReference> srs(new OGRSpatialReference);
  OGRErr err = srs->SetFromUserInput("WGS84");
  if (err != OGRERR_NONE)
    TEST_FAILED("Failed to create spatial reference WGS84");

  std::string result = Fmi::OGR::exportToWkt(*srs);

  if (result.substr(0, 15) != "GEOGCS[\"WGS 84\"")
    TEST_FAILED("Failed to export WGS84 spatial reference as WKT");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void exportToProj()
{
  {
    std::unique_ptr<OGRSpatialReference> srs(new OGRSpatialReference);
    OGRErr err = srs->SetFromUserInput("WGS84");
    if (err != OGRERR_NONE)
      TEST_FAILED("Failed to create spatial reference WGS84");

    std::string result = Fmi::OGR::exportToProj(*srs);

    if (result != "+proj=longlat +datum=WGS84 +no_defs")
      TEST_FAILED("Failed to export WGS84 spatial reference as PROJ, got '" + result + "'");
  }

  {
    std::unique_ptr<OGRSpatialReference> srs(new OGRSpatialReference);
    OGRErr err = srs->SetFromUserInput("+proj=latlong +datum=WGS84");
    if (err != OGRERR_NONE)
      TEST_FAILED("Failed to create spatial reference +proj=latlong +datum=WGS84");

    std::string result = Fmi::OGR::exportToProj(*srs);

    if (result != "+proj=longlat +datum=WGS84 +no_defs")
      TEST_FAILED("Failed to export +proj=latlong +datum=WGS84 spatial reference as PROJ, got '" +
                  result + "'");
  }

  {
    std::unique_ptr<OGRSpatialReference> srs(new OGRSpatialReference);
    OGRErr err = srs->SetFromUserInput("EPSG:4326");
    if (err != OGRERR_NONE)
      TEST_FAILED("Failed to create spatial reference +init=epsg:4326");

    std::string result = Fmi::OGR::exportToProj(*srs);

    // Result seems to depend on PROJ.4 version
    if (result != "+proj=longlat +datum=WGS84 +no_defs" &&
        result != "+proj=longlat +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +no_defs")
      TEST_FAILED("Failed to export +init=epsg:4326 spatial reference as PROJ, got '" + result +
                  "'");
  }

  {
    std::unique_ptr<OGRSpatialReference> srs(new OGRSpatialReference);
    OGRErr err = srs->SetFromUserInput("EPSG:4326");
    if (err != OGRERR_NONE)
      TEST_FAILED("Failed to create spatial reference EPSG:4326");

    std::string result = Fmi::OGR::exportToProj(*srs);

    // Second construction
    err = srs->SetFromUserInput(result.c_str());
    if (err != OGRERR_NONE)
      TEST_FAILED("Failed to create secondary spatial reference from EPSG:4326");

    result = Fmi::OGR::exportToProj(*srs);

    if (result != "+proj=longlat +datum=WGS84 +no_defs")
      TEST_FAILED("Failed to export +init=epsg:4326 spatial reference as PROJ, got '" + result +
                  "'");
  }

  {
    std::unique_ptr<OGRSpatialReference> srs(new OGRSpatialReference);
    OGRErr err = srs->SetFromUserInput("EPSG:2393");
    if (err != OGRERR_NONE)
      TEST_FAILED("Failed to create spatial reference EPSG:2393");

    std::string result = Fmi::OGR::exportToProj(*srs);

    if (result !=
        "+proj=tmerc +lat_0=0 +lon_0=27 +k=1 +x_0=3500000 +y_0=0 +ellps=intl "
        "+towgs84=-96.062,-82.428,-121.753,4.801,0.345,-1.376,1.496 +units=m +no_defs")
      TEST_FAILED("Failed to export EPSG:2393 spatial reference as PROJ, got '" + result + "'");
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void expand_geometry()
{
  // point Helsinki
  const char* point = "POINT (24.9459 60.1921)";
  const char* linestring = "LINESTRING (24.9459 60.1921, 24.9859 60.2921)";

  OGRGeometry* input_geom;
  OGRGeometry* output_geom;

  OGRSpatialReference srs;
  srs.importFromEPSGA(4326);

  const auto box = Fmi::Box::identity();  // identity transformation
  int precision = 6;

  OGRGeometryFactory::createFromWkt(point, &srs, &input_geom);
  // circle around helsinki coordinates  with 1 km radius
  output_geom = Fmi::OGR::expandGeometry(input_geom, 1000);

  std::string result = Fmi::OGR::exportToSvg(*output_geom, box, precision);
  // OGRGeometryFactory::destroyGeometry(output_geom); needed in the next test
  OGRGeometryFactory::destroyGeometry(input_geom);

  std::string ok =
      "M24.963866 60.192100 24.963811 60.191398 24.963645 60.190701 24.963370 60.190012 24.962987 "
      "60.189335 24.962499 60.188676 24.961908 60.188038 24.961219 60.187425 24.960435 60.186841 "
      "24.959562 60.186290 24.958604 60.185774 24.957568 60.185297 24.956460 60.184862 24.955287 "
      "60.184472 24.954057 60.184128 24.952775 60.183834 24.951452 60.183591 24.950094 60.183400 "
      "24.948711 60.183263 24.947310 60.183181 24.945900 60.183153 24.944490 60.183181 24.943089 "
      "60.183263 24.941706 60.183400 24.940348 60.183591 24.939025 60.183834 24.937743 60.184128 "
      "24.936513 60.184472 24.935340 60.184862 24.934232 60.185297 24.933196 60.185774 24.932238 "
      "60.186290 24.931365 60.186841 24.930581 60.187425 24.929892 60.188038 24.929301 60.188676 "
      "24.928813 60.189335 24.928430 60.190012 24.928155 60.190701 24.927989 60.191398 24.927934 "
      "60.192100 24.927989 60.192802 24.928155 60.193499 24.928430 60.194188 24.928813 60.194864 "
      "24.929301 60.195523 24.929892 60.196161 24.930581 60.196774 24.931365 60.197358 24.932238 "
      "60.197909 24.933196 60.198425 24.934232 60.198902 24.935340 60.199337 24.936513 60.199727 "
      "24.937743 60.200070 24.939025 60.200364 24.940348 60.200607 24.941706 60.200797 24.943089 "
      "60.200934 24.944490 60.201017 24.945900 60.201045 24.947310 60.201017 24.948711 60.200934 "
      "24.950094 60.200797 24.951452 60.200607 24.952775 60.200364 24.954057 60.200070 24.955287 "
      "60.199727 24.956460 60.199337 24.957568 60.198902 24.958604 60.198425 24.959562 60.197909 "
      "24.960435 60.197358 24.961219 60.196774 24.961908 60.196161 24.962499 60.195523 24.962987 "
      "60.194864 24.963370 60.194188 24.963645 60.193499 24.963811 60.192802Z";

  if (result != ok)
    TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);

  // expand circle another 1 km
  OGRGeometry* output_geom2 = Fmi::OGR::expandGeometry(output_geom, 1000);

  result = Fmi::OGR::exportToSvg(*output_geom2, box, precision);
  OGRGeometryFactory::destroyGeometry(output_geom);
  OGRGeometryFactory::destroyGeometry(output_geom2);

  ok = "M24.981819 60.192451 24.981819 60.191749 24.981763 60.191047 24.981653 60.190347 24.981487 "
       "60.189649 24.981266 60.188955 24.980991 60.188266 24.980662 60.187583 24.980279 60.186907 "
       "24.979843 60.186239 24.979355 60.185580 24.978815 60.184931 24.978224 60.184293 24.977584 "
       "60.183666 24.976894 60.183053 24.976157 60.182454 24.975373 60.181870 24.974544 60.181302 "
       "24.973671 60.180750 24.972755 60.180216 24.971797 60.179700 24.970800 60.179203 24.969764 "
       "60.178726 24.968691 60.178269 24.967583 60.177834 24.966442 60.177421 24.965269 60.177031 "
       "24.964066 60.176664 24.962835 60.176320 24.961578 60.176001 24.960297 60.175707 24.958994 "
       "60.175438 24.957670 60.175195 24.956329 60.174978 24.954971 60.174787 24.953599 60.174623 "
       "24.952216 60.174486 24.950822 60.174376 24.949421 60.174293 24.948015 60.174238 24.946605 "
       "60.174210 24.945195 60.174210 24.943785 60.174238 24.942379 60.174293 24.940978 60.174376 "
       "24.939584 60.174486 24.938201 60.174623 24.936829 60.174787 24.935471 60.174978 24.934130 "
       "60.175195 24.932806 60.175438 24.931503 60.175707 24.930222 60.176001 24.928965 60.176320 "
       "24.927734 60.176664 24.926531 60.177031 24.925358 60.177421 24.924217 60.177834 24.923109 "
       "60.178269 24.922036 60.178726 24.921000 60.179203 24.920003 60.179700 24.919045 60.180216 "
       "24.918129 60.180750 24.917256 60.181302 24.916427 60.181870 24.915643 60.182454 24.914906 "
       "60.183053 24.914216 60.183666 24.913576 60.184293 24.912985 60.184931 24.912445 60.185580 "
       "24.911957 60.186239 24.911521 60.186907 24.911138 60.187583 24.910809 60.188266 24.910534 "
       "60.188955 24.910313 60.189649 24.910147 60.190347 24.910037 60.191047 24.909981 60.191749 "
       "24.909981 60.192451 24.910037 60.193153 24.910147 60.193853 24.910313 60.194551 24.910534 "
       "60.195245 24.910809 60.195933 24.911138 60.196616 24.911521 60.197292 24.911957 60.197960 "
       "24.912445 60.198619 24.912985 60.199268 24.913576 60.199906 24.914216 60.200531 24.914906 "
       "60.201144 24.915643 60.201743 24.916427 60.202327 24.917256 60.202895 24.918129 60.203446 "
       "24.919045 60.203980 24.920003 60.204496 24.921000 60.204992 24.922036 60.205469 24.923109 "
       "60.205925 24.924217 60.206359 24.925358 60.206772 24.926531 60.207162 24.927734 60.207529 "
       "24.928965 60.207872 24.930222 60.208191 24.931503 60.208485 24.932806 60.208753 24.934130 "
       "60.208996 24.935471 60.209213 24.936829 60.209404 24.938201 60.209568 24.939584 60.209705 "
       "24.940978 60.209815 24.942379 60.209897 24.943785 60.209952 24.945195 60.209980 24.946605 "
       "60.209980 24.948015 60.209952 24.949421 60.209897 24.950822 60.209815 24.952216 60.209705 "
       "24.953599 60.209568 24.954971 60.209404 24.956329 60.209213 24.957670 60.208996 24.958994 "
       "60.208753 24.960297 60.208485 24.961578 60.208191 24.962835 60.207872 24.964066 60.207529 "
       "24.965269 60.207162 24.966442 60.206772 24.967583 60.206359 24.968691 60.205925 24.969764 "
       "60.205469 24.970800 60.204992 24.971797 60.204496 24.972755 60.203980 24.973671 60.203446 "
       "24.974544 60.202895 24.975373 60.202327 24.976157 60.201743 24.976894 60.201144 24.977584 "
       "60.200531 24.978224 60.199906 24.978815 60.199268 24.979355 60.198619 24.979843 60.197960 "
       "24.980279 60.197292 24.980662 60.196616 24.980991 60.195933 24.981266 60.195245 24.981487 "
       "60.194551 24.981653 60.193853 24.981763 60.193153Z";

  if (result != ok)
    TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);

  OGRGeometryFactory::createFromWkt(&linestring, &srs, &input_geom);

  // expand line by 1 km
  output_geom = Fmi::OGR::expandGeometry(input_geom, 1000);

  result = Fmi::OGR::exportToSvg(*output_geom, box, precision);

  OGRGeometryFactory::destroyGeometry(output_geom);
  OGRGeometryFactory::destroyGeometry(input_geom);

  ok = "M24.977089 60.292970 24.977254 60.293310 24.977472 60.293643 24.977742 60.293967 24.978062 "
       "60.294279 24.978431 60.294577 24.978845 60.294860 24.979303 60.295127 24.979802 60.295374 "
       "24.980338 60.295602 24.980909 60.295807 24.981510 60.295990 24.982139 60.296149 24.982790 "
       "60.296283 24.983461 60.296391 24.984147 60.296473 24.984844 60.296528 24.985547 60.296555 "
       "24.986252 60.296555 24.986955 60.296528 24.987652 60.296473 24.988338 60.296392 24.989009 "
       "60.296283 24.989661 60.296149 24.990289 60.295991 24.990890 60.295808 24.991461 60.295602 "
       "24.991997 60.295374 24.992496 60.295127 24.992954 60.294861 24.993369 60.294577 24.993738 "
       "60.294279 24.994058 60.293967 24.994328 60.293644 24.994546 60.293311 24.994710 60.292970 "
       "24.994821 60.292624 24.994876 60.292275 24.994876 60.291925 24.994821 60.291576 24.994711 "
       "60.291230 24.954711 60.191228 24.954546 60.190886 24.954328 60.190552 24.954058 60.190228 "
       "24.953738 60.189915 24.953369 60.189615 24.952955 60.189331 24.952497 60.189064 24.951998 "
       "60.188815 24.951462 60.188587 24.950891 60.188381 24.950290 60.188197 24.949661 60.188038 "
       "24.949010 60.187903 24.948339 60.187795 24.947653 60.187713 24.946956 60.187658 24.946253 "
       "60.187630 24.945548 60.187630 24.944845 60.187658 24.944148 60.187713 24.943462 60.187795 "
       "24.942791 60.187903 24.942139 60.188038 24.941511 60.188197 24.940910 60.188381 24.940339 "
       "60.188587 24.939803 60.188815 24.939304 60.189063 24.938846 60.189331 24.938431 60.189615 "
       "24.938062 60.189914 24.937742 60.190227 24.937472 60.190552 24.937254 60.190886 24.937090 "
       "60.191227 24.936979 60.191574 24.936924 60.191924 24.936924 60.192275 24.936979 60.192626 "
       "24.937089 60.192972Z";

  if (result != ok)
    TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void exportToSvg_wiki_examples()
{
  using Fmi::Box;
  using Fmi::OGR::exportToSvg;

  // Ref: http://en.wikipedia.org/wiki/Well-known_text

  const char* point = "POINT (30 10)";
  const char* linestring = "LINESTRING (30 10, 10 30, 40 40)";
  const char* polygon1 = "POLYGON ((30 10, 40 40, 20 40, 10 20, 30 10))";
  const char* polygon2 =
      "POLYGON ((35 10, 45 45, 15 40, 10 20, 35 10),(20 30, 35 35, 30 20, 20 30))";
  const char* multipoint1 = "MULTIPOINT ((10 40), (40 30), (20 20), (30 10))";
  const char* multipoint2 = "MULTIPOINT (10 40, 40 30, 20 20, 30 10)";
  const char* multilinestring =
      "MULTILINESTRING ((10 10, 20 20, 10 40),(40 40, 30 30, 40 20, 30 10))";
  const char* multipolygon1 =
      "MULTIPOLYGON (((30 20, 45 40, 10 40, 30 20)),((15 5, 40 10, 10 20, 5 10, 15 5)))";
  const char* multipolygon2 =
      "MULTIPOLYGON (((40 40, 20 45, 45 30, 40 40)),((20 35, 10 30, 10 10, 30 5, 45 20, 20 35),(30 "
      "20, 20 15, 20 25, 30 20)))";

  Box box = Box::identity();  // identity transformation

  OGRGeometry* geom;
  {
    OGRGeometryFactory::createFromWkt(point, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M30 10";
    if (result != ok)
      TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(linestring, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M30 10 10 30 40 40";
    if (result != ok)
      TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(polygon1, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M30 10 40 40 20 40 10 20Z";
    if (result != ok)
      TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(polygon2, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M35 10 45 45 15 40 10 20ZM20 30 35 35 30 20Z";
    if (result != ok)
      TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(multipoint1, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M10 40M40 30M20 20M30 10";
    if (result != ok)
      TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(multipoint2, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M10 40M40 30M20 20M30 10";
    if (result != ok)
      TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(multilinestring, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M10 10 20 20 10 40M40 40 30 30 40 20 30 10";
    if (result != ok)
      TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(multipolygon1, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M30 20 45 40 10 40ZM15 5 40 10 10 20 5 10Z";
    if (result != ok)
      TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

#if 0
	// Note: Using multipolygon2 twice will crash
	{
	  OGRGeometryFactory::createFromWkt(multipolygon2,NULL,&geom);
	  string result = exportToSvg(*geom, box, precision);
	  OGRGeometryFactory::destroyGeometry(geom);
	  string ok = "M40 40 20 45 45 30ZM20 35 10 30 10 10 30 5 45 20ZM30 20 20 15 20 25Z";
	  if(result != ok)
		TEST_FAILED("Expected: "+ok+"\n\tObtained: "+result);
	}
#endif

  // Finally test integer precision too
  {
    OGRGeometryFactory::createFromWkt(multipolygon2, NULL, &geom);
    string result = exportToSvg(*geom, box, 0);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M40 40 20 45 45 30ZM20 35 10 30 10 10 30 5 45 20ZM30 20 20 15 20 25Z";
    if (result != ok)
      TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void exportToSvg_precision()
{
  using namespace Fmi;
  using Fmi::Box;
  using OGR::exportToSvg;

  // Create a new linestring with incremental coordinates

  OGRLineString* line = new OGRLineString;
  for (int i = -10; i < 100; i += 2)
  {
    double x = i / 100.0;
    double y = (i + 1) / 100.0;
    line->addPoint(x, y);
  }

  try
  {
    {
      string ok =
          "M-0.10 -0.09 -0.08 -0.07 -0.06 -0.05 -0.04 -0.03 -0.02 -0.01 0 0.01 0.02 0.03 0.04 0.05 "
          "0.06 0.07 0.08 0.09 0.10 0.11 0.12 0.13 0.14 0.15 0.16 0.17 0.18 0.19 0.20 0.21 0.22 "
          "0.23 "
          "0.24 0.25 0.26 0.27 0.28 0.29 0.30 0.31 0.32 0.33 0.34 0.35 0.36 0.37 0.38 0.39 0.40 "
          "0.41 "
          "0.42 0.43 0.44 0.45 0.46 0.47 0.48 0.49 0.50 0.51 0.52 0.53 0.54 0.55 0.56 0.57 0.58 "
          "0.59 "
          "0.60 0.61 0.62 0.63 0.64 0.65 0.66 0.67 0.68 0.69 0.70 0.71 0.72 0.73 0.74 0.75 0.76 "
          "0.77 "
          "0.78 0.79 0.80 0.81 0.82 0.83 0.84 0.85 0.86 0.87 0.88 0.89 0.90 0.91 0.92 0.93 0.94 "
          "0.95 "
          "0.96 0.97 0.98 0.99";
      string result = exportToSvg(*line, Box::identity(), 2.0);
      if (result != ok)
        TEST_FAILED("Precision 2:\n\tExpected: " + ok + "\n\tObtained: " + result);
    }

    {
      string ok =
          "M-0.1 -0.1 0 0 0 0.1 0.1 0.1 0.1 0.2 0.2 0.2 0.2 0.3 0.3 0.3 0.3 0.4 0.4 0.4 0.4 0.5 "
          "0.5 0.5 0.5 0.6 0.6 0.6 0.6 0.7 0.7 0.7 0.7 0.8 0.8 0.8 0.8 0.9 0.9 0.9 0.9 1 1 1";
      string result = exportToSvg(*line, Box::identity(), 1.0);
      if (result != ok)
        TEST_FAILED("Precision 1:\n\tExpected: " + ok + "\n\tObtained: " + result);
    }

    {
      string ok = "M-0.2 0 0 0 0.2 0.2 0.4 0.4 0.6 0.6 0.8 0.8 1 1";
      string result = exportToSvg(*line, Box::identity(), 0.7);
      if (result != ok)
        TEST_FAILED("Precision 0.7:\n\tExpected: " + ok + "\n\tObtained: " + result);
    }

    {
      string ok = "M0 0 0.3 0.3 0.6 0.6 0.9 0.9";
      string result = exportToSvg(*line, Box::identity(), 0.5);
      if (result != ok)
        TEST_FAILED("Precision 0.5:\n\tExpected: " + ok + "\n\tObtained: " + result);
    }

    {
      string ok = "M0 0 0.8 0.8";
      string result = exportToSvg(*line, Box::identity(), 0.1);
      if (result != ok)
        TEST_FAILED("Precision 0.1:\n\tExpected: " + ok + "\n\tObtained: " + result);
    }
  }
  catch (...)
  {
    OGRGeometryFactory::destroyGeometry(line);
    throw;
  }

  OGRGeometryFactory::destroyGeometry(line);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void lineclip()
{
  using namespace Fmi;
  using Fmi::Box;
  using OGR::exportToWkt;

  Box box(0, 0, 10, 10, 10, 10);  // 0,0-->10,10 with irrelevant transformation sizes

  int ntests = 75;

  char* mytests[75][2] = {
      // inside
      {"LINESTRING (1 1,1 9,9 9,9 1)", "LINESTRING (1 1,1 9,9 9,9 1)"},
      // outside
      {"LINESTRING (-1 -9,-1 11,9 11)", "GEOMETRYCOLLECTION EMPTY"},
      // go in from left
      {"LINESTRING (-1 5,5 5,9 9)", "LINESTRING (0 5,5 5,9 9)"},
      // go out from right
      {"LINESTRING (5 5,8 5,12 5)", "LINESTRING (5 5,8 5,10 5)"},
      // go in and out
      {"LINESTRING (5 -1,5 5,1 2,-3 2,1 6)", "MULTILINESTRING ((5 0,5 5,1 2,0 2),(0 5,1 6))"},
      // go along left edge
      {"LINESTRING (0 3,0 5,0 7)", "GEOMETRYCOLLECTION EMPTY"},
      // go out from left edge
      {"LINESTRING (0 3,0 5,-1 7)", "GEOMETRYCOLLECTION EMPTY"},
      // go in from left edge
      {"LINESTRING (0 3,0 5,2 7)", "LINESTRING (0 5,2 7)"},
      // triangle corner at bottom left corner
      {"LINESTRING (2 1,0 0,1 2)", "LINESTRING (2 1,0 0,1 2)"},
      // go from in to edge and back in
      {"LINESTRING (3 3,0 3,0 5,2 7)", "MULTILINESTRING ((3 3,0 3),(0 5,2 7))"},
      // go from in to edge and then straight out
      {"LINESTRING (5 5,10 5,20 5)", "LINESTRING (5 5,10 5)"},
      // triangle corner at left edge
      {"LINESTRING (3 3,0 6,3 9)", "LINESTRING (3 3,0 6,3 9)"},

      // polygon completely inside
      {"POLYGON ((5 5,5 6,6 6,6 5,5 5))", "POLYGON ((5 5,5 6,6 6,6 5,5 5))"},
      // polygon completely outside
      {"POLYGON ((15 15,15 16,16 16,16 15,15 15))", "GEOMETRYCOLLECTION EMPTY"},
      // polygon surrounds the rectangle
      {"POLYGON ((-1 -1,-1 11,11 11,11 -1,-1 -1))", "GEOMETRYCOLLECTION EMPTY"},
      // polygon cuts the rectangle
      {"POLYGON ((-1 -1,-1 5,5 5,5 -1,-1 -1))", "LINESTRING (0 5,5 5,5 0)"},
      // polygon with hole cuts the rectangle
      {"POLYGON ((-2 -2,-2 5,5 5,5 -2,-2 -2), (3 3,4 2,4 4,3 3))",
       "GEOMETRYCOLLECTION (POLYGON ((3 3,4 4,4 2,3 3)),LINESTRING (0 5,5 5,5 0))"},
      // rectangle cuts both the polygon and the hole
      {"POLYGON ((-2 -2,-2 5,5 5,5 -2,-2 -2), (-1 -1,3 1,3 3,-1 -1))",
       "MULTILINESTRING ((0 5,5 5,5 0),(1 0,3 1,3 3,0 0))"},
      // Triangle at two corners and one edge
      {
          "POLYGON ((0 0,5 10,10 0,0 0))",
          "LINESTRING (0 0,5 10,10 0)",
      },
      // Same triangle with another starting point
      {"POLYGON ((5 10,0 0,10 0,5 10))", "LINESTRING (10 0,5 10,0 0)"},
      // Triangle intersection at corner and edge
      {"POLYGON ((-5 -5,5 5,5 -5,-5 -5))", "LINESTRING (0 0,5 5,5 0)"},
      // Triangle intersection at adjacent edges
      {"POLYGON ((-1 5,5 11,5 5,-1 5))", "MULTILINESTRING ((0 6,4 10),(5 10,5 5,0 5))"},
      // One triangle intersection and one inside edge
      {"POLYGON ((-5 5,5 10,5 5,-5 5))", "LINESTRING (0.0 7.5,5 10,5 5,0 5)"},
      // Triangle intersection at center and end of the same edge
      {"POLYGON ((-10 5,10 10,10 5,-10 5))", "MULTILINESTRING ((0.0 7.5,10 10),(10 5,0 5))"},
      // Two different edges clips
      {"POLYGON ((-5 5,15 15,15 5,-5 5))", "MULTILINESTRING ((0.0 7.5,5 10),(10 5,0 5))"},
      // Inside triangle with all corners at edges
      {"POLYGON ((0 5,5 10,10 5,0 5))", "POLYGON ((0 5,5 10,10 5,0 5))"},
      // Inside triangle whose base is one of the edges
      {"POLYGON ((0 0,5 5,10 0,0 0))", "LINESTRING (0 0,5 5,10 0)"},
      // Triangle touching two corners on the outside
      {"POLYGON ((-5 5,5 15,15 5,-5 5))", "LINESTRING (10 5,0 5)"},
      // Triangle with a diagonal and sharing two edges
      {"POLYGON ((0 0,10 10,10 0,0 0))", "LINESTRING (0 0,10 10)"},
      // Triangle exits edge at a corner
      {"POLYGON ((-5 0,5 10,5 0,-5 0))", "LINESTRING (0 5,5 10,5 0)"},
      // Triangle enters edge at a corner
      {"POLYGON ((-5 10,5 10,0 0,-5 10))", "LINESTRING (5 10,0 0)"},
      // Triangle enters and exits the same edge
      {"POLYGON ((-5 0,5 10,15 0,-5 0))", "LINESTRING (0 5,5 10,10 5)"},
      // Triangle enters at a corner and exits at another
      {"POLYGON ((-5 -5,15 15,15 -5,-5 -5))", "LINESTRING (0 0,10 10)"},
      // From outside to nearest edge etc
      {"POLYGON ((-5 -5,0 5,5 0,-5 -5))", "LINESTRING (0 5,5 0)"},
      // From outside to opposite edge etc
      {"POLYGON ((-10 5,10 5,0 -5,-10 5))", "LINESTRING (0 5,10 5,5 0)"},

      // Drew all combinations I could imagine on paper, and added the following.

      // All triangles fully inside, but some edges will be removed
      {"POLYGON ((0 0,0 10,10 10,0 0))", "LINESTRING (10 10,0 0)"},
      {"POLYGON ((0 5,0 10,10 10,0 5))", "LINESTRING (10 10,0 5)"},
      {"POLYGON ((0 10,10 10,5 0,0 10))", "LINESTRING (10 10,5 0,0 10)"},
      {"POLYGON ((0 10,10 10,5 5,0 10))", "LINESTRING (10 10,5 5,0 10)"},
      {"POLYGON ((0 10,5 10,0 5,0 10))", "LINESTRING (5 10,0 5)"},
      {"POLYGON ((0 10,10 5,0 5,0 10))", "LINESTRING (0 10,10 5,0 5)"},
      {"POLYGON ((0 10,10 0,0 5,0 10))", "LINESTRING (0 10,10 0,0 5)"},
      {"POLYGON ((0 10,5 0,0 5,0 10))", "LINESTRING (0 10,5 0,0 5)"},
      {"POLYGON ((0 10,5 5,0 5,0 10))", "LINESTRING (0 10,5 5,0 5)"},
      {"POLYGON ((0 10,7 7,3 3,0 10))", "POLYGON ((0 10,7 7,3 3,0 10))"},
      {"POLYGON ((0 10,5 5,5 0,0 10))", "POLYGON ((0 10,5 5,5 0,0 10))"},
      {"POLYGON ((0 10,10 5,5 0,0 10))", "POLYGON ((0 10,10 5,5 0,0 10))"},
      {"POLYGON ((2 5,5 7,7 5,2 5))", "POLYGON ((2 5,5 7,7 5,2 5))"},
      {"POLYGON ((2 5,5 10,7 5,2 5))", "POLYGON ((2 5,5 10,7 5,2 5))"},
      {"POLYGON ((0 5,5 10,5 5,0 5))", "POLYGON ((0 5,5 10,5 5,0 5))"},
      {"POLYGON ((0 5,5 10,10 5,0 5))", "POLYGON ((0 5,5 10,10 5,0 5))"},
      {"POLYGON ((0 5,5 7,10 5,0 5))", "POLYGON ((0 5,5 7,10 5,0 5))"},

      // No points inside, one intersection
      {"POLYGON ((-5 10,0 15,0 10,-5 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((-5 10,0 5,-5 0,-5 10))", "GEOMETRYCOLLECTION EMPTY"},
      // No points inside, two intersections
      {"POLYGON ((-5 5,0 10,0 0,-5 5))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((-5 5,0 10,0 5,-5 5))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((-5 5,0 7,0 3,-5 5))", "GEOMETRYCOLLECTION EMPTY"},
      // One point inside
      {
          "POLYGON ((5 5,-5 0,-5 10,5 5))",
          "LINESTRING (0.0 7.5,5 5,0.0 2.5)",
      },
      {"POLYGON ((5 0,-5 0,-5 10,5 0))", "LINESTRING (0 5,5 0)"},
      {"POLYGON((10 0,-10 0,-10 10,10 0))", "LINESTRING (0 5,10 0)"},
      {"POLYGON ((5 0,-5 5,-5 10,5 0))", "LINESTRING (0 5,5 0,0.0 2.5)"},
      {"POLYGON ((10 5,-10 0,-10 10,10 5))", "LINESTRING (0.0 7.5,10 5,0.0 2.5)"},
      {"POLYGON ((10 10,-10 0,-10 5,10 10))", "LINESTRING (0.0 7.5,10 10,0 5)"},
      {"POLYGON ((5 5,-5 -5,-5 15,5 5))", "LINESTRING (0 10,5 5,0 0)"},
      {"POLYGON ((10 5,-10 -5,-10 15,10 5))", "LINESTRING (0 10,10 5,0 0)"},
      {
          "POLYGON ((5 0,-5 0,-5 20,5 0))",
          "LINESTRING (0 10,5 0)",
      },
      {"POLYGON ((10 0,-10 0,-10 20,10 0))", "LINESTRING (0 10,10 0)"},
      {
          "POLYGON ((5 5,-10 5,0 15,5 5))",
          "LINESTRING (2.5 10.0,5 5,0 5)",
      },
      {"POLYGON ((5 5,-5 -5,0 15,5 5))", "LINESTRING (2.5 10.0,5 5,0 0)"},
      {
          "POLYGON ((5 5,-15 -20,-15 30,5 5))",
          "LINESTRING (1 10,5 5,1 0)",
      },
      // Two points inside
      {"POLYGON ((5 7,5 3,-5 5,5 7))", "LINESTRING (0 6,5 7,5 3,0 4)"},
      {"POLYGON ((5 7,5 3,-5 13,5 7))", "LINESTRING (0 10,5 7,5 3,0 8)"},
      {"POLYGON ((6 6,4 4,-4 14,6 6))", "LINESTRING (1.0 10.0,6 6,4 4,0 9)"},

      // Polygon with hole which surrounds the rectangle
      {"POLYGON ((-2 -2,-2 12,12 12,12 -2,-2 -2),(-1 -1,11 -1,11 11,-1 11,-1 -1))",
       "GEOMETRYCOLLECTION EMPTY"},
      // Polygon surrounding the rect, but with a hole inside the rect
      {"POLYGON ((-2 -2,-2 12,12 12,12 -2,-2 -2),(1 1,9 1,9 9,1 9,1 1))",
       "POLYGON ((1 1,1 9,9 9,9 1,1 1))"}

  };

  for (int test = 0; test < ntests; ++test)
  {
    OGRGeometry* input;
    OGRGeometry* output;

    const char* wkt = mytests[test][0];
    string ok = mytests[test][1];

    try
    {
      auto err = OGRGeometryFactory::createFromWkt(wkt, NULL, &input);
      if (err != OGRERR_NONE)
        TEST_FAILED("Failed to parse input " + std::string(wkt));
    }
    catch (...)
    {
      TEST_FAILED("Failed to parse WKT for testing: " + std::string(mytests[test][0]));
    }
    output = OGR::lineclip(*input, box);
    string ret = exportToWkt(*output);
    OGRGeometryFactory::destroyGeometry(input);
    OGRGeometryFactory::destroyGeometry(output);
    if (ret != ok)
      TEST_FAILED("Test " + std::to_string(test) + "\n\tInput   : " +
                  std::string(mytests[test][0]) + "\n\tExpected: " + ok + "\n\tGot     : " + ret);
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void polyclip()
{
  using namespace Fmi;
  using Fmi::Box;
  using OGR::exportToWkt;

  Box box(0, 0, 10, 10, 10, 10);  // 0,0-->10,10 with irrelevant transformation sizes

  int ntests = 76;
  char* mytests[76][2] = {
      // inside
      {"LINESTRING (1 1,1 9,9 9,9 1)", "LINESTRING (1 1,1 9,9 9,9 1)"},
      // outside
      {"LINESTRING (-1 -9,-1 11,9 11)", "GEOMETRYCOLLECTION EMPTY"},
      // go in from left
      {"LINESTRING (-1 5,5 5,9 9)", "LINESTRING (0 5,5 5,9 9)"},
      // go out from right
      {"LINESTRING (5 5,8 5,12 5)", "LINESTRING (5 5,8 5,10 5)"},
      // go in and out
      {"LINESTRING (5 -1,5 5,1 2,-3 2,1 6)", "MULTILINESTRING ((5 0,5 5,1 2,0 2),(0 5,1 6))"},
      // go along left edge
      {"LINESTRING (0 3,0 5,0 7)", "GEOMETRYCOLLECTION EMPTY"},
      // go out from left edge
      {"LINESTRING (0 3,0 5,-1 7)", "GEOMETRYCOLLECTION EMPTY"},
      // go in from left edge
      {"LINESTRING (0 3,0 5,2 7)", "LINESTRING (0 5,2 7)"},
      // triangle corner at bottom left corner
      {"LINESTRING (2 1,0 0,1 2)", "LINESTRING (2 1,0 0,1 2)"},
      // go from in to edge and back in
      {"LINESTRING (3 3,0 3,0 5,2 7)", "MULTILINESTRING ((3 3,0 3),(0 5,2 7))"},
      // go from in to edge and then straight out
      {"LINESTRING (5 5,10 5,20 5)", "LINESTRING (5 5,10 5)"},
      // triangle corner at left edge
      {"LINESTRING (3 3,0 6,3 9)", "LINESTRING (3 3,0 6,3 9)"},

      // polygon completely inside
      {"POLYGON ((5 5,5 6,6 6,6 5,5 5))", "POLYGON ((5 5,5 6,6 6,6 5,5 5))"},
      // polygon completely outside
      {"POLYGON ((15 15,15 16,16 16,16 15,15 15))", "GEOMETRYCOLLECTION EMPTY"},
      // polygon surrounds the rectangle
      {"POLYGON ((-1 -1,-1 11,11 11,11 -1,-1 -1))", "POLYGON ((0 0,0 10,10 10,10 0,0 0))"},
      // polygon cuts the rectangle
      {"POLYGON ((-1 -1,-1 5,5 5,5 -1,-1 -1))", "POLYGON ((0 0,0 5,5 5,5 0,0 0))"},
      // polygon with hole cuts the rectangle
      {"POLYGON ((-2 -2,-2 5,5 5,5 -2,-2 -2), (3 3,4 4,4 2,3 3))",
       "POLYGON ((0 0,0 5,5 5,5 0,0 0),(3 3,4 2,4 4,3 3))"},
      // rectangle cuts both the polygon and the hole
      {"POLYGON ((-2 -2,-2 5,5 5,5 -2,-2 -2), (-1 -1,3 1,3 3,-1 -1))",
       "POLYGON ((0 0,0 5,5 5,5 0,1 0,3 1,3 3,0 0))"},
      // Triangle at two corners and one edge
      {"POLYGON ((0 0,5 10,10 0,0 0))", "POLYGON ((0 0,5 10,10 0,0 0))"},
      // This was disabled since there is no code to normalize a polygon which is not modified at
      // all by clipping.
      // Same triangle with another starting point is normalized
      {"POLYGON ((5 10,10 0,0 0,5 10))", "POLYGON ((0 0,5 10,10 0,0 0))"},
      // Triangle intersection at corner and edge
      {"POLYGON ((-5 -5,5 5,5 -5,-5 -5))", "POLYGON ((0 0,5 5,5 0,0 0))"},
      // All triangles fully inside
      {"POLYGON ((0 0,0 10,10 10,0 0))", "POLYGON ((0 0,0 10,10 10,0 0))"},
      {"POLYGON ((0 5,0 10,10 10,0 5))", "POLYGON ((0 5,0 10,10 10,0 5))"},
      {"POLYGON ((0 10,10 10,5 0,0 10))", "POLYGON ((0 10,10 10,5 0,0 10))"},
      {"POLYGON ((0 10,10 10,5 5,0 10))", "POLYGON ((0 10,10 10,5 5,0 10))"},
      {"POLYGON ((0 10,5 10,0 5,0 10))", "POLYGON ((0 5,0 10,5 10,0 5))"},
      {"POLYGON ((0 10,10 5,0 5,0 10))", "POLYGON ((0 5,0 10,10 5,0 5))"},
      {"POLYGON ((0 10,10 0,0 5,0 10))", "POLYGON ((0 5,0 10,10 0,0 5))"},
      {"POLYGON ((0 10,5 0,0 5,0 10))", "POLYGON ((0 5,0 10,5 0,0 5))"},
      {"POLYGON ((0 10,5 5,0 5,0 10))", "POLYGON ((0 5,0 10,5 5,0 5))"},
      {"POLYGON ((0 10,7 7,3 3,0 10))", "POLYGON ((0 10,7 7,3 3,0 10))"},
      {"POLYGON ((0 10,5 5,5 0,0 10))", "POLYGON ((0 10,5 5,5 0,0 10))"},
      {"POLYGON ((0 10,10 5,5 0,0 10))", "POLYGON ((0 10,10 5,5 0,0 10))"},
      {"POLYGON ((2 5,5 7,7 5,2 5))", "POLYGON ((2 5,5 7,7 5,2 5))"},
      {"POLYGON ((2 5,5 10,7 5,2 5))", "POLYGON ((2 5,5 10,7 5,2 5))"},
      {"POLYGON ((0 5,5 10,5 5,0 5))", "POLYGON ((0 5,5 10,5 5,0 5))"},
      {"POLYGON ((0 5,5 10,10 5,0 5))", "POLYGON ((0 5,5 10,10 5,0 5))"},
      {"POLYGON ((0 5,5 7,10 5,0 5))", "POLYGON ((0 5,5 7,10 5,0 5))"},

      // No points inside, one intersection
      {"POLYGON ((-5 10,0 15,0 10,-5 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((-5 10,0 5,-5 0,-5 10))", "GEOMETRYCOLLECTION EMPTY"},
      // No points inside, two intersections
      {"POLYGON ((-5 5,0 10,0 0,-5 5))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((-5 5,0 10,0 5,-5 5))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((-5 5,0 7,0 3,-5 5))", "GEOMETRYCOLLECTION EMPTY"},
      // One point inside
      {"POLYGON ((5 5,-5 0,-5 10,5 5))", "POLYGON ((0.0 2.5,0.0 7.5,5 5,0.0 2.5))"},
      {"POLYGON ((5 0,-5 0,-5 10,5 0))", "POLYGON ((0 0,0 5,5 0,0 0))"},
      {"POLYGON ((10 0,-10 0,-10 10,10 0))", "POLYGON ((0 0,0 5,10 0,0 0))"},
      {"POLYGON ((5 0,-5 5,-5 10,5 0))", "POLYGON ((0.0 2.5,0 5,5 0,0.0 2.5))"},
      {"POLYGON ((10 5,-10 0,-10 10,10 5))", "POLYGON ((0.0 2.5,0.0 7.5,10 5,0.0 2.5))"},
      {"POLYGON ((10 10,-10 0,-10 5,10 10))", "POLYGON ((0 5,0.0 7.5,10 10,0 5))"},
      {"POLYGON ((5 5,-5 -5,-5 15,5 5))", "POLYGON ((0 0,0 10,5 5,0 0))"},
      {"POLYGON ((10 5,-10 -5,-10 15,10 5))", "POLYGON ((0 0,0 10,10 5,0 0))"},
      {"POLYGON ((5 0,-5 0,-5 20,5 0))", "POLYGON ((0 0,0 10,5 0,0 0))"},
      {"POLYGON ((10 0,-10 0,-10 20,10 0))", "POLYGON ((0 0,0 10,10 0,0 0))"},
      {"POLYGON ((5 5,-10 5,0 15,5 5))", "POLYGON ((0 5,0 10,2.5 10.0,5 5,0 5))"},
      {"POLYGON ((5 5,-5 -5,0 15,5 5))", "POLYGON ((0 0,0 10,2.5 10.0,5 5,0 0))"},
      {"POLYGON ((5 5,-15 -20,-15 30,5 5))", "POLYGON ((0 0,0 10,1 10,5 5,1 0,0 0))"},
      // Two points inside
      {"POLYGON ((5 7,5 3,-5 5,5 7))", "POLYGON ((0 4,0 6,5 7,5 3,0 4))"},
      {"POLYGON ((5 7,5 3,-5 13,5 7))", "POLYGON ((0 8,0 10,5 7,5 3,0 8))"},
      {"POLYGON ((6 6,4 4,-4 14,6 6))", "POLYGON ((0 9,0 10,1.0 10.0,6 6,4 4,0 9))"},

      // Polygon with hole which surrounds the rectangle
      {"POLYGON ((-2 -2,-2 12,12 12,12 -2,-2 -2),(-1 -1,11 -1,11 11,-1 11,-1 -1))",
       "GEOMETRYCOLLECTION EMPTY"},
      // Polygon surrounding the rect, but with a hole inside the rect
      {"POLYGON ((-2 -2,-2 12,12 12,12 -2,-2 -2),(1 1,9 1,9 9,1 9,1 1))",
       "POLYGON ((0 0,0 10,10 10,10 0,0 0),(1 1,9 1,9 9,1 9,1 1))"},
      // Polygon with hole cut at the right corner
      {"POLYGON ((5 5,15 5,15 -5,5 -5,5 5),(8 1,8 -1,9 -1,9 1,8 1))",
       "POLYGON ((5 0,5 5,10 5,10 0,9 0,9 1,8 1,8 0,5 0))"},
      // Polygon going around a corner
      {"POLYGON ((-6 5,5 5,5 -6,-6 5))", "POLYGON ((0 0,0 5,5 5,5 0,0 0))"},
      // Hole in a corner
      {"POLYGON ((-15 -15,-15 15,15 15,15 -15,-15 -15),(-5 5,-5 -5,5 -5,5 5,-5 5))",
       "POLYGON ((0 5,0 10,10 10,10 0,5 0,5 5,0 5))"},
      // Hole going around a corner
      {"POLYGON ((-15 -15,-15 15,15 15,15 -15,-15 -15),(-6 5,5 -6,5 5,-6 5))",
       "POLYGON ((0 5,0 10,10 10,10 0,5 0,5 5,0 5))"},
      // Surround the rectangle, hole outside rectangle
      {"POLYGON ((-15 -15,-15 15,15 15,15 -15,-15 -15),(-6 5,-5 5,-5 6,-6 6,-6 5))",
       "POLYGON ((0 0,0 10,10 10,10 0,0 0))"},
      // Surround the rectangle, hole outside rectangle but shares edge
      {"POLYGON ((-15 -15,-15 15,15 15,15 -15,-15 -15),(0 5,-1 5,-1 6,0 6,0 5))",
       "POLYGON ((0 0,0 10,10 10,10 0,0 0))"},
      // Polygon with hole, box intersects both
      {"POLYGON ((-5 1,-5 9,5 9,5 1,-5 1),(-4 8,-4 2,4 2,4 8,-4 8)))",
       "POLYGON ((0 1,0 2,4 2,4 8,0 8,0 9,5 9,5 1,0 1))"},
      // Polygon with hole, box intersects both - variant 2
      {"POLYGON ((-15 1,-15 15,15 15,15 1,-5 1),(-1 3,-1 2,1 2,1 3,-1 3))",
       "POLYGON ((0 1,0 2,1 2,1 3,0 3,0 10,10 10,10 1,0 1))"},
      // Hole touches the edge
      {"POLYGON ((1 1,1 9,10 9,10 1,1 1),(5 5,6 5,5 6,5 5))",
       "POLYGON ((1 1,1 9,10 9,10 1,1 1),(5 5,6 5,5 6,5 5))"},
      {"POLYGON ((1 9,10 9,10 1,1 1,1 9),(5 5,6 5,5 6,5 5))",
       "POLYGON ((1 1,1 9,10 9,10 1,1 1),(5 5,6 5,5 6,5 5))"},
      {"POLYGON ((10 9,10 1,1 1,1 9,10 9),(5 5,6 5,5 6,5 5))",
       "POLYGON ((1 1,1 9,10 9,10 1,1 1),(5 5,6 5,5 6,5 5))"},
      {"POLYGON ((10 1,1 1,1 9,10 9,10 1),(5 5,6 5,5 6,5 5))",
       "POLYGON ((1 1,1 9,10 9,10 1,1 1),(5 5,6 5,5 6,5 5))"},
      {"POLYGON ((1 1,1 9,10 9,10 1,1 1),(6 5,5 6,5 5,6 5))",
       "POLYGON ((1 1,1 9,10 9,10 1,1 1),(5 5,6 5,5 6,5 5))"},
      {"POLYGON ((1 1,1 9,10 9,10 1,1 1),(5 6,5 5,6 5,5 6))",
       "POLYGON ((1 1,1 9,10 9,10 1,1 1),(5 5,6 5,5 6,5 5))"},
      {"POLYGON ((1 1,1 9,10 9,10 1,1 1),(5 5,6 5,5 6,5 5))",
       "POLYGON ((1 1,1 9,10 9,10 1,1 1),(5 5,6 5,5 6,5 5))"}

  };

  for (int test = 0; test < ntests; ++test)
  {
    OGRGeometry* input;
    OGRGeometry* output;

    const char* wkt = mytests[test][0];
    string ok = mytests[test][1];

    try
    {
      auto err = OGRGeometryFactory::createFromWkt(wkt, NULL, &input);
      if (err != OGRERR_NONE)
        TEST_FAILED("Failed to parse input " + std::string(wkt));
    }
    catch (...)
    {
      TEST_FAILED("Failed to parse WKT for testing: " + std::string(mytests[test][0]));
    }
    output = OGR::polyclip(*input, box);
    string ret = exportToWkt(*output);
    OGRGeometryFactory::destroyGeometry(input);
    OGRGeometryFactory::destroyGeometry(output);
    if (ret != ok)
      TEST_FAILED("Test " + std::to_string(test) + "\n\tInput   : " +
                  std::string(mytests[test][0]) + "\n\tExpected: " + ok + "\n\tGot     : " + ret);
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void polyclip_spike()
{
  using namespace Fmi;
  using Fmi::Box;
  using OGR::exportToWkt;

  Box box(0, 0, 10, 10, 10, 10);  // 0,0-->10,10 with irrelevant transformation sizes

  // char* wkt = "POLYGON ((5 5,5 4.9999,-1e-20 5,5 5))";
  // string ok = "POLYGON ((0 5,5 5,5.0 4.9999,0 5))";
  char* wkt = "POLYGON ((-1 5,1e-20 5,-1 0,-1 5))";
  string ok = "GEOMETRYCOLLECTION EMPTY";

  OGRGeometry* input;
  OGRGeometry* output;

  try
  {
    auto err = OGRGeometryFactory::createFromWkt(wkt, NULL, &input);
    if (err != OGRERR_NONE)
      TEST_FAILED("Failed to parse input " + std::string(wkt));
  }
  catch (...)
  {
    TEST_FAILED("Failed to parse WKT for testing: " + std::string(wkt));
  }
  output = OGR::polyclip(*input, box);
  string ret = exportToWkt(*output);
  OGRGeometryFactory::destroyGeometry(input);
  OGRGeometryFactory::destroyGeometry(output);
  if (ret != ok)
    TEST_FAILED("\n\tInput   : " + std::string(wkt) + "\n\tExpected: " + ok +
                "\n\tGot     : " + ret);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void polyclip_case_hirlam()
{
  using namespace Fmi;
  using Fmi::Box;
  using OGR::exportToWkt;

  // Real test case when the second column from the left had NaN values at every second row.
  // The failing case *must* travel along the edge, and then touch it *twice*. Like a saw with
  // two corners at the left edge plus one flat edge.

  // HIRLAM left edge is at -33.5. Changing the value to -33 works, so test this demonstrates a
  // rounding issue in the algorithm.

  Box box(-33.5, 0, 100, 100, 100, 100);

  std::string wkt =
      "POLYGON ((-33.5 2.0,-33.5 3.0,-33 3,-33.5 4.0,-33 4,-33.5 5.0,-32 5,-32 2,-33.5 2.0))";

  OGRGeometry* input;

  try
  {
    auto err = OGRGeometryFactory::createFromWkt(wkt.c_str(), NULL, &input);
    if (err != OGRERR_NONE)
      TEST_FAILED("Failed to parse input " + wkt);
  }
  catch (...)
  {
    TEST_FAILED("Failed to parse WKT for testing: " + wkt);
  }

  auto* output = OGR::polyclip(*input, box);
  string ret = exportToWkt(*output);
  OGRGeometryFactory::destroyGeometry(input);

  if (ret != wkt)
    TEST_FAILED("Failed\n\tExpected: " + wkt + "\n\tGot     : " + ret);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void linecut()
{
  using namespace Fmi;
  using Fmi::Box;
  using OGR::exportToWkt;

  Box box(0, 0, 10, 10, 10, 10);  // 0,0-->10,10 with irrelevant transformation sizes

  int ntests = 75;

  char* mytests[75][2] = {
      // inside
      {"LINESTRING (1 1,1 9,9 9,9 1)", "GEOMETRYCOLLECTION EMPTY"},
      // outside
      {"LINESTRING (-1 -9,-1 11,9 11)", "LINESTRING (-1 -9,-1 11,9 11)"},
      // go in from left
      {"LINESTRING (-1 5,5 5,9 9)", "LINESTRING (-1 5,0 5)"},
      // go out from right
      {"LINESTRING (5 5,8 5,12 5)", "LINESTRING (10 5,12 5)"},
      // go in and out
      {"LINESTRING (5 -1,5 5,1 2,-3 2,1 6)", "MULTILINESTRING ((5 -1,5 0),(0 2,-3 2,0 5))"},
      // go along left edge
      {"LINESTRING (0 3,0 5,0 7)", "GEOMETRYCOLLECTION EMPTY"},
      // go out from left edge
      {"LINESTRING (0 3,0 5,-1 7)", "LINESTRING (0 5,-1 7)"},
      // go in from left edge
      {"LINESTRING (0 3,0 5,2 7)", "GEOMETRYCOLLECTION EMPTY"},
      // triangle corner at bottom left corner
      {"LINESTRING (2 1,0 0,1 2)", "GEOMETRYCOLLECTION EMPTY"},
      // go from in to edge and back in
      {"LINESTRING (3 3,0 3,0 5,2 7)", "GEOMETRYCOLLECTION EMPTY"},
      // go from in to edge and then straight out
      {"LINESTRING (5 5,10 5,20 5)", "LINESTRING (10 5,20 5)"},
      // triangle corner at left edge
      {"LINESTRING (3 3,0 6,3 9)", "GEOMETRYCOLLECTION EMPTY"},

      // polygon completely inside
      {"POLYGON ((5 5,5 6,6 6,6 5,5 5))", "GEOMETRYCOLLECTION EMPTY"},
      // polygon completely outside
      {"POLYGON ((15 15,15 16,16 16,16 15,15 15))", "POLYGON ((15 15,15 16,16 16,16 15,15 15))"},
      // polygon surrounds the rectangle
      {"POLYGON ((-1 -1,-1 11,11 11,11 -1,-1 -1))", "POLYGON ((-1 -1,-1 11,11 11,11 -1,-1 -1))"},
      // polygon cuts the rectangle
      {"POLYGON ((-1 -1,-1 5,5 5,5 -1,-1 -1))", "LINESTRING (5 0,5 -1,-1 -1,-1 5,0 5)"},
      // polygon with hole cuts the rectangle
      {"POLYGON ((-2 -2,-2 5,5 5,5 -2,-2 -2), (3 3,4 4,4 2,3 3))",
       "LINESTRING (5 0,5 -2,-2 -2,-2 5,0 5)"},
      // rectangle cuts both the polygon and the hole
      {"POLYGON ((-2 -2,-2 5,5 5,5 -2,-2 -2), (-1 -1,3 1,3 3,-1 -1))",
       "MULTILINESTRING ((5 0,5 -2,-2 -2,-2 5,0 5),(0 0,-1 -1,1 0))"},
      // Triangle at two corners and one edge
      {
          "POLYGON ((0 0,5 10,10 0,0 0))",
          "GEOMETRYCOLLECTION EMPTY",
      },
      // Same triangle with another starting point
      {"POLYGON ((5 10,0 0,10 0,5 10))", "GEOMETRYCOLLECTION EMPTY"},
      // Triangle intersection at corner and edge
      {"POLYGON ((-5 -5,5 5,5 -5,-5 -5))", "LINESTRING (5 0,5 -5,-5 -5,0 0)"},
      // Triangle intersection at adjacent edges
      {"POLYGON ((-1 5,5 11,5 5,-1 5))", "MULTILINESTRING ((4 10,5 11,5 10),(0 5,-1 5,0 6))"},
      // One triangle intersection and one inside edge
      {"POLYGON ((-5 5,5 10,5 5,-5 5))", "LINESTRING (0 5,-5 5,0.0 7.5)"},
      // Triangle intersection at center and end of the same edge
      {"POLYGON ((-10 5,10 10,10 5,-10 5))", "LINESTRING (0 5,-10 5,0.0 7.5)"},
      // Two different edges clips
      {"POLYGON ((-5 5,15 15,15 5,-5 5))",
       "MULTILINESTRING ((5 10,15 15,15 5,10 5),(0 5,-5 5,0.0 7.5))"},
      // Inside triangle with all corners at edges
      {"POLYGON ((0 5,5 10,10 5,0 5))", "GEOMETRYCOLLECTION EMPTY"},
      // Inside triangle whose base is one of the edges
      {"POLYGON ((0 0,5 5,10 0,0 0))", "GEOMETRYCOLLECTION EMPTY"},
      // Triangle touching two corners on the outside
      {"POLYGON ((-5 5,5 15,15 5,-5 5))", "LINESTRING (0 5,-5 5,5 15,15 5,10 5)"},
      // Triangle with a diagonal and sharing two edges
      {"POLYGON ((0 0,10 10,10 0,0 0))", "GEOMETRYCOLLECTION EMPTY"},
      // Triangle exits edge at a corner
      {"POLYGON ((-5 0,5 10,5 0,-5 0))", "LINESTRING (0 0,-5 0,0 5)"},
      // Triangle enters edge at a corner
      {"POLYGON ((-5 10,5 10,0 0,-5 10))", "LINESTRING (0 0,-5 10,0 10)"},
      // Triangle enters and exits the same edge
      {"POLYGON ((-5 0,5 10,15 0,-5 0))", "MULTILINESTRING ((10 5,15 0,10 0),(0 0,-5 0,0 5))"},
      // Triangle enters at a corner and exits at another
      {"POLYGON ((-5 -5,15 15,15 -5,-5 -5))", "LINESTRING (10 10,15 15,15 -5,-5 -5,0 0)"},
      // From outside to nearest edge etc
      {"POLYGON ((-5 -5,0 5,5 0,-5 -5))", "LINESTRING (5 0,-5 -5,0 5)"},
      // From outside to opposite edge etc
      {"POLYGON ((-10 5,10 5,0 -5,-10 5))", "LINESTRING (5 0,0 -5,-10 5,0 5)"},

      // Drew all combinations I could imagine on paper, and added the following.

      // All triangles fully inside
      {"POLYGON ((0 0,0 10,10 10,0 0))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 5,0 10,10 10,0 5))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,10 10,5 0,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,10 10,5 5,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,5 10,0 5,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,10 5,0 5,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,10 0,0 5,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,5 0,0 5,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,5 5,0 5,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,7 7,3 3,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,5 5,5 0,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,10 5,5 0,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((2 5,5 7,7 5,2 5))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((2 5,5 10,7 5,2 5))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 5,5 10,5 5,0 5))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 5,5 10,10 5,0 5))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 5,5 7,10 5,0 5))", "GEOMETRYCOLLECTION EMPTY"},

      // No points inside, one intersection
      {"POLYGON ((-5 10,0 15,0 10,-5 10))", "POLYGON ((-5 10,0 15,0 10,-5 10))"},
      {"POLYGON ((-5 10,0 5,-5 0,-5 10))", "POLYGON ((-5 0,-5 10,0 5,-5 0))"},
      // No points inside, two intersections
      {"POLYGON ((-5 5,0 10,0 0,-5 5))", "LINESTRING (0 0,-5 5,0 10)"},
      {"POLYGON ((-5 5,0 10,0 5,-5 5))", "LINESTRING (0 5,-5 5,0 10)"},
      {"POLYGON ((-5 5,0 7,0 3,-5 5))", "LINESTRING (0 3,-5 5,0 7)"},
      // One point inside
      {
          "POLYGON ((5 5,-5 0,-5 10,5 5))",
          "LINESTRING (0.0 2.5,-5 0,-5 10,0.0 7.5)",
      },
      {"POLYGON ((5 0,-5 0,-5 10,5 0))", "LINESTRING (0 0,-5 0,-5 10,0 5)"},
      {"POLYGON ((10 0,-10 0,-10 10,10 0))", "LINESTRING (0 0,-10 0,-10 10,0 5)"},
      {"POLYGON ((5 0,-5 5,-5 10,5 0))", "LINESTRING (0.0 2.5,-5 5,-5 10,0 5)"},
      {"POLYGON ((10 5,-10 0,-10 10,10 5))", "LINESTRING (0.0 2.5,-10 0,-10 10,0.0 7.5)"},
      {"POLYGON ((10 10,-10 0,-10 5,10 10))", "LINESTRING (0 5,-10 0,-10 5,0.0 7.5)"},
      {"POLYGON ((5 5,-5 -5,-5 15,5 5))", "LINESTRING (0 0,-5 -5,-5 15,0 10)"},
      {"POLYGON ((10 5,-10 -5,-10 15,10 5))", "LINESTRING (0 0,-10 -5,-10 15,0 10)"},
      {
          "POLYGON ((5 0,-5 0,-5 20,5 0))",
          "LINESTRING (0 0,-5 0,-5 20,0 10)",
      },
      {"POLYGON ((10 0,-10 0,-10 20,10 0))", "LINESTRING (0 0,-10 0,-10 20,0 10)"},
      {
          "POLYGON ((5 5,-10 5,0 15,5 5))",
          "LINESTRING (0 5,-10 5,0 15,2.5 10.0)",
      },
      {"POLYGON ((5 5,-5 -5,0 15,5 5))", "LINESTRING (0 0,-5 -5,0 15,2.5 10.0)"},
      {
          "POLYGON ((5 5,-15 -20,-15 30,5 5))",
          "LINESTRING (1 0,-15 -20,-15 30,1 10)",
      },
      // Two points inside
      {"POLYGON ((5 7,5 3,-5 5,5 7))", "LINESTRING (0 4,-5 5,0 6)"},
      {"POLYGON ((5 7,5 3,-5 13,5 7))", "LINESTRING (0 8,-5 13,0 10)"},
      {"POLYGON ((6 6,4 4,-4 14,6 6))", "LINESTRING (0 9,-4 14,1.0 10.0)"},

      // Polygon with hole which surrounds the rectangle
      {"POLYGON ((-2 -2,-2 12,12 12,12 -2,-2 -2),(-1 -1,11 -1,11 11,-1 11,-1 -1))",
       "POLYGON ((-2 -2,-2 12,12 12,12 -2,-2 -2),(-1 -1,11 -1,11 11,-1 11,-1 -1))"},
      // Polygon surrounding the rect, but with a hole inside the rect
      {"POLYGON ((-2 -2,-2 12,12 12,12 -2,-2 -2),(1 1,9 1,9 9,1 9,1 1))",
       "POLYGON ((-2 -2,-2 12,12 12,12 -2,-2 -2))"}

  };

  for (int test = 0; test < ntests; ++test)
  {
    OGRGeometry* input;
    OGRGeometry* output;

    const char* wkt = mytests[test][0];
    string ok = mytests[test][1];

    try
    {
      auto err = OGRGeometryFactory::createFromWkt(wkt, NULL, &input);
      if (err != OGRERR_NONE)
        TEST_FAILED("Failed to parse input " + std::string(wkt));
    }
    catch (...)
    {
      TEST_FAILED("Failed to parse WKT for testing: " + std::string(mytests[test][0]));
    }
    output = OGR::linecut(*input, box);
    string ret = exportToWkt(*output);
    OGRGeometryFactory::destroyGeometry(input);
    OGRGeometryFactory::destroyGeometry(output);
    if (ret != ok)
      TEST_FAILED("Test " + std::to_string(test) + "\n\tInput   : " +
                  std::string(mytests[test][0]) + "\n\tExpected: " + ok + "\n\tGot     : " + ret);
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void polycut()
{
  using namespace Fmi;
  using Fmi::Box;
  using OGR::exportToWkt;

  Box box(0, 0, 10, 10, 10, 10);  // 0,0-->10,10 with irrelevant transformation sizes

  int ntests = 69;
  char* mytests[69][2] = {
      // inside
      {"LINESTRING (1 1,1 9,9 9,9 1)", "GEOMETRYCOLLECTION EMPTY"},
      // outside
      {"LINESTRING (-1 -9,-1 11,9 11)", "LINESTRING (-1 -9,-1 11,9 11)"},
      // go in from left
      {"LINESTRING (-1 5,5 5,9 9)", "LINESTRING (-1 5,0 5)"},
      // go out from right
      {"LINESTRING (5 5,8 5,12 5)", "LINESTRING (10 5,12 5)"},
      // go in and out
      {"LINESTRING (5 -1,5 5,1 2,-3 2,1 6)", "MULTILINESTRING ((5 -1,5 0),(0 2,-3 2,0 5))"},
      // go along left edge
      {"LINESTRING (0 3,0 5,0 7)", "GEOMETRYCOLLECTION EMPTY"},
      // go out from left edge
      {"LINESTRING (0 3,0 5,-1 7)", "LINESTRING (0 5,-1 7)"},
      // go in from left edge
      {"LINESTRING (0 3,0 5,2 7)", "GEOMETRYCOLLECTION EMPTY"},
      // triangle corner at bottom left corner
      {"LINESTRING (2 1,0 0,1 2)", "GEOMETRYCOLLECTION EMPTY"},
      // go from in to edge and back in
      {"LINESTRING (3 3,0 3,0 5,2 7)", "GEOMETRYCOLLECTION EMPTY"},
      // go from in to edge and then straight out
      {"LINESTRING (5 5,10 5,20 5)", "LINESTRING (10 5,20 5)"},
      // triangle corner at left edge
      {"LINESTRING (3 3,0 6,3 9)", "GEOMETRYCOLLECTION EMPTY"},

      // polygon completely inside
      {"POLYGON ((5 5,5 6,6 6,6 5,5 5))", "GEOMETRYCOLLECTION EMPTY"},
      // polygon completely outside
      {"POLYGON ((15 15,15 16,16 16,16 15,15 15))", "POLYGON ((15 15,15 16,16 16,16 15,15 15))"},
      // polygon surrounds the rectangle
      {"POLYGON ((-1 -1,-1 11,11 11,11 -1,-1 -1))",
       "POLYGON ((-1 -1,-1 11,11 11,11 -1,-1 -1),(0 0,10 0,10 10,0 10,0 0))"},
      // polygon cuts the rectangle
      {"POLYGON ((-1 -1,-1 5,5 5,5 -1,-1 -1))", "POLYGON ((-1 -1,-1 5,0 5,0 0,5 0,5 -1,-1 -1))"},
      // polygon with hole cuts the rectangle
      {"POLYGON ((-2 -2,-2 5,5 5,5 -2,-2 -2), (3 3,4 4,4 2,3 3))",
       "POLYGON ((-2 -2,-2 5,0 5,0 0,5 0,5 -2,-2 -2))"},
      // rectangle cuts both the polygon and the hole
      {"POLYGON ((-2 -2,-2 5,5 5,5 -2,-2 -2), (-1 -1,3 1,3 3,-1 -1))",
       "POLYGON ((-2 -2,-2 5,0 5,0 0,-1 -1,1 0,5 0,5 -2,-2 -2))"},
      // Triangle at two corners and one edge
      {
          "POLYGON ((0 0,5 10,10 0,0 0))",
          "GEOMETRYCOLLECTION EMPTY",
      },
      // Same triangle with another starting point
      {
          "POLYGON ((5 10,10 0,0 0,5 10))",
          "GEOMETRYCOLLECTION EMPTY",
      },
      // Triangle intersection at corner and edge
      {"POLYGON ((-5 -5,5 5,5 -5,-5 -5))", "POLYGON ((-5 -5,0 0,5 0,5 -5,-5 -5))"},
      // All triangles fully inside
      {"POLYGON ((0 0,0 10,10 10,0 0))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 5,0 10,10 10,0 5))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,10 10,5 0,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,10 10,5 5,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,5 10,0 5,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,10 5,0 5,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,10 0,0 5,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,5 0,0 5,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,5 5,0 5,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,7 7,3 3,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,5 5,5 0,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 10,10 5,5 0,0 10))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((2 5,5 7,7 5,2 5))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((2 5,5 10,7 5,2 5))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 5,5 10,5 5,0 5))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 5,5 10,10 5,0 5))", "GEOMETRYCOLLECTION EMPTY"},
      {"POLYGON ((0 5,5 7,10 5,0 5))", "GEOMETRYCOLLECTION EMPTY"},

      // No points inside, one intersection
      {"POLYGON ((-5 10,0 15,0 10,-5 10))", "POLYGON ((-5 10,0 15,0 10,-5 10))"},
      {"POLYGON ((-5 10,0 5,-5 0,-5 10))", "POLYGON ((-5 0,-5 10,0 5,-5 0))"},
      // No points inside, two intersections
      {"POLYGON ((-5 5,0 10,0 0,-5 5))", "POLYGON ((-5 5,0 10,0 0,-5 5))"},
      {"POLYGON ((-5 5,0 10,0 5,-5 5))", "POLYGON ((-5 5,0 10,0 5,-5 5))"},
      {"POLYGON ((-5 5,0 7,0 3,-5 5))", "POLYGON ((-5 5,0 7,0 3,-5 5))"},
      // One point inside
      {"POLYGON ((5 5,-5 0,-5 10,5 5))", "POLYGON ((-5 0,-5 10,0.0 7.5,0.0 2.5,-5 0))"},
      {"POLYGON ((5 0,-5 0,-5 10,5 0))", "POLYGON ((-5 0,-5 10,0 5,0 0,-5 0))"},
      {"POLYGON ((10 0,-10 0,-10 10,10 0))", "POLYGON ((-10 0,-10 10,0 5,0 0,-10 0))"},
      {"POLYGON ((5 0,-5 5,-5 10,5 0))", "POLYGON ((-5 5,-5 10,0 5,0.0 2.5,-5 5))"},
      {"POLYGON ((10 5,-10 0,-10 10,10 5))", "POLYGON ((-10 0,-10 10,0.0 7.5,0.0 2.5,-10 0))"},
      {"POLYGON ((10 10,-10 0,-10 5,10 10))", "POLYGON ((-10 0,-10 5,0.0 7.5,0 5,-10 0))"},
      {"POLYGON ((5 5,-5 -5,-5 15,5 5))", "POLYGON ((-5 -5,-5 15,0 10,0 0,-5 -5))"},
      {"POLYGON ((10 5,-10 -5,-10 15,10 5))", "POLYGON ((-10 -5,-10 15,0 10,0 0,-10 -5))"},
      {"POLYGON ((5 0,-5 0,-5 20,5 0))", "POLYGON ((-5 0,-5 20,0 10,0 0,-5 0))"},
      {"POLYGON ((10 0,-10 0,-10 20,10 0))", "POLYGON ((-10 0,-10 20,0 10,0 0,-10 0))"},
      {"POLYGON ((5 5,-10 5,0 15,5 5))", "POLYGON ((-10 5,0 15,2.5 10.0,0 10,0 5,-10 5))"},
      {"POLYGON ((5 5,-5 -5,0 15,5 5))", "POLYGON ((-5 -5,0 15,2.5 10.0,0 10,0 0,-5 -5))"},
      {"POLYGON ((5 5,-15 -20,-15 30,5 5))",
       "POLYGON ((-15 -20,-15 30,1 10,0 10,0 0,1 0,-15 -20))"},  //
      // Two points inside
      {"POLYGON ((5 7,5 3,-5 5,5 7))", "POLYGON ((-5 5,0 6,0 4,-5 5))"},
      {"POLYGON ((5 7,5 3,-5 13,5 7))", "POLYGON ((-5 13,0 10,0 8,-5 13))"},
      {"POLYGON ((6 6,4 4,-4 14,6 6))", "POLYGON ((-4 14,1.0 10.0,0 10,0 9,-4 14))"},

      // Polygon with hole which surrounds the rectangle
      {"POLYGON ((-2 -2,-2 12,12 12,12 -2,-2 -2),(-1 -1,11 -1,11 11,-1 11,-1 -1))",
       "POLYGON ((-2 -2,-2 12,12 12,12 -2,-2 -2),(-1 -1,11 -1,11 11,-1 11,-1 -1))"},
      // Polygon surrounding the rect, but with a hole inside the rect
      {"POLYGON ((-2 -2,-2 12,12 12,12 -2,-2 -2),(1 1,9 1,9 9,1 9,1 1))",
       "POLYGON ((-2 -2,-2 12,12 12,12 -2,-2 -2),(0 0,10 0,10 10,0 10,0 0))"},
      // Polygon with hole cut at the right corner
      {"POLYGON ((5 5,15 5,15 -5,5 -5,5 5),(8 1,8 -1,9 -1,9 1,8 1))",
       "POLYGON ((5 -5,5 0,8 0,8 -1,9 -1,9 0,10 0,10 5,15 5,15 -5,5 -5))"},
      // Polygon going around a corner
      {"POLYGON ((-6 5,5 5,5 -6,-6 5))", "POLYGON ((-6 5,0 5,0 0,5 0,5 -6,-6 5))"},
      // Hole in a corner
      {"POLYGON ((-15 -15,-15 15,15 15,15 -15,-15 -15),(-5 5,-5 -5,5 -5,5 5,-5 5))",
       "POLYGON ((-15 -15,-15 15,15 15,15 -15,-15 -15),(-5 -5,5 -5,5 0,10 0,10 10,0 10,0 5,-5 5,-5 "
       "-5))"},
      // Hole going around a corner
      {"POLYGON ((-15 -15,-15 15,15 15,15 -15,-15 -15),(-6 5,5 -6,5 5,-6 5))",
       "POLYGON ((-15 -15,-15 15,15 15,15 -15,-15 -15),(-6 5,5 -6,5 0,10 0,10 10,0 10,0 5,-6 5))"},
      // Surround the rectangle, hole outside rectangle
      {"POLYGON ((-15 -15,-15 15,15 15,15 -15,-15 -15),(-5 5,-6 5,-6 6,-5 6,-5 5))",
       "POLYGON ((-15 -15,-15 15,15 15,15 -15,-15 -15),(-6 5,-5 5,-5 6,-6 6,-6 5),(0 0,10 0,10 "
       "10,0 10,0 0))"},
      // Surround the rectangle, hole outside rectangle but shares edge
      {"POLYGON ((-15 -15,-15 15,15 15,15 -15,-15 -15),(-1 5,0 5,0 6,-1 6,-1 5))",
       "POLYGON ((-15 -15,-15 15,15 15,15 -15,-15 -15),(-1 5,0 5,0 0,10 0,10 10,0 10,0 6,-1 6,-1 "
       "5))"},
      // Polygon with hole, box intersects both
      {"POLYGON ((-5 1,-5 9,5 9,5 1,-5 1),(-4 8,-4 2,4 2,4 8,-4 8)))",
       "POLYGON ((-5 1,-5 9,0 9,0 8,-4 8,-4 2,0 2,0 1,-5 1))"},
      // Polygon with hole, box intersects both - variant 2
      {"POLYGON ((-15 1,-15 15,15 15,15 1,-15 1),(-1 3,-1 2,1 2,1 3,-1 3))",
       "POLYGON ((-15 1,-15 15,15 15,15 1,10 1,10 10,0 10,0 3,-1 3,-1 2,0 2,0 1,-15 1))"}};

  for (int test = 0; test < ntests; ++test)
  {
    OGRGeometry* input;
    OGRGeometry* output;

    const char* wkt = mytests[test][0];
    string ok = mytests[test][1];

    try
    {
      auto err = OGRGeometryFactory::createFromWkt(wkt, NULL, &input);
      if (err != OGRERR_NONE)
        TEST_FAILED("Failed to parse input " + std::string(wkt));
    }
    catch (...)
    {
      TEST_FAILED("Failed to parse WKT for testing: " + std::string(mytests[test][0]));
    }
    output = OGR::polycut(*input, box);
    string ret = exportToWkt(*output);
    OGRGeometryFactory::destroyGeometry(input);
    OGRGeometryFactory::destroyGeometry(output);
    if (ret != ok)
      TEST_FAILED("Test " + std::to_string(test) + "\n\tInput   : " +
                  std::string(mytests[test][0]) + "\n\tExpected: " + ok + "\n\tGot     : " + ret);
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void polyclip_segmentation()
{
  using namespace Fmi;
  using Fmi::Box;
  using OGR::exportToWkt;

  Box box(0, 0, 10, 10, 10, 10);  // 0,0-->10,10 with irrelevant transformation sizes

  const double max_segment_length = 2.5;

  int ntests = 6;
  char* mytests[6][2] = {
      // polygon surrounds the rectangle
      {"POLYGON ((-1 -1,-1 11,11 11,11 -1,-1 -1))",
       "POLYGON ((0 0,0.0 2.5,0 5,0.0 7.5,0 10,2.5 10.0,5 10,7.5 10.0,10 10,10.0 7.5,10 5,10.0 "
       "2.5,10 0,7.5 0.0,5 0,2.5 0.0,0 0))"},
      // Polygon with hole which surrounds the rectangle
      {"POLYGON ((-2 -2,-2 12,12 12,12 -2,-2 -2),(-1 -1,11 -1,11 11,-1 11,-1 -1))",
       "GEOMETRYCOLLECTION EMPTY"},
      // Polygon surrounding the rect, but with a hole inside the rect
      {"POLYGON ((-2 -2,-2 12,12 12,12 -2,-2 -2),(1 1,9 1,9 9,1 9,1 1))",
       "POLYGON ((0 0,0.0 2.5,0 5,0.0 7.5,0 10,2.5 10.0,5 10,7.5 10.0,10 10,10.0 7.5,10 5,10.0 "
       "2.5,10 0,7.5 0.0,5 0,2.5 0.0,0 0),(1 1,9 1,9 9,1 9,1 1))"},
      // Surround the rectangle, hole outside rectangle
      {"POLYGON ((-15 -15,-15 15,15 15,15 -15,-15 -15),(-6 5,-5 5,-5 6,-6 6,-6 5))",
       "POLYGON ((0 0,0.0 2.5,0 5,0.0 7.5,0 10,2.5 10.0,5 10,7.5 10.0,10 10,10.0 7.5,10 5,10.0 "
       "2.5,10 0,7.5 0.0,5 0,2.5 0.0,0 0))"},
      // Surround the rectangle, hole outside rectangle but shares edge
      {"POLYGON ((-15 -15,-15 15,15 15,15 -15,-15 -15),(0 5,-1 5,-1 6,0 6,0 5))",
       "POLYGON ((0 0,0.0 2.5,0 5,0.0 7.5,0 10,2.5 10.0,5 10,7.5 10.0,10 10,10.0 7.5,10 5,10.0 "
       "2.5,10 0,7.5 0.0,5 0,2.5 0.0,0 0))"},
      // Polygon with hole, box intersects both
      {"POLYGON ((-5 1,-5 9,5 9,5 1,-5 1),(-4 8,-4 2,4 2,4 8,-4 8)))",
       "POLYGON ((0 1,0 2,4 2,4 8,0 8,0 9,5 9,5 1,0 1))"}};

  for (int test = 0; test < ntests; ++test)
  {
    OGRGeometry* input;
    OGRGeometry* output;

    const char* wkt = mytests[test][0];
    string ok = mytests[test][1];

    try
    {
      auto err = OGRGeometryFactory::createFromWkt(wkt, NULL, &input);
      if (err != OGRERR_NONE)
        TEST_FAILED("Failed to parse input " + std::string(wkt));
    }
    catch (...)
    {
      TEST_FAILED("Failed to parse WKT for testing: " + std::string(mytests[test][0]));
    }
    output = OGR::polyclip(*input, box, max_segment_length);
    string ret = exportToWkt(*output);
    OGRGeometryFactory::destroyGeometry(input);
    OGRGeometryFactory::destroyGeometry(output);
    if (ret != ok)
      TEST_FAILED("Input   : " + std::string(mytests[test][0]) + "\n\tExpected: " + ok +
                  "\n\tGot     : " + ret);
  }

  TEST_PASSED();
}  // namespace Tests

// ----------------------------------------------------------------------

void despeckle()
{
  using namespace Fmi;
  using Fmi::Box;
  using OGR::despeckle;
  using OGR::exportToWkt;

  Box box(0, 0, 10, 10, 10, 10);  // 0,0-->10,10 with irrelevant transformation sizes

  int ntests = 3;

  char* mytests[3][2] = {{"POLYGON ((1 1,1 5,5 5,5 1,1 1))", "POLYGON ((1 1,1 5,5 5,5 1,1 1))"},
                         {"POLYGON ((2 2,2 3,3 3,3 2,2 2))", ""},
                         {"POLYGON ((1 1,1 5,5 5,5 1,1 1), (2 2,2 3,3 3,3 2,2 2))",
                          "POLYGON ((1 1,1 5,5 5,5 1,1 1))"}};

  for (int test = 0; test < ntests; ++test)
  {
    OGRGeometry* input;
    OGRGeometry* output;

    const char* wkt = mytests[test][0];
    string ok = mytests[test][1];

    // std::cerr << "\nTEST: " << std::string(wkt) << std::endl;
    try
    {
      auto err = OGRGeometryFactory::createFromWkt(wkt, NULL, &input);
      if (err != OGRERR_NONE)
        TEST_FAILED("Failed to parse input " + std::string(wkt));
    }
    catch (...)
    {
      TEST_FAILED("Failed to parse WKT for testing: " + std::string(mytests[test][0]));
    }
    output = OGR::despeckle(*input, 2e-6);  // Note: The API multiplies by 1e6
    string ret = (output ? exportToWkt(*output) : "");
    OGRGeometryFactory::destroyGeometry(input);
    OGRGeometryFactory::destroyGeometry(output);
    if (ret != ok)
      TEST_FAILED("Input   : " + std::string(mytests[test][0]) + "\n\tExpected: " + ok +
                  "\n\tGot     : " + ret);
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void despeckle_geography()
{
  using namespace Fmi;
  using Fmi::Box;
  using OGR::despeckle;
  using OGR::exportToWkt;

  Box box(0, 0, 10, 10, 10, 10);  // 0,0-->10,10 with irrelevant transformation sizes

  int ntests = 3;

  char* mytests[3][2] = {{"POLYGON ((1 1,1 5,5 5,5 1,1 1))", "POLYGON ((1 1,1 5,5 5,5 1,1 1))"},
                         {"POLYGON ((2 2,2 3,3 3,3 2,2 2))", ""},
                         {"POLYGON ((1 1,1 5,5 5,5 1,1 1), (2 2,2 3,3 3,3 2,2 2))",
                          "POLYGON ((1 1,1 5,5 5,5 1,1 1))"}};

  OGRSpatialReference wgs84;
  wgs84.SetFromUserInput("WGS84");

  for (int test = 0; test < ntests; ++test)
  {
    OGRGeometry* input;
    OGRGeometry* output;

    const char* wkt = mytests[test][0];
    string ok = mytests[test][1];

    // std::cerr << "\nTEST: " << std::string(wkt) << std::endl;
    try
    {
      auto err = OGRGeometryFactory::createFromWkt(wkt, &wgs84, &input);
      if (err != OGRERR_NONE)
        TEST_FAILED("Failed to parse input " + std::string(wkt));
    }
    catch (...)
    {
      TEST_FAILED("Failed to parse WKT for testing: " + std::string(mytests[test][0]));
    }
    output = OGR::despeckle(*input, 100000);  // Note: The API multiplies by 1e6 to get m^2
    string ret = (output ? exportToWkt(*output) : "");
    OGRGeometryFactory::destroyGeometry(input);
    OGRGeometryFactory::destroyGeometry(output);
    if (ret != ok)
      TEST_FAILED("Input   : " + std::string(mytests[test][0]) + "\n\tExpected: " + ok +
                  "\n\tGot     : " + ret);
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void grid_north_wgs84()
{
  // For latlon itself north is always at 0
  Fmi::CoordinateTransformation trans("WGS84", "WGS84");

  auto result = Fmi::OGR::gridNorth(trans, 25, 60);
  if (!result)
    TEST_FAILED("Failed to establish WGS84 north at 25,60");
  if (*result != 0)
    TEST_FAILED("Expecting WGS84 north 0 at 25,60 but got " + std::to_string(*result));
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void grid_north_epsg_4326()
{
  Fmi::CoordinateTransformation trans("WGS84", "EPSG:4326");

  auto result = Fmi::OGR::gridNorth(trans, 25, 60);
  if (!result)
    TEST_FAILED("Failed to establish EPSG:4326 north at 25,60");
  if (*result != 0)
    TEST_FAILED("Expecting WGS84 north 0 at 25,60 but got " + std::to_string(*result));
  TEST_PASSED();
}
// ----------------------------------------------------------------------

void grid_north_epsga_4326()
{
  Fmi::CoordinateTransformation trans("WGS84", "EPSGA:4326");

  auto result = Fmi::OGR::gridNorth(trans, 25, 60);
  if (!result)
    TEST_FAILED("Failed to establish EPSGA:4326 north at 25,60");
  if (*result != 0)
    TEST_FAILED("Expecting WGS84 north 0 at 25,60 but got " + std::to_string(*result));
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void grid_north_epsg_3035()
{
  Fmi::CoordinateTransformation trans("WGS84", "EPSG:3035");

  // Helsinki
  auto result = Fmi::OGR::gridNorth(trans, 25, 60);
  auto expected = -12.762637;
  if (!result)
    TEST_FAILED("Failed to establish EPSG:3035 north at 25,60");
  if (std::abs(*result - expected) > 0.0001)
    TEST_FAILED("Expecting EPSG:3035 north " + std::to_string(expected) + " at 25,60 but got " +
                std::to_string(*result));

  // Stockholm
  result = Fmi::OGR::gridNorth(trans, 18, 60);
  expected = -6.815401;
  if (!result)
    TEST_FAILED("Failed to establish EPSG:3035 north at 18,60");
  if (std::abs(*result - expected) > 0.0001)
    TEST_FAILED("Expecting EPSG:3035 north " + std::to_string(expected) + " at 18,60 but got " +
                std::to_string(*result));

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void grid_north_epsg_3034()
{
  Fmi::CoordinateTransformation trans("WGS84", "EPSG:3034");

  // Helsinki
  auto result = Fmi::OGR::gridNorth(trans, 25, 60);
  auto expected = -11.630724;
  if (!result)
    TEST_FAILED("Failed to establish EPSG:3034 north at 25,60");
  if (std::abs(*result - expected) > 0.0001)
    TEST_FAILED("Expecting EPSG:3034 north " + std::to_string(expected) + " at 25,60 but got " +
                std::to_string(*result));

  // Stockholm
  result = Fmi::OGR::gridNorth(trans, 18, 60);
  expected = -6.203053;
  if (!result)
    TEST_FAILED("Failed to establish EPSG:3034 north at 18,60");
  if (std::abs(*result - expected) > 0.0001)
    TEST_FAILED("Expecting EPSG:3034 north " + std::to_string(expected) + " at 18,60 but got " +
                std::to_string(*result));

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void grid_north_smartmet_editor()
{
  Fmi::CoordinateTransformation trans("WGS84",
                                      "+proj=stere +lat_0=90 +lat_ts=60 +lon_0=20 +k=1 +x_0=0 "
                                      "+y_0=0 +a=6371220 +b=6371220 +units=m +no_defs");

  // Helsinki
  auto result = Fmi::OGR::gridNorth(trans, 25, 60);
  auto expected = -5;
  if (!result)
    TEST_FAILED("Failed to establish polster north at 25,60");
  if (std::abs(*result - expected) > 0.0001)
    TEST_FAILED("Expecting polster north " + std::to_string(expected) + " at 25,60 but got " +
                std::to_string(*result));

  // Stockholm
  result = Fmi::OGR::gridNorth(trans, 18, 60);
  expected = 2;
  if (!result)
    TEST_FAILED("Failed to establish polster north at 18,60");
  if (std::abs(*result - expected) > 0.0001)
    TEST_FAILED("Expecting polster north " + std::to_string(expected) + " at 18,60 but got " +
                std::to_string(*result));

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void grid_north_rotlatlon_proj()
{
  Fmi::CoordinateTransformation trans(
      "WGS84",
      "+proj=ob_tran +o_proj=eqc +o_lon_p=0 +o_lat_p=30 +lon_0=0 +R=6371220 +wktext +over "
      "+towgs84=0,0,0 +no_defs");

  // Helsinki
  auto result = Fmi::OGR::gridNorth(trans, 25, 60);
  auto expected = -21.499203;
  if (!result)
    TEST_FAILED("Failed to establish rotlatlon north at 25,60");
  if (std::abs(*result - expected) > 0.0001)
    TEST_FAILED("Expecting rotlatlon north " + std::to_string(expected) + " at 25,60 but got " +
                std::to_string(*result));

  // Stockholm
  result = Fmi::OGR::gridNorth(trans, 18, 60);
  expected = -15.527667;
  if (!result)
    TEST_FAILED("Failed to establish rotlatlon north at 18,60");
  if (std::abs(*result - expected) > 0.0001)
    TEST_FAILED("Expecting rotlatlon north " + std::to_string(expected) + " at 18,60 but got " +
                std::to_string(*result));

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void grid_north_rotlatlon_wkt()
{
  Fmi::CoordinateTransformation trans(
      "WGS84",
      R"(PROJCS["Plate_Carree",GEOGCS["FMI_Sphere",DATUM["FMI_2007",SPHEROID["FMI_Sphere",6371220,0]],PRIMEM["Greenwich",0],UNIT["Degree",0.017453292519943295]],PROJECTION["Plate_Carree"],EXTENSION["PROJ4","+proj=ob_tran +o_proj=longlat +o_lon_p=-180 +o_lat_p=30 +a=6371220 +k=1 +wktext"],UNIT["Meter",1]])");

  // This test is known to fail in GDAL 3.0, but may start working again in GDAL 3.1

  // Helsinki
  auto result = Fmi::OGR::gridNorth(trans, 25, 60);
  auto expected = -21.503683;
  if (!result)
    TEST_FAILED("Failed to establish rotlatlon north at 25,60");
  if (std::abs(*result - expected) > 0.0001)
    TEST_FAILED("Expecting rotlatlon north " + std::to_string(expected) + " at 25,60 but got " +
                std::to_string(*result));

  // Stockholm
  result = Fmi::OGR::gridNorth(trans, 18, 60);
  expected = -15.529383;
  if (!result)
    TEST_FAILED("Failed to establish rotlatlon north at 18,60");
  if (std::abs(*result - expected) > 0.0001)
    TEST_FAILED("Expecting rotlatlon north " + std::to_string(expected) + " at 18,60 but got " +
                std::to_string(*result));

  TEST_PASSED();
}  // namespace Tests

// Test driver
class tests : public tframe::tests
{
  // Overridden message separator
  virtual const char* error_message_prefix() const { return "\n\t"; }
  // Main test suite
  void test()
  {
    TEST(polyclip_case_hirlam);
    TEST(expand_geometry);
    TEST(exportToWkt_spatialreference);
    TEST(exportToSvg_precision);
    TEST(exportToSvg_wiki_examples);
    TEST(exportToProj);
    TEST(lineclip);
    TEST(polyclip);
    TEST(polyclip_segmentation);
    TEST(polyclip_spike);
    TEST(polyclip_case_hirlam);
    TEST(linecut);
    TEST(polycut);
    TEST(despeckle);
    TEST(despeckle_geography);
    TEST(grid_north_wgs84);
    TEST(grid_north_epsg_4326);
    TEST(grid_north_epsga_4326);
    TEST(grid_north_epsg_3035);
    TEST(grid_north_epsg_3034);
    TEST(grid_north_smartmet_editor);
    TEST(grid_north_rotlatlon_proj);
    TEST(grid_north_rotlatlon_wkt);
  }

};  // class tests

}  // namespace Tests

int main(void)
{
  cout << endl << "OGR tester" << endl << "==========" << endl;
  Tests::tests t;
  return t.run();
}
