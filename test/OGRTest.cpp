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

  OGRGeometryFactory::createFromWkt(point, &srs, &input_geom);
  // circle around helsinki coordinates  with 1 km radius
  output_geom = Fmi::OGR::expandGeometry(input_geom, 1000);

  std::string result = Fmi::OGR::exportToWkt(*output_geom);
  // OGRGeometryFactory::destroyGeometry(output_geom); needed in the next test
  OGRGeometryFactory::destroyGeometry(input_geom);

  std::string ok =
      "POLYGON ((24.9638663056824 60.1920999999987,24.9638109215579 "
      "60.1913981124061,24.9636451106461 60.1907005372643,24.9633698952262 "
      "60.1900115757695,24.962986972093 60.1893354762759,24.9624987020948 "
      "60.1886764080872,24.9619080955786 60.188038435728,24.9612187938299 "
      "60.1874254938571,24.9604350466232 60.1868413629751,24.9595616860207 "
      "60.1862896460795,24.9586040965809 60.1857737464111,24.9575681821611 "
      "60.1852968464286,24.9564603295183 60.1848618881452,24.9552873689323 "
      "60.1844715549457,24.9540565320952 60.1841282550009,24.9527754075255 "
      "60.1838341063794,24.951451893782 60.1835909239525,24.9500941507672 "
      "60.1834002081713,24.9487105494182 60.1832631357875,24.9473096200974 "
      "60.1831805525761,24.9459 60.1831529681034,24.9444903799026 "
      "60.1831805525761,24.9430894505818 60.1832631357875,24.9417058492328 "
      "60.1834002081713,24.940348106218 60.1835909239525,24.9390245924745 "
      "60.1838341063794,24.9377434679048 60.1841282550009,24.9365126310677 "
      "60.1844715549457,24.9353396704817 60.1848618881452,24.9342318178389 "
      "60.1852968464286,24.9331959034191 60.1857737464111,24.9322383139793 "
      "60.1862896460795,24.9313649533768 60.1868413629751,24.9305812061701 "
      "60.1874254938571,24.9298919044214 60.188038435728,24.9293012979052 "
      "60.1886764080872,24.928813027907 60.1893354762759,24.9284301047738 "
      "60.1900115757695,24.9281548893539 60.1907005372643,24.9279890784421 "
      "60.1913981124061,24.9279336943176 60.1920999999987,24.9279890784421 "
      "60.1928018725331,24.9281548893539 60.1934994028709,24.9284301047738 "
      "60.1941882909193,24.928813027907 60.1948642901323,24.9293012979052 "
      "60.1955232336757,24.9298919044214 60.196161060094,24.9305812061701 "
      "60.196773838322,24.9313649533768 60.1973577918885,24.9322383139793 "
      "60.1979093221619,24.9331959034191 60.1984250304971,24.9342318178389 "
      "60.1989017391462,24.9353396704817 60.1993365108076,24.9365126310677 "
      "60.1997266666915,24.9377434679048 60.2000698029934,24.9390245924745 "
      "60.200363805674,24.940348106218 60.2006068634556,24.9417058492328 "
      "60.2007974789563,24.9430894505818 60.2009344778936,24.9444903799026 "
      "60.2010170163011,24.9459 60.2010445857154,24.9473096200974 "
      "60.2010170163011,24.9487105494182 60.2009344778936,24.9500941507672 "
      "60.2007974789563,24.951451893782 60.2006068634556,24.9527754075255 "
      "60.200363805674,24.9540565320952 60.2000698029934,24.9552873689323 "
      "60.1997266666915,24.9564603295183 60.1993365108076,24.9575681821611 "
      "60.1989017391462,24.9586040965809 60.1984250304971,24.9595616860207 "
      "60.1979093221619,24.9604350466232 60.1973577918885,24.9612187938299 "
      "60.196773838322,24.9619080955786 60.196161060094,24.9624987020948 "
      "60.1955232336757,24.962986972093 60.1948642901323,24.9633698952262 "
      "60.1941882909193,24.9636451106461 60.1934994028709,24.9638109215579 "
      "60.1928018725331,24.9638663056824 60.1920999999987))";

  if (result != ok)
    TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);

  // expand circle another 1 km
  OGRGeometry* output_geom2 = Fmi::OGR::expandGeometry(output_geom, 1000);

  result = Fmi::OGR::exportToWkt(*output_geom2);
  OGRGeometryFactory::destroyGeometry(output_geom);
  OGRGeometryFactory::destroyGeometry(output_geom2);

  ok = "POLYGON ((24.9818187599942 60.1924512089148,24.9818187599942 "
       "60.1917487873097,24.9817633758697 60.1910468921821,24.9816526930191 "
       "60.1903466058727,24.9814868821073 60.1896490083109,24.9812661988026 "
       "60.1889551753486,24.9809909833827 60.1882661771001,24.98066166021 "
       "60.1875830762904,24.9802787370767 60.1869069266141,24.9798428044228 "
       "60.1862387711086,24.9793545344246 60.1855796405431,24.9788146799591 "
       "60.1849305518255,24.9782240734429 60.184292506431,24.9775836255482 "
       "60.1836664888545,24.9768943237995 60.1830534650878,24.9761572310503 "
       "60.1824543811261,24.9753734838436 60.1818701615041,24.9745442906608 "
       "60.1813017078662,24.9736709300583 60.1807498975715,24.9727547486946 "
       "60.1802155823345,24.9717971592548 60.1796995869074,24.9707996382726 "
       "60.1792027078028,24.9697637238528 60.1787257120602,24.9686910133006 "
       "60.1782693360576,24.9675831606578 60.1778342843706,24.9664418741527 "
       "60.1774212286808,24.9652689135667 60.1770308067345,24.96406608752 "
       "60.1766636213544,24.9628352506829 60.176320239505,24.9615783009166 "
       "60.1760011914131,24.9602971763468 60.1757069697463,24.9589938523752 "
       "60.1754380288484,24.9576703386317 60.1751947840344,24.9563286758789 "
       "60.174977610947,24.9549709328641 60.1747868449732,24.9535992031281 "
       "60.1746227807245,24.9522156017791 60.1744856715791,24.9508222622297 "
       "60.1743757292892,24.9494213329089 60.1742931236522,24.9480149739485 "
       "60.1742379822474,24.9466053538511 60.1742103902378,24.9451946461489 "
       "60.1742103902378,24.9437850260515 60.1742379822474,24.9423786670911 "
       "60.1742931236522,24.9409777377703 60.1743757292892,24.9395843982209 "
       "60.1744856715791,24.9382007968719 60.1746227807245,24.9368290671359 "
       "60.1747868449732,24.9354713241211 60.174977610947,24.9341296613683 "
       "60.1751947840344,24.9328061476248 60.1754380288484,24.9315028236532 "
       "60.1757069697463,24.9302216990834 60.1760011914131,24.9289647493171 "
       "60.176320239505,24.92773391248 60.1766636213544,24.9265310864333 "
       "60.1770308067345,24.9253581258473 60.1774212286807,24.9242168393422 "
       "60.1778342843706,24.9231089866994 60.1782693360576,24.9220362761471 "
       "60.1787257120602,24.9210003617274 60.1792027078029,24.9200028407452 "
       "60.1796995869073,24.9190452513054 60.1802155823345,24.9181290699417 "
       "60.1807498975715,24.9172557093392 60.1813017078662,24.9164265161564 "
       "60.1818701615041,24.9156427689497 60.1824543811261,24.9149056762005 "
       "60.1830534650878,24.9142163744517 60.1836664888545,24.9135759265571 "
       "60.1842925064311,24.9129853200409 60.1849305518255,24.9124454655754 "
       "60.1855796405431,24.9119571955773 60.1862387711086,24.9115212629233 "
       "60.186906926614,24.91113833979 60.1875830762904,24.9108090166173 "
       "60.1882661771001,24.9105338011974 60.1889551753486,24.9103131178927 "
       "60.1896490083109,24.9101473069809 60.1903466058727,24.9100366241303 "
       "60.1910468921821,24.9099812400058 60.1917487873097,24.9099812400058 "
       "60.1924512089148,24.9100366241303 60.1931530739144,24.9101473069809 "
       "60.1938533001532,24.9103131178927 60.1945508080725,24.9105338011974 "
       "60.1952445223729,24.9108090166173 60.1959333736716,24.9111383397901 "
       "60.1966163001499,24.9115212629233 60.1972922491879,24.9119571955772 "
       "60.1979601789848,24.9124454655754 60.1986190601636,24.9129853200409 "
       "60.1992678773537,24.9135759265571 60.1999056307538,24.9142163744516 "
       "60.2005313376693,24.9149056762003 60.2011440340239,24.9156427689498 "
       "60.201742775841,24.9164265161565 60.2023266406952,24.9172557093389 "
       "60.202894729129,24.9181290699415 60.2034461660356,24.9190452513054 "
       "60.2039801020025,24.9200028407452 60.2044957146153,24.9210003617274 "
       "60.2049922097218,24.9220362761471 60.2054688226501,24.9231089866996 "
       "60.2059248193825,24.9242168393425 60.2063594976814,24.9253581258473 "
       "60.2067721881674,24.9265310864333 60.2071622553458,24.92773391248 "
       "60.2075290985811,24.9289647493171 60.2078721530185,24.9302216990831 "
       "60.2081908904494,24.9315028236528 60.2084848201217,24.9328061476256 "
       "60.2087534894923,24.934129661369 60.2089964849195,24.9354713241211 "
       "60.2092134322985,24.9368290671359 60.2094039976338,24.9382007968715 "
       "60.209567887551,24.9395843982205 60.2097048497468,24.9409777377701 "
       "60.2098146733748,24.9423786670909 60.2098971893693,24.9437850260515 "
       "60.2099522707035,24.9451946461489 60.2099798325851,24.9466053538511 "
       "60.2099798325851,24.9480149739485 60.2099522707035,24.9494213329091 "
       "60.2098971893693,24.9508222622299 60.2098146733748,24.9522156017795 "
       "60.2097048497468,24.9535992031285 60.209567887551,24.9549709328641 "
       "60.2094039976338,24.9563286758789 60.2092134322985,24.957670338631 "
       "60.2089964849195,24.9589938523744 60.2087534894923,24.9602971763472 "
       "60.2084848201217,24.9615783009169 60.2081908904493,24.9628352506829 "
       "60.2078721530185,24.96406608752 60.2075290985811,24.9652689135667 "
       "60.2071622553458,24.9664418741527 60.2067721881674,24.9675831606575 "
       "60.2063594976814,24.9686910133004 60.2059248193825,24.9697637238528 "
       "60.2054688226502,24.9707996382726 60.2049922097218,24.9717971592548 "
       "60.2044957146153,24.9727547486946 60.2039801020025,24.9736709300585 "
       "60.2034461660356,24.9745442906611 60.202894729129,24.9753734838435 "
       "60.2023266406952,24.9761572310502 60.201742775841,24.9768943237997 "
       "60.2011440340239,24.9775836255484 60.2005313376693,24.9782240734428 "
       "60.1999056307538,24.9788146799591 60.1992678773537,24.9793545344246 "
       "60.1986190601636,24.9798428044228 60.1979601789848,24.9802787370767 "
       "60.1972922491878,24.98066166021 60.1966163001499,24.9809909833827 "
       "60.1959333736716,24.9812661988026 60.1952445223729,24.9814868821073 "
       "60.1945508080725,24.9816526930191 60.1938533001532,24.9817633758697 "
       "60.1931530739144,24.9818187599942 60.1924512089148))";

  if (result != ok)
    TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);

  OGRGeometryFactory::createFromWkt(&linestring, &srs, &input_geom);

  // expand line by 1 km
  output_geom = Fmi::OGR::expandGeometry(input_geom, 1000);

  result = Fmi::OGR::exportToWkt(*output_geom);

  OGRGeometryFactory::destroyGeometry(output_geom);
  OGRGeometryFactory::destroyGeometry(input_geom);

  ok = "POLYGON ((24.9770893742638 60.2929697363296,24.9772540038888 "
       "60.2933101917116,24.9774719389549 60.2936431825175,24.9777418358199 "
       "60.2939666560079,24.97806203048 60.2942786181738,24.9784305488286 "
       "60.294577146022,24.9788451188275 60.2948603994228,24.9793031845147 "
       "60.2951266324451,24.9798019217632 60.2953742041101,24.9803382556917 "
       "60.2956015884982,24.9809088796235 60.2958073841453,24.981510275472 "
       "60.2959903226732,24.982138735432 60.2961492765986,24.9827903848388 "
       "60.2962832662754,24.9834612060571 60.2963914659249,24.984147063251 "
       "60.2964732087203,24.984843727883 60.2965279908904,24.9855469047837 "
       "60.2965554748218,24.9862522586339 60.2965554911359,24.9869554406925 "
       "60.2965280397322,24.9876521156083 60.2964732897887,24.9883379881486 "
       "60.2963915787204,24.9890088296814 60.2962834101027,24.9896605042453 "
       "60.2961494505715,24.9902889940501 60.2959905257193,24.9908904242471 "
       "60.2958076150134,24.9914610868191 60.2956018457652,24.9919974634416 "
       "60.2953744861905,24.9924962471744 60.2951269376003,24.9929543628499 "
       "60.2948607257719,24.9933689860326 60.2945774915535,24.9937375604327 "
       "60.2942789807578,24.9940578136665 60.2939670334095,24.9943277712663 "
       "60.2936435724102,24.9945457688537 60.293310591692,24.9947104624012 "
       "60.2929701439319,24.9948208365181 60.292624327903,24.9948762107115 "
       "60.2922752755394,24.9948762435809 60.2919251387959,24.9948209349237 "
       "60.2915760763817,24.9947106257362 60.291230240452,24.9547106257362 "
       "60.1912275724462,24.9545459961112 60.1908860509161,24.9543280610451 "
       "60.1905520104369,24.9540581641801 60.1902275107394,24.95373796952 "
       "60.1899145527852,24.9533694511714 60.1896150664224,24.9529548811725 "
       "60.1893308984786,24.9524968154853 60.1890638013647,24.9519980782368 "
       "60.1888154222606,24.9514617443083 60.188587292949,24.9508911203765 "
       "60.1883808203604,24.950289724528 60.1881972778881,24.949661264568 "
       "60.188037797527,24.9490096151612 60.1879033628849,24.9483387939429 "
       "60.1877948031093,24.947652936749 60.1877127877676,24.946956272117 "
       "60.1876578227128,24.9462530952163 60.1876302469596,24.9455477413661 "
       "60.187630230591,24.9448445593075 60.1876577737079,24.9441478843917 "
       "60.1877127064287,24.9434620118514 60.1877946899381,24.9427911703186 "
       "60.1879032185793,24.9421394957547 60.1880376229771,24.9415110059499 "
       "60.1881970741705,24.9409095757529 60.1883805887315,24.9403389131809 "
       "60.1885870348374,24.9398025365584 60.1888151392581,24.9393037528256 "
       "60.1890634952165,24.9388456371501 60.1893305710729,24.9384310139674 "
       "60.1896147197783,24.9380624395673 60.1899141890403,24.9377421863335 "
       "60.1902271321367,24.9374722287337 60.1905516193111,24.9372542311463 "
       "60.190885649679,24.9370895375988 60.1912271635717,24.9369791634819 "
       "60.1915740552415,24.9369237892885 60.1919241858491,24.9369237564191 "
       "60.1922753966535,24.9369790650763 60.1926255223233,24.9370893742638 "
       "60.1929724042867,24.9770893742638 60.2929697363296))";

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
