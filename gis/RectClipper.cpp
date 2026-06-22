#include "RectClipper.h"
#include "Box.h"
#include "GeometryBuilder.h"
#include "OGR.h"
#include <macgyver/Exception.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace
{

// ----------------------------------------------------------------------
/*!
 * \brief Build a ring out of the bbox
 */
// ----------------------------------------------------------------------

OGRLinearRing *make_exterior(const Fmi::Box &theBox, double max_length = 0)
{
  try
  {
    auto *ring = new OGRLinearRing;
    ring->addPoint(theBox.xmin(), theBox.ymin());
    ring->addPoint(theBox.xmax(), theBox.ymin());
    ring->addPoint(theBox.xmax(), theBox.ymax());
    ring->addPoint(theBox.xmin(), theBox.ymax());
    ring->addPoint(theBox.xmin(), theBox.ymin());
    if (max_length > 0)
      ring->segmentize(max_length);
    return ring;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build a hole out of the bbox
 */
// ----------------------------------------------------------------------

OGRLinearRing *make_hole(const Fmi::Box &theBox, double max_length = 0)
{
  try
  {
    auto *ring = new OGRLinearRing;
    ring->addPoint(theBox.xmin(), theBox.ymin());
    ring->addPoint(theBox.xmin(), theBox.ymax());
    ring->addPoint(theBox.xmax(), theBox.ymax());
    ring->addPoint(theBox.xmax(), theBox.ymin());
    ring->addPoint(theBox.xmin(), theBox.ymin());
    if (max_length > 0)
      ring->segmentize(max_length);
    return ring;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Reconnect disjointed parts
 *
 * When we clip a LinearRing we may get multiple linestrings.
 * Often the first and last ones can be reconnected to simplify
 * output.
 *
 * Sample clip with a rectangle 0,0 --> 10,10 without reconnecting:
 *
 *   Input:   POLYGON ((5 10,0 0,10 0,5 10))
 *   Output:  MULTILINESTRING ((5 10,0 0),(10 0,5 10))
 *   Desired: LINESTRING (10 0,5 10,0 0)
 */
// ----------------------------------------------------------------------

void reconnectLines(std::list<OGRLineString *> &lines, Fmi::RectClipper &clipper, bool exterior)
{
  try
  {
    // std::cerr << "Reconnecting " << lines.size() << " lines\n";

    // Nothing to reconnect if there aren't at least two lines
    if (lines.size() < 2)
      return;

    for (auto pos1 = lines.begin(); pos1 != lines.end();)
    {
      auto *line1 = *pos1;
      int n1 = line1->getNumPoints();

      if (n1 == 0)  // safety check
      {
        ++pos1;
        continue;
      }

      for (auto pos2 = lines.begin(); pos2 != lines.end();)
      {
        auto *line2 = *pos2;
        const int n2 = line2->getNumPoints();

        // Continue if the ends do not match
        if (pos1 == pos2 || n2 == 0 || line1->getX(n1 - 1) != line2->getX(0) ||
            line1->getY(n1 - 1) != line2->getY(0))
        {
          ++pos2;
          continue;
        }

        // The lines are joinable

        line1->addSubLineString(line2, 1, n2 - 1);
        n1 = line1->getNumPoints();
        delete line2;
        line2 = nullptr;
        pos2 = lines.erase(pos2);

        // The merge may have closed a linearring if the intersections
        // have collapsed to a single point. This can happen if there is
        // a tiny sliver polygon just outside the rectangle, and the
        // intersection coordinates will be identical.

        if (line1->get_IsClosed())
        {
          auto *ring = new OGRLinearRing;
          ring->addSubLineString(line1, 0, -1);
          if (exterior)
            clipper.addExterior(ring);
          else
            clipper.addInterior(ring);

          delete line1;

          pos1 = lines.erase(pos1);
          if (pos1 == lines.end())
            return;

          line1 = *pos1;
          n1 = line1->getNumPoints();
          pos2 = lines.begin();  // safety measure
        }
      }

      if (pos1 != lines.end())
        ++pos1;
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Search for matching line segment clockwise (cutting)
 */
// ----------------------------------------------------------------------

std::list<OGRLineString *>::iterator search_cw(OGRLinearRing *ring,
                                               std::list<OGRLineString *> &lines,
                                               double x1,
                                               double y1,
                                               double &x2,
                                               double &y2,
                                               const Fmi::Box &box)
{
  try
  {
    // std::cerr << "Searching cw\n";
    auto best = lines.end();

    if (y1 == box.ymin() && x1 > box.xmin())
    {
      // On lower edge going left, worst we can do is left corner, closing might be better

      if (ring->getY(0) == y1 && ring->getX(0) < x1)
        x2 = ring->getX(0);
      else
        x2 = box.xmin();

      // Look for a better match from the remaining linestrings.

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);
        if (y == y1 && x > x2 && x <= x1)  // if not to the right and better than previous best
        {
          x2 = x;
          best = iter;
        }
      }
    }
    else if (x1 == box.xmin() && y1 < box.ymax())
    {
      // On left edge going up, worst we can do is upper corner, closing might be better

      if (ring->getX(0) == x1 && ring->getY(0) > y1)
        y2 = ring->getY(0);
      else
        y2 = box.ymax();

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);
        if (x == x1 && y > y1 && y <= y2)
        {
          y2 = y;
          best = iter;
        }
      }
    }
    else if (y1 == box.ymax() && x1 < box.xmax())
    {
      // On top edge going right, worst we can do is right corner, closing might be better

      if (ring->getY(0) == y1 && ring->getX(0) > x1)
        x2 = ring->getX(0);
      else
        x2 = box.xmax();

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);

        if (y == y1 && x >= x1 && x <= x2)
        {
          x2 = x;
          best = iter;
        }
      }
    }
    else
    {
      // On right edge going down, worst we can do is bottom corner, closing might be better

      if (ring->getX(0) == x1 && ring->getY(0) < y1)
        y2 = ring->getY(0);
      else
        y2 = box.ymin();

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);

        if (x == x2 && y <= y1 && y >= y2)
        {
          y2 = y;
          best = iter;
        }
      }
    }

    return best;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Search for matching line segment counter-clockwise (clipping)
 */
// ----------------------------------------------------------------------

std::list<OGRLineString *>::iterator search_ccw(OGRLinearRing *ring,
                                                std::list<OGRLineString *> &lines,
                                                double x1,
                                                double y1,
                                                double &x2,
                                                double &y2,
                                                const Fmi::Box &box)
{
  try
  {
    // std::cerr << "Searching ccw\n";

    auto best = lines.end();

    if (y1 == box.ymin() && x1 < box.xmax())
    {
      // On lower edge going right, worst we can do is right corner, closing might be better

      if (ring->getY(0) == y1 && ring->getX(0) > x1)
        x2 = ring->getX(0);
      else
        x2 = box.xmax();

      // Look for a better match from the remaining linestrings.

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);
        if (y == y1 && x < x2 && x >= x1)  // if not to the left and better than previous best
        {
          x2 = x;
          best = iter;
        }
      }
    }
    else if (x1 == box.xmin() && y1 > box.ymin())
    {
      // On left edge going down, worst we can do is lower corner, closing might be better

      if (ring->getX(0) == x1 && ring->getY(0) < y1)
        y2 = ring->getY(0);
      else
        y2 = box.ymin();

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);
        if (x == x1 && y < y1 && y >= y2)
        {
          y2 = y;
          best = iter;
        }
      }
    }
    else if (y1 == box.ymax() && x1 > box.xmin())
    {
      // On top edge going left, worst we can do is right corner, closing might be better

      if (ring->getY(0) == y1 && ring->getX(0) < x1)
        x2 = ring->getX(0);
      else
        x2 = box.xmin();

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);

        if (y == y1 && x <= x1 && x >= x2)
        {
          x2 = x;
          best = iter;
        }
      }
    }
    else
    {
      // On right edge going up, worst we can do is upper corner, closing might be better

      if (ring->getX(0) == x1 && ring->getY(0) > y1)
        y2 = ring->getY(0);
      else
        y2 = box.ymax();

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);

        if (x == x2 && y >= y1 && y <= y2)
        {
          y2 = y;
          best = iter;
        }
      }
    }

    return best;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Structural alternation guard for the box reconnection
 *
 * connectLines() reconnects the clipped arcs into rings by walking the box
 * boundary, pairing each arc's exit with the next arc's entry. For a valid
 * (simple) polygon clipped to a convex box, the entry/exit points strictly
 * alternate around the boundary: while inside the polygon the next crossing
 * must be an exit, and vice versa. This holds for any number of disjoint
 * exteriors too, since the box edge can only be inside one of them at a time.
 *
 * Self-intersecting input breaks this: two exits (or two entries) can become
 * adjacent on the boundary. The reconnection then pairs an exit with an entry
 * that lies past another arc's endpoint, wraps the whole box boundary, and
 * produces a polygon that fills the entire tile. An output area check cannot
 * catch this reliably, because a single wrap fills ~100% of the box while
 * staying just under the box area (only self-overlapping wraps exceed it).
 *
 * The functions below provide an O(k log k) (k = number of boundary crossings)
 * pre-check that detects the alternation violation directly, and a fallback
 * reconnection that closes rings without ever wrapping the box.
 */
