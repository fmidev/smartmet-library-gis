#include "Box.h"
#include "OGR.h"
#include "TestDefs.h"
#include <gdal/ogr_geometry.h>
#include <regression/tframe.h>
#include <memory>

using namespace std;

const int precision = 2;

namespace Tests
{
// ----------------------------------------------------------------------

void exportToWkt_spatialreference()
{
  std::unique_ptr<OGRSpatialReference> srs(new OGRSpatialReference);
  OGRErr err = srs->SetFromUserInput("WGS84");
  if (err != OGRERR_NONE) TEST_FAILED("Failed to create spatial reference WGS84");

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
    if (err != OGRERR_NONE) TEST_FAILED("Failed to create spatial reference WGS84");

    std::string result = Fmi::OGR::exportToProj(*srs);

    if (result != "+proj=longlat +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +no_defs ")
      TEST_FAILED("Failed to export WGS84 spatial reference as PROJ, got '" + result + "'");
  }

  {
    std::unique_ptr<OGRSpatialReference> srs(new OGRSpatialReference);
    OGRErr err = srs->SetFromUserInput("+proj=latlong +datum=WGS84");
    if (err != OGRERR_NONE)
      TEST_FAILED("Failed to create spatial reference +proj=latlong +datum=WGS84");

    std::string result = Fmi::OGR::exportToProj(*srs);

    if (result != "+proj=longlat +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +no_defs ")
      TEST_FAILED("Failed to export +proj=latlong +datum=WGS84 spatial reference as PROJ, got '" +
                  result + "'");
  }

  {
    std::unique_ptr<OGRSpatialReference> srs(new OGRSpatialReference);
    OGRErr err = srs->SetFromUserInput("+init=epsg:4326");
    if (err != OGRERR_NONE) TEST_FAILED("Failed to create spatial reference +init=epsg:4326");

    std::string result = Fmi::OGR::exportToProj(*srs);

    // Result seems to depend on PROJ.4 version
    if (result != "+proj=longlat +datum=WGS84 +no_defs " &&
        result != "+proj=longlat +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +no_defs")
      TEST_FAILED("Failed to export +init=epsg:4326 spatial reference as PROJ, got '" + result +
                  "'");
  }

  {
    std::unique_ptr<OGRSpatialReference> srs(new OGRSpatialReference);
    OGRErr err = srs->SetFromUserInput("+init=epsg:4326");
    if (err != OGRERR_NONE) TEST_FAILED("Failed to create spatial reference +init=epsg:4326");

    std::string result = Fmi::OGR::exportToProj(*srs);

    // Second construction
    err = srs->SetFromUserInput(result.c_str());
    if (err != OGRERR_NONE)
      TEST_FAILED("Failed to create secondary spatial reference from +init=epsg:4326");

    result = Fmi::OGR::exportToProj(*srs);

    if (result != "+proj=longlat +ellps=WGS84 +towgs84=0,0,0,0,0,0,0 +no_defs ")
      TEST_FAILED("Failed to export +init=epsg:4326 spatial reference as PROJ, got '" + result +
                  "'");
  }

