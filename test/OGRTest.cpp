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
    if (err != OGRERR_NONE) TEST_FAILED("Failed to create spatial reference +init=epsg:4326");

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
    if (err != OGRERR_NONE) TEST_FAILED("Failed to create spatial reference EPSG:4326");

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
    if (err != OGRERR_NONE) TEST_FAILED("Failed to create spatial reference EPSG:2393");

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
      "POLYGON ((24.9459 60.2100663056824,24.9446148000447 60.2100109215579,24.9433375103434 "
      "60.2098451106461,24.942076006184 60.2095698952262,24.9408380657699 "
      "60.209186972093,24.9396313222488 60.2086987020948,24.9384632166318 "
      "60.2081080955786,24.9373409518912 60.2074187938299,24.9362714485222 "
      "60.2066350466232,24.9352613018418 60.2057616860207,24.9343167412908 "
      "60.2048040965809,24.9334435919885 60.2037681821611,24.93264723878 "
      "60.2026603295183,24.9319325929981 60.2014873689323,24.9313040621443 "
      "60.2002565320952,24.9307655226797 60.1989754075255,24.9303202960913 "
      "60.197651893782,24.9299711283843 60.1962941507672,24.9297201731266 "
      "60.1949105494182,24.9295689781508 60.1935096200974,24.9295184759963 "
      "60.1921,24.9295689781508 60.1906903799026,24.9297201731266 "
      "60.1892894505818,24.9299711283843 60.1879058492328,24.9303202960913 "
      "60.186548106218,24.9307655226797 60.1852245924745,24.9313040621443 "
      "60.1839434679048,24.9319325929981 60.1827126310677,24.93264723878 "
      "60.1815396704817,24.9334435919885 60.1804318178389,24.9343167412908 "
      "60.1793959034191,24.9352613018418 60.1784383139793,24.9362714485222 "
      "60.1775649533768,24.9373409518912 60.1767812061701,24.9384632166318 "
      "60.1760919044214,24.9396313222488 60.1755012979052,24.9408380657699 "
      "60.175013027907,24.942076006184 60.1746301047738,24.9433375103434 "
      "60.1743548893539,24.9446148000447 60.1741890784421,24.9459 "
      "60.1741336943176,24.9471851863965 60.1741890784421,24.9484624357595 "
      "60.1743548893539,24.9497238737929 60.1746301047738,24.9509617239219 "
      "60.175013027907,24.9521683552214 60.1755012979052,24.9533363294439 "
      "60.1760919044214,24.9544584468522 60.1767812061701,24.9555277905793 "
      "60.1775649533768,24.9565377692387 60.1784383139793,24.9574821575272 "
      "60.1793959034191,24.9583551345669 60.1804318178389,24.9591513197544 "
      "60.1815396704817,24.9598658058944 60.1827126310677,24.960494189416 "
      "60.1839434679048,24.961032597486 60.1852245924745,24.9614777118529 "
      "60.186548106218,24.9618267892747 60.1879058492328,24.9620776784065 "
      "60.1892894505818,24.9622288330441 60.1906903799026,24.9622793216412 "
      "60.1921,24.9622288330441 60.1935096200974,24.9620776784065 "
      "60.1949105494182,24.9618267892747 60.1962941507672,24.9614777118529 "
      "60.197651893782,24.961032597486 60.1989754075255,24.960494189416 "
      "60.2002565320952,24.9598658058944 60.2014873689323,24.9591513197544 "
      "60.2026603295183,24.9583551345669 60.2037681821611,24.9574821575272 "
      "60.2048040965809,24.9565377692387 60.2057616860207,24.9555277905793 "
      "60.2066350466232,24.9544584468522 60.2074187938299,24.9533363294439 "
      "60.2081080955786,24.9521683552214 60.2086987020948,24.9509617239219 "
      "60.209186972093,24.9497238737929 60.2095698952262,24.9484624357595 "
      "60.2098451106461,24.9471851863965 60.2100109215579,24.9459 60.2100663056824))";

  if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);

  // expand circle another 1 km
  OGRGeometry* output_geom2 = Fmi::OGR::expandGeometry(output_geom, 1000);

  result = Fmi::OGR::exportToWkt(*output_geom2);
  OGRGeometryFactory::destroyGeometry(output_geom);
  OGRGeometryFactory::destroyGeometry(output_geom2);

  ok = "POLYGON ((24.94654309069 60.2280187599942,24.9452569059124 "
       "60.2280187599942,24.9439716991741 60.2279633758697,24.9426894522339 "
       "60.2278526930191,24.9414121423496 60.2276868821072,24.9401417392278 "
       "60.2274661988026,24.9388802019845 60.2271909833828,24.9376294761253 "
       "60.2268616602099,24.9363914905424 60.2264787370767,24.9351681545377 "
       "60.2260428044227,24.9339613548764 60.2255545344246,24.9327729528755 "
       "60.2250146799591,24.9316047815302 60.2244240734429,24.9304586426838 "
       "60.2237836255483,24.929336304246 60.2230943237996,24.928239497463 "
       "60.2223572310502,24.927169914243 60.2215734838435,24.9261292045442 "
       "60.2207442906609,24.9251189738258 60.2198709300584,24.924140780567 "
       "60.2189547486947,24.9231961338599 60.2179971592549,24.9222864910778 "
       "60.2169996382725,24.9214132556229 60.2159637238528,24.9205777747573 "
       "60.2148910133006,24.9197813375208 60.2137831606578,24.9190251727384 "
       "60.2126418741527,24.9183104471212 60.2114689135667,24.9176382634623 "
       "60.2102660875199,24.9170096589315 60.2090352506828,24.9164256034726 "
       "60.2077783009169,24.9158869983027 60.2064971763471,24.915394674519 "
       "60.2051938523747,24.9149493918141 60.2038703386312,24.9145518373003 "
       "60.2025286758789,24.9142026244468 60.2011709328641,24.913902292132 "
       "60.199799203128,24.913651303809 60.198415601779,24.9134500467892 "
       "60.1970222622302,24.913298831643 60.1956213329094,24.9131978917196 "
       "60.1942149739482,24.9131473827861 60.1928053538508,24.9131473827861 "
       "60.1913946461492,24.9131978917196 60.1899850260518,24.913298831643 "
       "60.1885786670906,24.9134500467892 60.1871777377698,24.913651303809 "
       "60.185784398221,24.913902292132 60.184400796872,24.9142026244468 "
       "60.1830290671359,24.9145518373003 60.181671324121,24.9149493918142 "
       "60.1803296613687,24.915394674519 60.1790061476253,24.9158869983026 "
       "60.1777028236529,24.9164256034726 60.1764216990832,24.9170096589315 "
       "60.1751647493172,24.9176382634623 60.1739339124801,24.9183104471213 "
       "60.1727310864333,24.9190251727384 60.1715581258473,24.9197813375208 "
       "60.1704168393422,24.9205777747573 60.1693089866994,24.9214132556228 "
       "60.1682362761473,24.9222864910777 60.1672003617276,24.9231961338599 "
       "60.1662028407451,24.924140780567 60.1652452513053,24.9251189738258 "
       "60.1643290699415,24.9261292045443 60.163455709339,24.927169914243 "
       "60.1626265161565,24.928239497463 60.1618427689498,24.929336304246 "
       "60.1611056762004,24.9304586426838 60.1604163744517,24.9316047815302 "
       "60.1597759265571,24.9327729528755 60.1591853200409,24.9339613548764 "
       "60.1586454655754,24.9351681545377 60.1581571955772,24.9363914905425 "
       "60.1577212629233,24.9376294761254 60.15733833979,24.9388802019844 "
       "60.1570090166173,24.9401417392277 60.1567338011974,24.9414121423497 "
       "60.1565131178928,24.942689452234 60.1563473069809,24.9439716991739 "
       "60.1562366241303,24.9452569059122 60.1561812400058,24.9465430906902 "
       "60.1561812400058,24.9478282703033 60.1562366241303,24.9491104631601 "
       "60.1563473069809,24.9503876923367 60.1565131178927,24.9516579886243 "
       "60.1567338011974,24.9529193935647 60.1570090166173,24.9541699624684 "
       "60.15733833979,24.9554077674111 60.1577212629233,24.9566309002045 "
       "60.1581571955772,24.9578374753361 60.1586454655754,24.9590256328734 "
       "60.1591853200409,24.9601935413283 60.1597759265571,24.9613394004783 "
       "60.1604163744517,24.962461444138 60.1611056762004,24.963557942879 "
       "60.1618427689499,24.9646272066917 60.1626265161566,24.9656675875875 "
       "60.163455709339,24.9666774821345 60.1643290699415,24.9676553339261 "
       "60.1652452513054,24.9685996359751 60.1662028407452,24.9695089330333 "
       "60.1672003617274,24.9703818238302 60.1682362761472,24.9712169632285 "
       "60.1693089866995,24.9720130642937 60.1704168393423,24.972768900273 "
       "60.1715581258471,24.9734833064831 60.1727310864331,24.9741551821 "
       "60.1739339124802,24.9747834918528 60.1751647493172,24.9753672676153 "
       "60.1764216990832,24.9759056098948 60.1777028236529,24.9763976892148 "
       "60.1790061476252,24.9768427473901 60.1803296613687,24.9772400986927 "
       "60.1816713241211,24.977589130906 60.183029067136,24.9778893062655 "
       "60.1844007968718,24.9781401622857 60.1857843982208,24.978341312471 "
       "60.1871777377699,24.9784924469096 60.1885786670906,24.9785933327498 "
       "60.1899850260516,24.9786438145582 60.191394646149,24.9786438145582 "
       "60.192805353851,24.9785933327498 60.1942149739484,24.9784924469096 "
       "60.1956213329093,24.978341312471 60.1970222622301,24.9781401622857 "
       "60.1984156017792,24.9778893062655 60.1997992031282,24.977589130906 "
       "60.201170932864,24.9772400986927 60.2025286758789,24.9768427473901 "
       "60.2038703386312,24.9763976892148 60.2051938523747,24.9759056098948 "
       "60.2064971763471,24.9753672676153 60.2077783009169,24.9747834918528 "
       "60.2090352506828,24.9741551821 60.2102660875198,24.9734833064831 "
       "60.2114689135668,24.9727689002731 60.2126418741528,24.9720130642937 "
       "60.2137831606577,24.9712169632285 60.2148910133005,24.9703818238301 "
       "60.2159637238529,24.9695089330332 60.2169996382727,24.9685996359751 "
       "60.2179971592548,24.9676553339261 60.2189547486946,24.9666774821346 "
       "60.2198709300584,24.9656675875875 60.2207442906609,24.9646272066917 "
       "60.2215734838434,24.963557942879 60.2223572310501,24.962461444138 "
       "60.2230943237996,24.9613394004783 60.2237836255483,24.9601935413282 "
       "60.2244240734429,24.9590256328734 60.2250146799591,24.9578374753362 "
       "60.2255545344246,24.9566309002046 60.2260428044227,24.9554077674112 "
       "60.2264787370767,24.9541699624685 60.2268616602099,24.9529193935647 "
       "60.2271909833828,24.9516579886242 60.2274661988026,24.9503876923368 "
       "60.2276868821072,24.9491104631602 60.2278526930191,24.9478282703031 "
       "60.2279633758697,24.94654309069 60.2280187599942))";
  if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);

  OGRGeometryFactory::createFromWkt(&linestring, &srs, &input_geom);

  // expand line by 1 km
  output_geom = Fmi::OGR::expandGeometry(input_geom, 1000);

  result = Fmi::OGR::exportToWkt(*output_geom);

  OGRGeometryFactory::destroyGeometry(output_geom);
  OGRGeometryFactory::destroyGeometry(input_geom);

  ok = "POLYGON ((24.9933972467316 60.2884904207253,24.9936322408839 "
       "60.28914695665,24.9938195643442 60.289821699069,24.9939580624238 "
       "60.2905104879708,24.994046881404 60.2912090767429,24.9940854737954 "
       "60.2919131583531,24.994073601711 60.2926183919042,24.9940113383314 "
       "60.2933204293968,24.993899067454 60.2940149425364,24.9937374811289 "
       "60.2946976494184,24.9935275753952 60.295364340928,24.9932706441451 "
       "60.2960109066902,24.9929682711527 60.2966333604122,24.9926223203166 "
       "60.2972278644598,24.9922349241767 60.2977907535177,24.9918084707749 "
       "60.2983185571879,24.991345588942 60.2988080213851,24.9908491320995 "
       "60.2992561283998,24.9903221606772 60.2996601155033,24.9897679232544 "
       "60.3000174919807,24.9891898365403 60.3003260544873,24.9885914643179 "
       "60.3005839006325,24.9879764954792 60.300789440709,24.9873487212891 "
       "60.3009414074941,24.9867120120164 60.301038864062,24.9860702930763 "
       "60.3010812095604,24.9854275208316 60.3010681829153,24.9847876582019 "
       "60.3009998644403,24.9841546502299 60.3008766753415,24.9835323997577 "
       "60.3006993751207,24.9829247433606 60.3004690568927,24.9823354276889 "
       "60.300187140646,24.9817680863619 60.299855364488,24.9812262175592 "
       "60.2994757739289,24.980713162444 60.2990507092709,24.9802320845557 "
       "60.2985827911789,24.9797859502957 60.2980749045235,24.9793775106288 "
       "60.2975301805949,24.9790092841131 60.2969519777972,24.9786835413622 "
       "60.2963438609429,24.978402291037 60.2957095792747,24.9383998270841 "
       "60.1957095792747,24.9381647262764 60.19505304335,24.9379773171463 "
       "60.194378300931,24.9378387553555 60.1936895120291,24.9377498953513 "
       "60.1929909232571,24.9377112850943 60.1922868416469,24.9377231626772 "
       "60.1915816080958,24.9377854548562 60.1908795706032,24.9378977775024 "
       "60.1901850574636,24.9380594379722 60.1895023505816,24.9382694393811 "
       "60.188835659072,24.9385264867547 60.1881890933098,24.9388289950182 "
       "60.1875666395878,24.9391750987767 60.1869721355402,24.9395626638239 "
       "60.1864092464823,24.9399893003089 60.1858814428121,24.9404523774802 "
       "60.1853919786149,24.9409490399147 60.1849438716002,24.9414762251322 "
       "60.1845398844967,24.942030682486 60.1841825080192,24.942608993214 "
       "60.1838739455127,24.9432075915246 60.1836160993675,24.9438227865888 "
       "60.1834105592909,24.944450785302 60.1832585925059,24.9450877156757 "
       "60.183161135938,24.945729650713 60.1831187904396,24.9463726326232 "
       "60.1831318170847,24.9470126972237 60.1832001355597,24.9476458983801 "
       "60.1833233246585,24.9482683323339 60.1835006248793,24.9488761617665 "
       "60.1837309431072,24.9494656394537 60.184012859354,24.9500331313614 "
       "60.184344635512,24.9505751390443 60.1847242260711,24.9510883212066 "
       "60.1851492907291,24.951569514293 60.1856172088211,24.9520157519839 "
       "60.1861250954765,24.9524242834741 60.1866698194051,24.9527925904223 "
       "60.1872480222028,24.9531184024682 60.1878561390571,24.9533997112205 "
       "60.1884904207253,24.9933972467316 60.2884904207253))";
  if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);

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
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(linestring, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M30 10 10 30 40 40";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(polygon1, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M30 10 40 40 20 40 10 20Z";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(polygon2, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M35 10 45 45 15 40 10 20ZM20 30 35 35 30 20Z";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(multipoint1, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M10 40M40 30M20 20M30 10";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(multipoint2, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M10 40M40 30M20 20M30 10";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(multilinestring, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M10 10 20 20 10 40M40 40 30 30 40 20 30 10";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    OGRGeometryFactory::createFromWkt(multipolygon1, NULL, &geom);
    string result = exportToSvg(*geom, box, precision);
    OGRGeometryFactory::destroyGeometry(geom);
    string ok = "M30 20 45 40 10 40ZM15 5 40 10 10 20 5 10Z";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
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

    const char* wkt = mytests[test][0];
    string ok = mytests[test][1];

    try
    {
      auto err = OGRGeometryFactory::createFromWkt(wkt, NULL, &input);
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

    const char* wkt = mytests[test][0];
    string ok = mytests[test][1];

    // std::cerr << "\nTEST: " << std::string(wkt) << std::endl;
    try
    {
      auto err = OGRGeometryFactory::createFromWkt(wkt, NULL, &input);
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

    const char* wkt = mytests[test][0];
    string ok = mytests[test][1];

    // std::cerr << "\nTEST: " << std::string(wkt) << std::endl;
    try
    {
      auto err = OGRGeometryFactory::createFromWkt(wkt, &wgs84, &input);
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

void grid_north_wgs84()
{
  // For latlon itself north is always at 0
  Fmi::CoordinateTransformation trans("WGS84", "WGS84");

  auto result = Fmi::OGR::gridNorth(trans, 25, 60);
  if (!result) TEST_FAILED("Failed to establish WGS84 north at 25,60");
  if (*result != 0)
    TEST_FAILED("Expecting WGS84 north 0 at 25,60 but got " + std::to_string(*result));
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void grid_north_epsg_4326()
{
  Fmi::CoordinateTransformation trans("WGS84", "EPSG:4326");

  auto result = Fmi::OGR::gridNorth(trans, 25, 60);
  if (!result) TEST_FAILED("Failed to establish EPSG:4326 north at 25,60");
  if (*result != 0)
    TEST_FAILED("Expecting WGS84 north 0 at 25,60 but got " + std::to_string(*result));
  TEST_PASSED();
}
// ----------------------------------------------------------------------

void grid_north_epsga_4326()
{
  Fmi::CoordinateTransformation trans("WGS84", "EPSGA:4326");

  auto result = Fmi::OGR::gridNorth(trans, 25, 60);
  if (!result) TEST_FAILED("Failed to establish EPSGA:4326 north at 25,60");
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
  if (!result) TEST_FAILED("Failed to establish EPSG:3035 north at 25,60");
  if (std::abs(*result - expected) > 0.0001)
    TEST_FAILED("Expecting EPSG:3035 north " + std::to_string(expected) + " at 25,60 but got " +
                std::to_string(*result));

  // Stockholm
  result = Fmi::OGR::gridNorth(trans, 18, 60);
  expected = -6.815401;
  if (!result) TEST_FAILED("Failed to establish EPSG:3035 north at 18,60");
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
  if (!result) TEST_FAILED("Failed to establish EPSG:3034 north at 25,60");
  if (std::abs(*result - expected) > 0.0001)
    TEST_FAILED("Expecting EPSG:3034 north " + std::to_string(expected) + " at 25,60 but got " +
                std::to_string(*result));

  // Stockholm
  result = Fmi::OGR::gridNorth(trans, 18, 60);
  expected = -6.203053;
  if (!result) TEST_FAILED("Failed to establish EPSG:3034 north at 18,60");
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
  if (!result) TEST_FAILED("Failed to establish polster north at 25,60");
  if (std::abs(*result - expected) > 0.0001)
    TEST_FAILED("Expecting polster north " + std::to_string(expected) + " at 25,60 but got " +
                std::to_string(*result));

  // Stockholm
  result = Fmi::OGR::gridNorth(trans, 18, 60);
  expected = 2;
  if (!result) TEST_FAILED("Failed to establish polster north at 18,60");
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
  if (!result) TEST_FAILED("Failed to establish rotlatlon north at 25,60");
  if (std::abs(*result - expected) > 0.0001)
    TEST_FAILED("Expecting rotlatlon north " + std::to_string(expected) + " at 25,60 but got " +
                std::to_string(*result));

  // Stockholm
  result = Fmi::OGR::gridNorth(trans, 18, 60);
  expected = -15.527667;
  if (!result) TEST_FAILED("Failed to establish rotlatlon north at 18,60");
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

  // Helsinki
  auto result = Fmi::OGR::gridNorth(trans, 25, 60);
  auto expected = -21.503683;
  if (!result) TEST_FAILED("Failed to establish rotlatlon north at 25,60");
  if (std::abs(*result - expected) > 0.0001)
    TEST_FAILED("Expecting rotlatlon north " + std::to_string(expected) + " at 25,60 but got " +
                std::to_string(*result));

  // Stockholm
  result = Fmi::OGR::gridNorth(trans, 18, 60);
  expected = -15.529383;
  if (!result) TEST_FAILED("Failed to establish rotlatlon north at 18,60");
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
    TEST(expand_geometry);
    TEST(exportToWkt_spatialreference);
    TEST(exportToSvg_precision);
    TEST(exportToSvg_wiki_examples);
    TEST(exportToProj);
    TEST(lineclip);
    TEST(polyclip);
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