// ----------------------------------------------------------------------

// Perimeter parameter of a boundary point, measured CCW from (xmin, ymin).
double perimeter_param(const Fmi::Box &box, double x, double y)
{
  const double W = box.xmax() - box.xmin();
  const double H = box.ymax() - box.ymin();
  const double tol = 1e-6 * (W + H) + 1e-12;
  if (std::abs(y - box.ymin()) <= tol)
    return std::clamp(x - box.xmin(), 0.0, W);  // bottom, left -> right
  if (std::abs(x - box.xmax()) <= tol)
    return W + std::clamp(y - box.ymin(), 0.0, H);  // right, bottom -> top
  if (std::abs(y - box.ymax()) <= tol)
    return W + H + std::clamp(box.xmax() - x, 0.0, W);  // top, right -> left
  return 2 * W + H + std::clamp(box.ymax() - y, 0.0, H);  // left, top -> bottom
}

// Coordinates of a boundary point given its CCW perimeter parameter.
std::pair<double, double> boundary_point(const Fmi::Box &box, double t)
{
  const double W = box.xmax() - box.xmin();
  const double H = box.ymax() - box.ymin();
  const double P = 2 * (W + H);
  t = std::fmod(t + P, P);
  if (t <= W)
    return {box.xmin() + t, box.ymin()};
  if (t <= W + H)
    return {box.xmax(), box.ymin() + (t - W)};
  if (t <= 2 * W + H)
    return {box.xmax() - (t - (W + H)), box.ymax()};
  return {box.xmin(), box.ymax() - (t - (2 * W + H))};
}