  {
    std::unique_ptr<OGRSpatialReference> srs(new OGRSpatialReference);
    OGRErr err = srs->SetFromUserInput("+init=epsg:2393");
    if (err != OGRERR_NONE) TEST_FAILED("Failed to create spatial reference +init=epsg:2393");

    std::string result = Fmi::OGR::exportToProj(*srs);

    if (result !=
        "+proj=tmerc +lat_0=0 +lon_0=27 +k=1 +x_0=3500000 +y_0=0 +ellps=intl "
        "+towgs84=-96.062,-82.428,-121.753,4.801,0.345,-1.376,1.496 +units=m +no_defs ")
      TEST_FAILED("Failed to export +init=epsg:2393 spatial reference as PROJ, got '" + result +
                  "'");
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void expand_geometry()
{
  // point Helsinki
  char* point = "POINT (24.9459 60.1921)";
  char* linestring = "LINESTRING (24.9459 60.1921, 24.9859 60.2921)";

  OGRGeometry* input_geom;
  OGRGeometry* output_geom;

  OGRSpatialReference srs;
  srs.importFromEPSGA(4326);

  OGRGeometryFactory::createFromWkt(&point, &srs, &input_geom);
  // circle around helsinki coordinates  with 1 km radius
  output_geom = Fmi::OGR::expandGeometry(input_geom, 1000);

  std::string result = Fmi::OGR::exportToWkt(*output_geom);
  // OGRGeometryFactory::destroyGeometry(output_geom); needed in the next test
  OGRGeometryFactory::destroyGeometry(input_geom);

  std::string ok =
      "POLYGON ((24.963866305682387 60.192099999998732,24.963810921557922 "
      "60.191398112406056,24.963645110646066 60.190700537264313,24.963369895226226 "
      "60.190011575769454,24.962986972092988 60.189335476275886,24.962498702094802 "
      "60.188676408087147,24.961908095578575 60.188038435728039,24.961218793829868 "
      "60.18742549385712,24.960435046623189 60.186841362975059,24.959561686020681 "
      "60.186289646079551,24.958604096580888 60.18577374641108,24.957568182161133 "
      "60.185296846428649,24.956460329518286 60.184861888145207,24.955287368932296 "
      "60.184471554945731,24.954056532095223 60.184128255000864,24.952775407525458 "
      "60.183834106379408,24.951451893781996 60.183590923952515,24.950094150767171 "
      "60.183400208171264,24.948710549418173 60.183263135787534,24.94730962009741 "
      "60.183180552576083,24.9459 60.183152968103443,24.944490379902589 "
      "60.183180552576083,24.943089450581827 60.183263135787534,24.941705849232829 "
      "60.183400208171264,24.940348106218003 60.183590923952515,24.939024592474542 "
      "60.183834106379408,24.937743467904777 60.184128255000864,24.936512631067703 "
      "60.184471554945731,24.935339670481714 60.184861888145207,24.934231817838871 "
      "60.185296846428649,24.933195903419112 60.18577374641108,24.932238313979319 "
      "60.186289646079551,24.931364953376811 60.186841362975059,24.930581206170132 "
      "60.18742549385712,24.929891904421424 60.188038435728039,24.929301297905194 "
      "60.188676408087147,24.928813027907012 60.189335476275886,24.928430104773774 "
      "60.190011575769454,24.928154889353934 60.190700537264313,24.927989078442078 "
      "60.191398112406056,24.927933694317609 60.192099999998732,24.927989078442078 "
      "60.192801872533131,24.928154889353934 60.193499402870913,24.928430104773774 "
      "60.194188290919257,24.928813027907012 60.194864290132301,24.929301297905194 "
      "60.19552323367575,24.929891904421424 60.196161060093978,24.930581206170132 "
      "60.196773838321974,24.931364953376811 60.197357791888464,24.932238313979319 "
      "60.197909322161927,24.933195903419112 60.198425030497077,24.934231817838871 "
      "60.198901739146201,24.935339670481714 60.199336510807591,24.936512631067703 "
      "60.199726666691504,24.937743467904777 60.200069802993447,24.939024592474542 "
      "60.200363805674023,24.940348106218003 60.200606863455619,24.941705849232829 "
      "60.200797478956346,24.943089450581827 60.200934477893568,24.944490379902589 "
      "60.201017016301051,24.9459 60.201044585715415,24.94730962009741 "
      "60.201017016301051,24.948710549418173 60.200934477893568,24.950094150767171 "
      "60.200797478956346,24.951451893781996 60.200606863455619,24.952775407525458 "
      "60.200363805674023,24.954056532095223 60.200069802993447,24.955287368932296 "
      "60.199726666691504,24.956460329518286 60.199336510807591,24.957568182161133 "
      "60.198901739146201,24.958604096580888 60.198425030497077,24.959561686020681 "
      "60.197909322161927,24.960435046623189 60.197357791888464,24.961218793829868 "
      "60.196773838321974,24.961908095578575 60.196161060093978,24.962498702094802 "
      "60.19552323367575,24.962986972092988 60.194864290132301,24.963369895226226 "
      "60.194188290919257,24.963645110646066 60.193499402870913,24.963810921557922 "
      "60.192801872533131,24.963866305682387 60.192099999998732))";

  if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);

  // expand circle another 1 km
  OGRGeometry* output_geom2 = Fmi::OGR::expandGeometry(output_geom, 1000);

  result = Fmi::OGR::exportToWkt(*output_geom2);
  OGRGeometryFactory::destroyGeometry(output_geom);
  OGRGeometryFactory::destroyGeometry(output_geom2);

  ok = "POLYGON ((24.981818759994205 60.19245120891479,24.981818759994205 "
       "60.19174878730977,24.981763375869743 60.191046892182072,24.981652693019107 "
       "60.190346605872563,24.981486882107248 60.189649008310795,24.981266198802597 "
       "60.188955175348632,24.980990983382757 60.188266177100175,24.980661660209961 "
       "60.187583076290352,24.980278737076723 60.186906926614036,24.979842804422727 "
       "60.186238771108627,24.979354534424541 60.185579640543111,24.978814679959111 "
       "60.18493055182546,24.978224073442888 60.184292506431049,24.97758362554821 "
       "60.183666488854485,24.976894323799499 60.183053465087774,24.976157231050294 "
       "60.182454381126128,24.975373483843615 60.181870161504087,24.974544290660827 "
       "60.181301707866197,24.973670930058326 60.180749897571481,24.972754748694676 "
       "60.180215582334519,24.97179715925488 60.17969958690739,24.970799638272602 "
       "60.179202707802837,24.96976372385285 60.178725712060256,24.968691013300379 "
       "60.178269336057539,24.967583160657533 60.177834284370562,24.966441874152874 "
       "60.177421228680828,24.965268913566881 60.177030806734507,24.964066087520006 "
       "60.176663621354415,24.962835250682932 60.176320239504967,24.961578300916724 "
       "60.176001191413128,24.960297176346959 60.175706969746365,24.958993852374828 "
       "60.175438028848369,24.957670338631363 60.175194784034375,24.956328675879092 "
       "60.174977610946989,24.954970932864271 60.174786844973283,24.953599203127858 "
       "60.174622780724512,24.952215601778857 60.174485671579092,24.95082226222987 "
       "60.174375729289196,24.9494213329091 60.174293123652241,24.948014973948549 "
       "60.174237982247455,24.94660535385114 60.174210390237761,24.945194646148863 "
       "60.174210390237761,24.943785026051451 60.174237982247455,24.942378667090889 "
       "60.174293123652241,24.940977737770123 60.174375729289196,24.93958439822114 "
       "60.174485671579092,24.938200796872142 60.174622780724512,24.936829067135747 "
       "60.174786844973283,24.935471324120918 60.174977610946989,24.93412966136863 "
       "60.175194784034375,24.932806147625168 60.175438028848369,24.931502823653016 "
       "60.175706969746365,24.930221699083255 60.176001191413128,24.928964749317085 "
       "60.176320239504967,24.927733912480011 60.176663621354415,24.926531086433116 "
       "60.177030806734507,24.925358125847122 60.177421228680828,24.924216839342439 "
       "60.177834284370562,24.923108986699596 60.178269336057539,24.9220362761472 "
       "60.178725712060221,24.921000361727437 60.179202707802808,24.92000284074506 "
       "60.179699586907411,24.919045251305274 60.18021558233454,24.918129069941699 "
       "60.180749897571467,24.917255709339191 60.181301707866197,24.916426516156378 "
       "60.181870161504087,24.915642768949699 60.182454381126128,24.914905676200497 "
       "60.183053465087774,24.91421637445179 60.183666488854485,24.913575926557129 "
       "60.184292506431042,24.912985320040903 60.18493055182546,24.912445465575431 "
       "60.185579640543132,24.911957195577248 60.186238771108641,24.911521262923259 "
       "60.186906926614057,24.911138339790021 60.18758307629038,24.910809016617254 "
       "60.188266177100161,24.910533801197413 60.18895517534861,24.910313117892748 "
       "60.189649008310795,24.910147306980885 60.190346605872563,24.910036624130253 "
       "60.191046892182008,24.909981240005788 60.191748787309692,24.909981240005788 "
       "60.192451208914839,24.910036624130253 60.193153073914367,24.910147306980878 "
       "60.193853300153243,24.910313117892738 60.194550808072492,24.910533801197438 "
       "60.195244522372882,24.910809016617279 60.195933373671622,24.911138339790007 "
       "60.19661630014987,24.911521262923245 60.197292249187797,24.911957195577248 "
       "60.197960178984879,24.912445465575431 60.198619060163644,24.912985320040903 "
       "60.199267877353726,24.913575926557129 60.199905630753776,24.914216374451737 "
       "60.200531337669382,24.914905676200448 60.201144034023983,24.915642768949898 "
       "60.201742775841097,24.916426516156577 60.202326640695205,24.917255709339027 "
       "60.202894729129071,24.918129069941536 60.203446166035683,24.919045251305171 "
       "60.203980102002319,24.920002840744964 60.204495714615184,24.921000361727437 "
       "60.20499220972183,24.9220362761472 60.205468822650161,24.923108986699468 "
       "60.20592481938246,24.924216839342307 60.206359497681348,24.925358125847712 "
       "60.206772188167534,24.926531086433705 60.207162255345928,24.927733912479852 "
       "60.20752909858107,24.928964749316926 60.207872153018407,24.930221699083081 "
       "60.20819089044933,24.931502823652838 60.20848482012174,24.932806147625168 "
       "60.20875348949221,24.93412966136863 60.208996484919446,24.935471324121117 "
       "60.209213432298483,24.936829067135942 60.2094039976338,24.938200796872142 "
       "60.20956788755111,24.93958439822114 60.209704849746842,24.940977737769703 "
       "60.209814673374808,24.942378667090466 60.209897189369237,24.943785026051664 "
       "60.209952270703539,24.945194646149076 60.20997983258507,24.946605353850927 "
       "60.20997983258507,24.948014973948336 60.209952270703539,24.949421332909523 "
       "60.209897189369237,24.95082226223029 60.209814673374808,24.952215601778857 "
       "60.209704849746842,24.953599203127858 60.20956788755111,24.954970932864068 "
       "60.2094039976338,24.95632867587889 60.209213432298483,24.957670338631363 "
       "60.208996484919446,24.958993852374828 60.20875348949221,24.960297176347133 "
       "60.20848482012174,24.961578300916898 60.20819089044933,24.962835250683096 "
       "60.207872153018407,24.96406608752017 60.207529098581055,24.965268913566291 "
       "60.207162255345928,24.966441874152284 60.206772188167534,24.967583160657664 "
       "60.206359497681348,24.968691013300507 60.20592481938246,24.96976372385285 "
       "60.205468822650147,24.970799638272602 60.204992209721809,24.971797159254976 "
       "60.204495714615213,24.972754748694772 60.20398010200234,24.973670930058489 "
       "60.203446166035675,24.974544290660994 60.202894729129056,24.97537348384342 "
       "60.202326640695205,24.976157231050099 60.201742775841097,24.976894323799549 "
       "60.201144034023983,24.977583625548256 60.200531337669382,24.978224073442888 "
       "60.199905630753747,24.978814679959111 60.199267877353705,24.979354534424541 "
       "60.198619060163658,24.979842804422727 60.197960178984907,24.980278737076741 "
       "60.197292249187818,24.980661660209982 60.196616300149898,24.980990983382735 "
       "60.195933373671593,24.981266198802572 60.195244522372853,24.981486882107262 "
       "60.194550808072492,24.981652693019122 60.193853300153243,24.981763375869743 "
       "60.193153073914317,24.981818759994205 60.19245120891479))";

  if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);

