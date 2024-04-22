#pragma once
#include <ogr_geometry.h>
#include <unordered_map>

namespace Fmi
{
struct OGRPointHash
{
  std::size_t operator()(const OGRPoint& point) const
  {
    return std::hash<double>()(point.getX()) ^ (std::hash<double>()(point.getY()) << 1);
  }
};

struct OGRPointEqual
{
  bool operator()(const OGRPoint& p1, const OGRPoint& p2) const
  {
    return (p1.getX() == p2.getX()) && (p1.getY() == p2.getY());
  }
};

class VertexCounter
{
 public:
  void add(const OGRGeometry* geometry);
  int getCount(const OGRPoint& point) const;

 private:
  std::unordered_map<OGRPoint, int, OGRPointHash, OGRPointEqual> m_counts;
  void processGeometry(const OGRGeometry* geometry);
};

}  // namespace Fmi