// True if the arcs' entry/exit crossings strictly alternate around the box.
// Near-coincident same-type crossings (within a small fraction of the
// perimeter) are tolerated: those are degenerate tangencies, not the gross
// mis-ordering that causes a wrap.
bool boundary_crossings_alternate(const std::list<OGRLineString *> &theLines, const Fmi::Box &box)
{
  std::vector<std::pair<double, int>> ev;  // (parameter, +1 = entry, -1 = exit)
  ev.reserve(2 * theLines.size());
  for (auto *ln : theLines)
  {
    const int m = ln->getNumPoints();
    if (m < 2)
      continue;
    ev.emplace_back(perimeter_param(box, ln->getX(0), ln->getY(0)), +1);
    ev.emplace_back(perimeter_param(box, ln->getX(m - 1), ln->getY(m - 1)), -1);
  }
  if (ev.size() < 2)
    return true;

  std::sort(ev.begin(), ev.end());

  const double W = box.xmax() - box.xmin();
  const double H = box.ymax() - box.ymin();
  const double P = 2 * (W + H);
  const double tol = 1e-6 * P;
  const std::size_t n = ev.size();
  for (std::size_t i = 0; i < n; ++i)
  {
    const auto &a = ev[i];
    const auto &b = ev[(i + 1) % n];
    const double d = std::fmod(b.first - a.first + P, P);
    if (a.second == b.second && d > tol)
      return false;  // two entries or two exits in a row: a genuine violation
  }
  return true;
}