  OGRGeometryFactory::createFromWkt(&linestring, &srs, &input_geom);

  // expand line by 1 km
  output_geom = Fmi::OGR::expandGeometry(input_geom, 1000);

  result = Fmi::OGR::exportToWkt(*output_geom);

  OGRGeometryFactory::destroyGeometry(output_geom);
  OGRGeometryFactory::destroyGeometry(input_geom);

  ok = "POLYGON ((24.977089374263791 60.292969736329617,24.977254003888774 "
       "60.293310191711598,24.977471938954867 60.293643182517485,24.97774183581992 "
       "60.293966656007932,24.978062030480004 60.294278618173763,24.978430548828566 "
       "60.294577146021958,24.978845118827451 60.294860399422788,24.979303184514748 "
       "60.295126632445069,24.979801921763176 60.295374204110118,24.980338255691748 "
       "60.295601588498194,24.980908879623453 60.29580738414532,24.981510275471994 "
       "60.295990322673198,24.982138735431981 60.296149276598648,24.982790384838776 "
       "60.296283266275374,24.983461206057097 60.296391465924927,24.984147063251047 "
       "60.296473208720251,24.984843727882971 60.296527990890446,24.985546904783742 "
       "60.296555474821794,24.986252258633943 60.296555491135905,24.986955440692523 "
       "60.296528039732216,24.98765211560826 60.296473289788665,24.98833798814864 "
       "60.296391578720403,24.989008829681371 60.296283410102731,24.989660504245322 "
       "60.296149450571512,24.990288994050108 60.295990525719347,24.990890424247084 "
       "60.295807615013388,24.991461086819093 60.295601845765248,24.991997463441617 "
       "60.295374486190546,24.992496247174408 60.295126937600287,24.99295436284989 "
       "60.294860725771898,24.993368986032596 60.294577491553461,24.993737560432717 "
       "60.294278980757809,24.994057813666519 60.293967033409523,24.99432777126632 "
       "60.293643572410204,24.99454576885374 60.293310591691984,24.994710462401169 "
       "60.292970143931917,24.994820836518119 60.292624327902992,24.99487621071146 "
       "60.292275275539417,24.994876243580869 60.291925138795861,24.994820934923698 "
       "60.29157607638173,24.994710625736211 60.291230240451959,24.954710625736212 "
       "60.191227572446167,24.954545996111225 60.190886050916127,24.954328061045135 "
       "60.190552010436903,24.954058164180083 60.19022751073939,24.953737969519999 "
       "60.189914552785183,24.953369451171433 60.189615066422384,24.952954881172548 "
       "60.189330898478538,24.952496815485254 60.189063801364632,24.951998078236823 "
       "60.188815422260618,24.951461744308251 60.188587292949052,24.950891120376546 "
       "60.188380820360429,24.950289724528009 60.188197277888065,24.949661264568018 "
       "60.188037797526995,24.949009615161224 60.187903362884924,24.948338793942906 "
       "60.187794803109327,24.947652936748948 60.187712787767637,24.946956272117031 "
       "60.187657822712822,24.94625309521626 60.1876302469596,24.945547741366056 "
       "60.187630230590976,24.94484455930748 60.187657773707933,24.944147884391743 "
       "60.187712706428719,24.943462011851363 60.187794689938045,24.942791170318632 "
       "60.187903218579329,24.942139495754677 60.188037622977099,24.941511005949895 "
       "60.188197074170446,24.940909575752919 60.188380588731498,24.940338913180906 "
       "60.188587034837383,24.939802536558386 60.188815139258047,24.939303752825595 "
       "60.18906349521653,24.938845637150109 60.189330571072887,24.938431013967406 "
       "60.189614719778284,24.938062439567286 60.189914189040287,24.937742186333484 "
       "60.190227132136741,24.937472228733682 60.190551619311144,24.937254231146255 "
       "60.190885649678997,24.937089537598833 60.191227163571703,24.93697916348188 "
       "60.191574055241524,24.936923789288539 60.191924185849089,24.936923756419134 "
       "60.192275396653507,24.936979065076301 60.19262552232334,24.937089374263792 "
       "60.192972404286671,24.977089374263791 60.292969736329617))";

