#pragma once
#include <memory>
#include <string>

namespace Fmi
{
class LandCover
{
 public:
  enum Type
  {
    IrrigatedCropLand = 11,
    RainFedCropLand = 14,
    MosaicCropLand = 20,
    MosaicVegetation = 30,
    ClosedToOpenBroadLeavedDeciduousForest = 40,
    ClosedBroadLeavedDeciduousForest = 50,
    OpenBroadLeavedDeciduousForest = 60,
    ClosedNeedleLeavedEvergreenForest = 70,
    OpenNeedleLeavedDeciduousOrEvergreenForest = 90,
    MixedForest = 100,
    MosaicForest = 110,
    MosaicGrassLand = 120,
    ShrubLand = 130,
    Herbaceous = 140,
    SparseVegetation = 150,
    RegularlyFloodedForest = 160,
    PermanentlyFloodedForest = 170,
    RegularlyFloodedGrassland = 180,
    Urban = 190,
    Bare = 200,
    Lakes = 210,
    PermanentSnow = 220,
    NoData = 230,
    Sea = 240,
    CaspianSea = 241,
    RiverEstuary = 242
  };

  ~LandCover();
  LandCover(const std::string& path);

  Type coverType(double lon, double lat) const;
  static bool isOpenWater(Type theType);

 private:
  LandCover() = delete;
  LandCover(const LandCover& other) = delete;
  LandCover& operator=(const LandCover& other) = delete;

  class Impl;
  std::unique_ptr<Impl> impl;

};  // class LandCover
}  // namespace Fmi