// Append the box corner vertices strictly between t_from and t_to, walking in
// the chosen direction (ccw = increasing perimeter parameter).
void append_boundary_corners(
    OGRLinearRing *ring, const Fmi::Box &box, double t_from, double t_to, bool ccw)
{
  const double W = box.xmax() - box.xmin();
  const double H = box.ymax() - box.ymin();
  const double P = 2 * (W + H);
  const double corners[4] = {W, W + H, 2 * W + H, 0.0};  // BR, TR, TL, BL (t == 0)
  const double span = ccw ? std::fmod(t_to - t_from + P, P) : std::fmod(t_from - t_to + P, P);
  std::vector<std::pair<double, double>> ordered;  // (walk distance, parameter)
  for (double c : corners)
  {
    const double d = ccw ? std::fmod(c - t_from + P, P) : std::fmod(t_from - c + P, P);
    if (d > 1e-9 && d < span - 1e-9)
      ordered.emplace_back(d, c);
  }
  std::sort(ordered.begin(), ordered.end());
  for (const auto &pr : ordered)
  {
    const auto p = boundary_point(box, pr.second);
    ring->addPoint(p.first, p.second);
  }
}

// Fallback reconnection used only when boundary_crossings_alternate() reports a
// violation. It pairs each exit with the next entry along the boundary as long
// as no other arc endpoint lies in between (the legal, alternating case). When
// the next boundary event after an exit is another exit, it closes the current
// ring via the shorter boundary path instead of walking on. The shorter path
// spans at most half the perimeter, so the result can never wrap the whole box
// and never fills the tile. Output is not guaranteed OGC-valid, which is fine:
// downstream rendering fills by even-odd / nonzero winding rules.
void connectLinesSafe(std::list<OGRLinearRing *> &theRings,
                      std::list<OGRLineString *> &theLines,
                      const Fmi::Box &box,
                      bool cw)
{
  const bool ccw = !cw;
  const std::vector<OGRLineString *> L(theLines.begin(), theLines.end());
  const int n = static_cast<int>(L.size());

  std::vector<double> tin(n);
  std::vector<double> tout(n);
  for (int i = 0; i < n; ++i)
  {
    const int m = L[i]->getNumPoints();
    tin[i] = perimeter_param(box, L[i]->getX(0), L[i]->getY(0));
    tout[i] = perimeter_param(box, L[i]->getX(m - 1), L[i]->getY(m - 1));
  }

  const double W = box.xmax() - box.xmin();
  const double H = box.ymax() - box.ymin();
  const double P = 2 * (W + H);
  auto ahead = [&](double from, double to)
  { return ccw ? std::fmod(to - from + P, P) : std::fmod(from - to + P, P); };

  std::vector<char> used(n, 0);
  for (int s = 0; s < n; ++s)
  {
    if (used[s] != 0)
      continue;

    auto *ring = new OGRLinearRing;
    ring->addSubLineString(L[s]);
    used[s] = 1;
    double cur = tout[s];
    const double rstart = tin[s];

    for (int guard = 0; guard <= n + 1; ++guard)
    {
      int be = -1;
      double dbe = std::numeric_limits<double>::max();   // nearest unused entry ahead
      double dany = std::numeric_limits<double>::max();  // nearest unused endpoint ahead
      for (int j = 0; j < n; ++j)
        if (used[j] == 0)
        {
          const double de = ahead(cur, tin[j]);
          if (de > 1e-9)
          {
            if (de < dbe)
            {
              dbe = de;
              be = j;
            }
            dany = std::min(dany, de);
          }
          const double dx = ahead(cur, tout[j]);
          if (dx > 1e-9)
            dany = std::min(dany, dx);
        }
      const double dclose = ahead(cur, rstart);

      if (dclose <= dbe + 1e-9 && dclose <= dany + 1e-9)
      {
        // The ring's own start is the next event: a clean close.
        append_boundary_corners(ring, box, cur, rstart, ccw);
        break;
      }
      if (be >= 0 && dbe <= dany + 1e-9)
      {
        // Legal: the next boundary event is an entry. Connect to it.
        append_boundary_corners(ring, box, cur, tin[be], ccw);
        if (ring->getX(ring->getNumPoints() - 1) != L[be]->getX(0) ||
            ring->getY(ring->getNumPoints() - 1) != L[be]->getY(0))
          ring->addSubLineString(L[be]);
        else
          ring->addSubLineString(L[be], 1);
        used[be] = 1;
        cur = tout[be];
        continue;
      }
      // Defect: the next boundary event is an exit. Close via the shorter path
      // so the ring can never wrap the whole box.
      const double fwd = std::fmod(rstart - cur + P, P);
      const bool close_ccw = (fwd <= P / 2.0);
      append_boundary_corners(ring, box, cur, rstart, close_ccw);
      break;
    }

    ring->closeRings();
    Fmi::OGR::normalize(*ring);
    theRings.push_back(ring);
  }

  for (auto *l : theLines)
    delete l;
  theLines.clear();
}