  if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void exportToSvg_wiki_examples()
{
  using Fmi::Box;
  using Fmi::OGR::exportToSvg;

  // Ref: http://en.wikipedia.org/wiki/Well-known_text

  char* point = "POINT (30 10)";
  char* linestring = "LINESTRING (30 10, 10 30, 40 40)";
  char* polygon1 = "POLYGON ((30 10, 40 40, 20 40, 10 20, 30 10))";
  char* polygon2 = "POLYGON ((35 10, 45 45, 15 40, 10 20, 35 10),(20 30, 35 35, 30 20, 20 30))";
  char* multipoint1 = "MULTIPOINT ((10 40), (40 30), (20 20), (30 10))";
  char* multipoint2 = "MULTIPOINT (10 40, 40 30, 20 20, 30 10)";
  char* multilinestring = "MULTILINESTRING ((10 10, 20 20, 10 40),(40 40, 30 30, 40 20, 30 10))";
  char* multipolygon1 =
      "MULTIPOLYGON (((30 20, 45 40, 10 40, 30 20)),((15 5, 40 10, 10 20, 5 10, 15 5)))";
  char* multipolygon2 =
      "MULTIPOLYGON (((40 40, 20 45, 45 30, 40 40)),((20 35, 10 30, 10 10, 30 5, 45 20, 20 35),(30 "
      "20, 20 15, 20 25, 30 20)))";

  Box box = Box::identity();  // identity transformation

  OGRGeometry* geom;
  {
    OGRGeometryFactory::createFromWkt(&point, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M30 10";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(&linestring, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M30 10 10 30 40 40";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(&polygon1, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M30 10 40 40 20 40 10 20Z";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(&polygon2, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M35 10 45 45 15 40 10 20ZM20 30 35 35 30 20Z";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(&multipoint1, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M10 40M40 30M20 20M30 10";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(&multipoint2, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M10 40M40 30M20 20M30 10";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(&multilinestring, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M10 10 20 20 10 40M40 40 30 30 40 20 30 10";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(&multipolygon1, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M30 20 45 40 10 40ZM15 5 40 10 10 20 5 10Z";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

#if 0
	// Note: Using multipolygon2 twice will crash
	{
	  OGRGeometryFactory::createFromWkt(&multipolygon2,NULL,&geom);
	  string result = exportToSvg(*geom, box, precision);
	  OGRGeometryFactory::destroyGeometry(geom);
	  string ok = "M40 40 20 45 45 30ZM20 35 10 30 10 10 30 5 45 20ZM30 20 20 15 20 25Z";
	  if(result != ok)
		TEST_FAILED("Expected: "+ok+"\n\tObtained: "+result);
	}
#endif

  // Finally test integer precision too
  {
    OGRGeometryFactory::createFromWkt(&multipolygon2, NULL, &geom);
    string result = exportToSvg(*geom, box, 0);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M40 40 20 45 45 30ZM20 35 10 30 10 10 30 5 45 20ZM30 20 20 15 20 25Z";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
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
      if (result != ok) TEST_FAILED("Precision 2:\n\tExpected: " + ok + "\n\tObtained: " + result);
    }

    {
      string ok =
          "M-0.1 -0.1 0 0 0 0.1 0.1 0.1 0.1 0.1 0.2 0.2 0.2 0.2 0.3 0.3 0.3 0.3 0.4 0.4 0.4 0.5 "
          "0.5 "
          "0.5 0.5 0.6 0.6 0.6 0.6 0.7 0.7 0.7 0.7 0.8 0.8 0.8 0.8 0.8 0.9 0.9 0.9 0.9 1 1";
      string result = exportToSvg(*line, Box::identity(), 1.0);
      if (result != ok) TEST_FAILED("Precision 1:\n\tExpected: " + ok + "\n\tObtained: " + result);
    }

    {
      string ok = "M-0.1 -0.1 -0.1 -0.1 0.1 0.1 0.3 0.3 0.5 0.5 0.7 0.7 0.9 0.9";
      string result = exportToSvg(*line, Box::identity(), 0.7);
      if (result != ok)
        TEST_FAILED("Precision 0.7:\n\tExpected: " + ok + "\n\tObtained: " + result);
    }

    {
      string ok = "M-0.1 -0.1 0.2 0.2 0.5 0.5 0.8 0.8";
      string result = exportToSvg(*line, Box::identity(), 0.5);
      if (result != ok)
        TEST_FAILED("Precision 0.5:\n\tExpected: " + ok + "\n\tObtained: " + result);
    }

    {
      string ok = "M-0.1 -0.1 0.4 0.4";
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
  using OGR::lineclip;

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
      {"POLYGON ((-2 -2,-2 5,5 5,5 -2,-2 -2), (3 3,4 4,4 2,3 3))",
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

      // All triangles fully inside
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
       "POLYGON ((1 1,9 1,9 9,1 9,1 1))"}

  };

  for (int test = 0; test < ntests; ++test)
  {
    OGRGeometry* input;
    OGRGeometry* output;

    char* wkt = mytests[test][0];
    string ok = mytests[test][1];

    try
    {
      auto err = OGRGeometryFactory::createFromWkt(&wkt, NULL, &input);
      if (err != OGRERR_NONE) TEST_FAILED("Failed to parse input " + std::string(wkt));
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
      TEST_FAILED("Input   : " + std::string(mytests[test][0]) + "\n\tExpected: " + ok +
                  "\n\tGot     : " + ret);
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void polyclip()
{
  using namespace Fmi;
  using Fmi::Box;
  using OGR::exportToWkt;
  using OGR::lineclip;

  Box box(0, 0, 10, 10, 10, 10);  // 0,0-->10,10 with irrelevant transformation sizes

  int ntests = 69;
  char* mytests[69][2] = {
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
      {
          "POLYGON ((0 0,5 10,10 0,0 0))",
          "POLYGON ((0 0,5 10,10 0,0 0))",
      },
      // Same triangle with another starting point
      {
          "POLYGON ((5 10,10 0,0 0,5 10))",
          "POLYGON ((0 0,5 10,10 0,0 0))",
      },
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
      {"POLYGON ((-15 -15,-15 15,15 15,15 -15,-15 -15),(-5 5,-6 5,-6 6,-5 6,-5 5))",
       "POLYGON ((0 0,0 10,10 10,10 0,0 0))"},
      // Surround the rectangle, hole outside rectangle but shares edge
      {"POLYGON ((-15 -15,-15 15,15 15,15 -15,-15 -15),(0 5,-1 5,-1 6,0 6,0 5))",
       "POLYGON ((0 0,0 10,10 10,10 0,0 0))"},
      // Polygon with hole, box intersects both
      {"POLYGON ((-5 1,-5 9,5 9,5 1,-5 1),(-4 8,-4 2,4 2,4 8,-4 8)))",
       "POLYGON ((0 1,0 2,4 2,4 8,0 8,0 9,5 9,5 1,0 1))"},
      // Polygon with hole, box intersects both - variant 2
      {"POLYGON ((-15 1,-15 15,15 15,15 1,-5 1),(-1 3,-1 2,1 2,1 3,-1 3))",
       "POLYGON ((0 1,0 2,1 2,1 3,0 3,0 10,10 10,10 1,0 1))"}};

  for (int test = 0; test < ntests; ++test)
  {
    OGRGeometry* input;
    OGRGeometry* output;

    char* wkt = mytests[test][0];
    string ok = mytests[test][1];

    // std::cerr << "\nTEST: " << std::string(wkt) << std::endl;
    try
    {
      auto err = OGRGeometryFactory::createFromWkt(&wkt, NULL, &input);
      if (err != OGRERR_NONE) TEST_FAILED("Failed to parse input " + std::string(wkt));
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
      TEST_FAILED("Input   : " + std::string(mytests[test][0]) + "\n\tExpected: " + ok +
                  "\n\tGot     : " + ret);
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void clip_spike()
{
  using namespace Fmi;
  using Fmi::Box;
  using OGR::exportToWkt;
  using OGR::lineclip;

  Box box(11000000, 0, 11250000, 250000, 100, 100);  // last two are irrelevant

  char* wkt = "POLYGON ((11076289 55660,11131949 55660,11131949 -1e-100,11076289 55660))";
  std::string ok = "POLYGON ((11131949 0,11076289 55660,11131949 55660,11131949 0))";

  OGRGeometry* input;
  OGRGeometry* output;

  try
  {
    auto err = OGRGeometryFactory::createFromWkt(&wkt, NULL, &input);
    if (err != OGRERR_NONE) TEST_FAILED("Failed to parse input " + std::string(wkt));
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
    TEST_FAILED("Input   : " + std::string(wkt) + "\n\tExpected: " + ok + "\n\tGot     : " + ret);

  TEST_PASSED();
}

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

    char* wkt = mytests[test][0];
    string ok = mytests[test][1];

    // std::cerr << "\nTEST: " << std::string(wkt) << std::endl;
    try
    {
      auto err = OGRGeometryFactory::createFromWkt(&wkt, NULL, &input);
      if (err != OGRERR_NONE) TEST_FAILED("Failed to parse input " + std::string(wkt));
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

    char* wkt = mytests[test][0];
    string ok = mytests[test][1];

    // std::cerr << "\nTEST: " << std::string(wkt) << std::endl;
    try
    {
      auto err = OGRGeometryFactory::createFromWkt(&wkt, &wgs84, &input);
      if (err != OGRERR_NONE) TEST_FAILED("Failed to parse input " + std::string(wkt));
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

void grid_north()
{
  std::unique_ptr<OGRSpatialReference> wgs84(new OGRSpatialReference);
  OGRErr err = wgs84->SetFromUserInput("WGS84");
  if (err != OGRERR_NONE) TEST_FAILED("Failed to create spatial reference WGS84");

  // For latlon itself north is always at 0
  {
    std::unique_ptr<OGRCoordinateTransformation> trans(
        OGRCreateCoordinateTransformation(wgs84.get(), wgs84.get()));

    auto result = Fmi::OGR::gridNorth(*trans, 25, 60);
    if (!result) TEST_FAILED("Failed to establish WGS84 north at 25,60");
    if (*result != 0)
      TEST_FAILED("Expecting WGS84 north 0 at 25,60 but got " + std::to_string(*result));
  }

  // EPSG:3035 ETRS-LAEA
  {
    std::unique_ptr<OGRSpatialReference> epsg(new OGRSpatialReference);
    err = epsg->SetFromUserInput("EPSG:3035");
    if (err != OGRERR_NONE) TEST_FAILED("Failed to create spatial reference EPSG:3035");

    std::unique_ptr<OGRCoordinateTransformation> trans(
        OGRCreateCoordinateTransformation(wgs84.get(), epsg.get()));

    // Helsinki
    auto result = Fmi::OGR::gridNorth(*trans, 25, 60);
    auto expected = -12.762637;
    if (!result) TEST_FAILED("Failed to establish EPSG:3035 north at 25,60");
    if (std::abs(*result - expected) > 0.0001)
      TEST_FAILED("Expecting EPSG:3035 north " + std::to_string(expected) + " at 25,60 but got " +
                  std::to_string(*result));

    // Stockholm
    result = Fmi::OGR::gridNorth(*trans, 18, 60);
    expected = -6.815401;
    if (!result) TEST_FAILED("Failed to establish EPSG:3035 north at 18,60");
    if (std::abs(*result - expected) > 0.0001)
      TEST_FAILED("Expecting EPSG:3035 north " + std::to_string(expected) + " at 18,60 but got " +
                  std::to_string(*result));
  }

  // EPSG:3034 ETRS-LCC
  {
    std::unique_ptr<OGRSpatialReference> epsg(new OGRSpatialReference);
    err = epsg->SetFromUserInput("EPSG:3034");
    if (err != OGRERR_NONE) TEST_FAILED("Failed to create spatial reference EPSG:3034");

    std::unique_ptr<OGRCoordinateTransformation> trans(
        OGRCreateCoordinateTransformation(wgs84.get(), epsg.get()));

    // Helsinki
    auto result = Fmi::OGR::gridNorth(*trans, 25, 60);
    auto expected = -11.630724;
    if (!result) TEST_FAILED("Failed to establish EPSG:3034 north at 25,60");
    if (std::abs(*result - expected) > 0.0001)
      TEST_FAILED("Expecting EPSG:3034 north " + std::to_string(expected) + " at 25,60 but got " +
                  std::to_string(*result));

    // Stockholm
    result = Fmi::OGR::gridNorth(*trans, 18, 60);
    expected = -6.203053;
    if (!result) TEST_FAILED("Failed to establish EPSG:3034 north at 18,60");
    if (std::abs(*result - expected) > 0.0001)
      TEST_FAILED("Expecting EPSG:3034 north " + std::to_string(expected) + " at 18,60 but got " +
                  std::to_string(*result));
  }

  // Polar stereographic for Scandinavian maps
  {
    std::unique_ptr<OGRSpatialReference> epsg(new OGRSpatialReference);
    err = epsg->SetFromUserInput(
        "+proj=stere +lat_0=90 +lat_ts=60 +lon_0=20 +k=1 +x_0=0 +y_0=0 +a=6371220 +b=6371220 "
        "+units=m +no_defs");
    if (err != OGRERR_NONE) TEST_FAILED("Failed to create polar stereographic spatial reference");

    std::unique_ptr<OGRCoordinateTransformation> trans(
        OGRCreateCoordinateTransformation(wgs84.get(), epsg.get()));

    // Helsinki
    auto result = Fmi::OGR::gridNorth(*trans, 25, 60);
    auto expected = -5;
    if (!result) TEST_FAILED("Failed to establish polster north at 25,60");
    if (std::abs(*result - expected) > 0.0001)
      TEST_FAILED("Expecting polster north " + std::to_string(expected) + " at 25,60 but got " +
                  std::to_string(*result));

    // Stockholm
    result = Fmi::OGR::gridNorth(*trans, 18, 60);
    expected = 2;
    if (!result) TEST_FAILED("Failed to establish polster north at 18,60");
    if (std::abs(*result - expected) > 0.0001)
      TEST_FAILED("Expecting polster north " + std::to_string(expected) + " at 18,60 but got " +
                  std::to_string(*result));
  }

  // Rotated latlon coordinates used in HIRLAM weather model
  {
    std::string tmp =
        R"(PROJCS["Plate_Carree",GEOGCS["FMI_Sphere",DATUM["FMI_2007",SPHEROID["FMI_Sphere",6371220,0]],PRIMEM["Greenwich",0],UNIT["Degree",0.017453292519943295]],PROJECTION["Plate_Carree"],EXTENSION["PROJ4","+proj=ob_tran +o_proj=longlat +o_lon_p=-180 +o_lat_p=30 +a=6371220 +k=1 +wktext"],UNIT["Meter",1]])";

    std::unique_ptr<OGRSpatialReference> epsg(new OGRSpatialReference);
    err = epsg->SetFromUserInput(tmp.c_str());
    if (err != OGRERR_NONE) TEST_FAILED("Failed to create rotlatlon spatial reference");

    std::unique_ptr<OGRCoordinateTransformation> trans(
        OGRCreateCoordinateTransformation(wgs84.get(), epsg.get()));

    // Helsinki
    auto result = Fmi::OGR::gridNorth(*trans, 25, 60);
    auto expected = -21.503683;
    if (!result) TEST_FAILED("Failed to establish rotlatlon north at 25,60");
    if (std::abs(*result - expected) > 0.0001)
      TEST_FAILED("Expecting rotlatlon north " + std::to_string(expected) + " at 25,60 but got " +
                  std::to_string(*result));

    // Stockholm
    result = Fmi::OGR::gridNorth(*trans, 18, 60);
    expected = -15.529383;
    if (!result) TEST_FAILED("Failed to establish rotlatlon north at 18,60");
    if (std::abs(*result - expected) > 0.0001)
      TEST_FAILED("Expecting rotlatlon north " + std::to_string(expected) + " at 18,60 but got " +
                  std::to_string(*result));
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
    TEST(expand_geometry);
    TEST(exportToWkt_spatialreference);
    TEST(exportToSvg_precision);
    TEST(exportToSvg_wiki_examples);
    TEST(exportToProj);
    TEST(lineclip);
    TEST(polyclip);
    TEST(despeckle);
    TEST(despeckle_geography);
    TEST(grid_north);
    TEST(clip_spike);
  }

};  // class tests

}  // namespace Tests

int main(void)
{
  cout << endl << "OGR tester" << endl << "==========" << endl;
  Tests::tests t;
  return t.run();
}