// ----------------------------------------------------------------------
/*!
 * \brief Reconnect lines into polygons along box edges
 */
// ----------------------------------------------------------------------

void connectLines(std::list<OGRLinearRing *> &theRings,
                  std::list<OGRLineString *> &theLines,
                  const Fmi::Box &theBox,
                  double /*max_length*/,
                  bool keep_inside,
                  bool exterior)
{
  try
  {
    if (theLines.empty())
      return;

    bool cw = false;
    if (keep_inside)
      cw = !exterior;  // clipping: holes CW, exteriors CCW
    else
      cw = true;  // cutting: always CW (for both exteriors and holes)

    // Structural alternation guard: if the clipped arcs' entry/exit crossings
    // do not strictly alternate around the box, the input self-intersects on
    // the boundary and the normal walk would wrap the whole box (filling the
    // tile). Use the non-wrapping fallback instead. Valid input always
    // alternates, so this never changes normal behaviour.
    if (!boundary_crossings_alternate(theLines, theBox))
    {
      connectLinesSafe(theRings, theLines, theBox, cw);
      return;
    }

    OGRLinearRing *ring = nullptr;
    int cornerSteps = 0;

    while (!theLines.empty() || ring != nullptr)
    {
      if (ring == nullptr)
      {
        ring = new OGRLinearRing;
        auto *line = theLines.front();
        theLines.pop_front();
        ring->addSubLineString(line);
        delete line;
        cornerSteps = 0;
      }

      int nr = ring->getNumPoints();
      double x1 = ring->getX(nr - 1);
      double y1 = ring->getY(nr - 1);
      double x2 = x1;
      double y2 = y1;

      auto best = (cw ? search_cw(ring, theLines, x1, y1, x2, y2, theBox)
                      : search_ccw(ring, theLines, x1, y1, x2, y2, theBox));

      if (best != theLines.end())
      {
        cornerSteps = 0;
        if (x1 != (*best)->getX(0) || y1 != (*best)->getY(0))
          ring->addSubLineString(*best);
        else
          ring->addSubLineString(*best, 1);
        delete *best;
        theLines.erase(best);
      }
      else
      {
        ++cornerSteps;
        if (cornerSteps > 5)
          throw Fmi::Exception(BCP, "Stuck, discarding ring");

        ring->addPoint(x2, y2);
      }

      if (ring->get_IsClosed())
      {
        Fmi::OGR::normalize(*ring);
        theRings.push_back(ring);
        ring = nullptr;
        cornerSteps = 0;
      }
    }
    theLines.clear();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace

// ----------------------------------------------------------------------
/*!
 * \brief Destructor must delete all objects left behind in case of throw
 *
 * Normally build succeeds and clears the containers
 */
// ----------------------------------------------------------------------

Fmi::RectClipper::~RectClipper()
{
  try
  {
    for (auto *ptr : itsExteriorRings)
      delete ptr;
    for (auto *ptr : itsInteriorRings)
      delete ptr;
    for (auto *ptr : itsExteriorLines)
      delete ptr;
    for (auto *ptr : itsInteriorLines)
      delete ptr;
    for (auto *ptr : itsPolygons)
      delete ptr;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Destructor failed", nullptr);
    exception.printError();
  }
}

void Fmi::RectClipper::reconnect()
{
  try
  {
    reconnectLines(itsExteriorLines, *this, /*exterior=*/true);
    reconnectLines(itsInteriorLines, *this, /*exterior=*/false);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Export parts to another container
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::release(GeometryBuilder &theBuilder)
{
  try
  {
    for (auto *ptr : itsPolygons)
      theBuilder.add(ptr);
    for (auto *ptr : itsExteriorLines)
      theBuilder.add(ptr);

    clear();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clear the parts having transferred ownership elsewhere
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::clear()
{
  try
  {
    itsExteriorRings.clear();
    itsExteriorLines.clear();
    itsInteriorRings.clear();
    itsInteriorLines.clear();
    itsPolygons.clear();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Test if there are no parts at all
 */
// ----------------------------------------------------------------------

bool Fmi::RectClipper::empty() const
{
  try
  {
    return itsExteriorRings.empty() && itsExteriorLines.empty() && itsInteriorRings.empty() &&
           itsInteriorLines.empty();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add box to the result
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::addBox()
{
  try
  {
    itsAddBoxFlag = true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add intermediate OGR Polygon
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::addExterior(OGRLinearRing *theRing)
{
  try
  {
    OGR::normalize(*theRing);
    if (theRing->isClockwise() == 1)
      theRing->reversePoints();
    itsExteriorRings.push_back(theRing);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add intermediate OGR LineString
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::addExterior(OGRLineString *theLine)
{
  try
  {
    auto n = theLine->getNumPoints();

    // We may have just touched the exterior at a single point
    if (n < 2)
      delete theLine;
    else
      itsExteriorLines.push_back(theLine);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add intermediate OGR Polygon
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::addInterior(OGRLinearRing *theRing)
{
  try
  {
    OGR::normalize(*theRing);
    if (theRing->isClockwise() == 0)
      theRing->reversePoints();
    itsInteriorRings.push_back(theRing);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add intermediate OGR LineString
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::addInterior(OGRLineString *theLine)
{
  try
  {
    itsInteriorLines.push_back(theLine);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build polygons from parts left by clipping one
 *
 * 1. Build exterior ring(s) from lines
 * 2. Attach polygons as holes to the exterior ring(s)
 *
 * Building new exterior:
 * 1. Pick first linestring as the beginning of a ring (until there are none left)
 * 2. Proceed outputting the bbox edges in clockwise orientation (clipping, ccw for cutting)
 *    until the ring itself or another linestring is encountered.
 * 3. If the ring became closed by joining the bbox edges, output it and restart at step 1.
 * 4. Attach the new linestring to the ring and jump back to step 2.
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::reconnectWithBox(double theMaximumSegmentLength)
{
  try
  {
    // Make exterior box if necessary

    if (itsKeepInsideFlag && itsAddBoxFlag && itsExteriorLines.empty())
    {
      auto *ring = make_exterior(itsBox, theMaximumSegmentLength);
      itsExteriorRings.push_back(ring);
    }

    // Make hole if necessary

    if (!itsKeepInsideFlag && itsAddBoxFlag && itsInteriorLines.empty())
    {
      auto *ring = make_hole(itsBox, theMaximumSegmentLength);
      itsInteriorRings.push_back(ring);
    }

    // Reconnect lines into polygons (exterior or hole)
    // Since clipped holes always become part of the exterior, and cut
    // holes are either part of the interior unless the exterior is also clipped,
    // if we have both lines they must by definition all belong to the exterior.

    if (!itsExteriorLines.empty() && !itsInteriorLines.empty())
    {
      std::move(
          itsInteriorLines.begin(), itsInteriorLines.end(), std::back_inserter(itsExteriorLines));
      itsInteriorLines.clear();
    }

    connectLines(itsExteriorRings,
                 itsExteriorLines,
                 itsBox,
                 theMaximumSegmentLength,
                 itsKeepInsideFlag,
                 /*exterior=*/true);
    connectLines(itsInteriorRings,
                 itsInteriorLines,
                 itsBox,
                 theMaximumSegmentLength,
                 itsKeepInsideFlag,
                 /*exterior=*/false);

    // Build polygons starting from the built exterior rings.
    // Skip degenerate rings with fewer than 4 points — these arise when jump detection
    // produces a tiny stub linestring that reconnectLines closes into a 2–3 point ring.
    // Such rings have zero area and cause allPolygonRingsClosed checks to fail.
    for (auto *exterior : itsExteriorRings)
    {
      if (exterior->getNumPoints() < 4)
      {
        delete exterior;
        continue;
      }
      auto *poly = new OGRPolygon;
      poly->addRingDirectly(exterior);
      itsPolygons.push_back(poly);
    }
    itsExteriorRings.clear();

    // Then assign the holes to them

    for (auto *hole : itsInteriorRings)
    {
      if (itsPolygons.size() == 1)
      {
        // addRingDirectly transfers ownership — no delete needed
        itsPolygons.front()->addRingDirectly(hole);
      }
      else
      {
        OGRPoint point;
        hole->getPoint(0, &point);
        bool assigned = false;
        for (auto *poly : itsPolygons)
        {
          auto *ext = poly->getExteriorRing();
          if (ext != nullptr && ext->isPointInRing(&point, 0) != 0)
          {
            poly->addRingDirectly(hole);
            assigned = true;
            break;
          }
        }
        if (!assigned)
          delete hole;  // no enclosing polygon found — discard
      }
    }

    // Merge all unjoinable lines to one list of lines

    std::move(
        itsInteriorLines.begin(), itsInteriorLines.end(), std::back_inserter(itsExteriorLines));

    itsInteriorRings.clear();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build results without connecting with the box
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::reconnectWithoutBox()
{
  try
  {
    // std::cerr << "Reconnecting without bbox\n";

    // Make exterior box if necessary

    if (itsKeepInsideFlag && itsAddBoxFlag && itsExteriorLines.empty())
    {
      auto *ring = make_exterior(itsBox);
      itsExteriorRings.push_back(ring);
    }

    // Make hole if necessary

    if (!itsKeepInsideFlag && itsAddBoxFlag && !itsInteriorLines.empty())
    {
      auto *ring = make_hole(itsBox);
      itsInteriorRings.push_back(ring);
    }

    // Build polygons starting from the built exterior rings
    for (auto *exterior : itsExteriorRings)
    {
      auto *poly = new OGRPolygon;
      poly->addRingDirectly(exterior);
      itsPolygons.push_back(poly);
    }
    itsExteriorRings.clear();

    // Then assign the holes to them

    for (auto *hole : itsInteriorRings)
    {
      if (itsPolygons.size() == 1)
      {
        // addRing copies — we keep ownership and must delete
        itsPolygons.front()->addRing(hole);
        delete hole;
      }
      else
      {
        OGRPoint point;
        hole->getPoint(0, &point);
        bool assigned = false;
        for (auto *poly : itsPolygons)
        {
          auto *ext = poly->getExteriorRing();
          if (ext != nullptr && ext->isPointInRing(&point, 0) != 0)
          {
            // addRingDirectly transfers ownership — do not delete
            poly->addRingDirectly(hole);
            assigned = true;
            break;
          }
        }
        if (!assigned)
          delete hole;  // no enclosing polygon found — discard
      }
    }

    // Merge all unjoinable lines to one list of lines

    std::move(
        itsInteriorLines.begin(), itsInteriorLines.end(), std::back_inserter(itsExteriorLines));

    itsInteriorRings.clear();
    itsInteriorLines.clear();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
