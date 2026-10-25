#ifndef IB_PROXIMITY_HXX
#define IB_PROXIMITY_HXX

#include "opobj.hxx"
#include <algorithm>
#include <limits>
#include <vector>

namespace ib_proximity
{
// Count qualifying nonoverlapping pairs and retain their original hits.
// Ordinary tokens use contiguous ranges and evidence boundary events. Nested
// right intervals need explicit end checks only for the before-side range.
template<class HitTable>
size_t Join(const HitTable& left, const HitTable& right, GPTYPE distance,
    bool far, OPOBJ::DIR_T direction, HitTable& evidence)
{
  using Hit = typename HitTable::hit_type;
  std::vector<Hit> hits;
  hits.reserve(right.Size());
  for (const auto& hit : right) if (!hit.IsEmpty()) hits.push_back(hit);
  std::sort(hits.begin(),hits.end(),[](const Hit& a, const Hit& b) {
    return a.GetFieldStart() < b.GetFieldStart() ||
      (a.GetFieldStart() == b.GetFieldStart() && a.GetFieldEnd() < b.GetFieldEnd());
  });
  std::vector<GPTYPE> maxEnds;
  maxEnds.reserve(hits.size());
  bool monotoneEnds = true;
  for (const auto& hit : hits)
    {
      const GPTYPE end = hit.GetFieldEnd();
      if (!maxEnds.empty() && end < maxEnds.back()) monotoneEnds = false;
      maxEnds.push_back(maxEnds.empty() ? end : std::max(end,maxEnds.back()));
    }
  std::vector<size_t> starts(hits.size()+1,0), stops(hits.size()+1,0);
  std::vector<unsigned char> used(monotoneEnds ? 0 : hits.size(),0);
  const auto startLower = [&](GPTYPE gp) {
    return size_t(std::lower_bound(hits.begin(),hits.end(),gp,
      [](const Hit& hit,GPTYPE p) { return hit.GetFieldStart() < p; })-hits.begin());
  };
  const auto startUpper = [&](GPTYPE gp) {
    return size_t(std::upper_bound(hits.begin(),hits.end(),gp,
      [](GPTYPE p,const Hit& hit) { return p < hit.GetFieldStart(); })-hits.begin());
  };

  evidence.Clear();
  size_t pairs = 0;
  const GPTYPE limit = std::numeric_limits<GPTYPE>::max();
  for (const auto& hit : left)
    {
      if (hit.IsEmpty()) continue;
      const GPTYPE first = hit.GetFieldStart(), last = hit.GetFieldEnd();
      size_t count = 0;
      const auto mark = [&](size_t begin,size_t end) {
        if (begin < end) { count += end-begin; ++starts[begin]; ++stops[end]; }
      };

      // Right hits before this left hit: their ends determine the gap.
      if (direction != OPOBJ::AFTER && first != 0 && (!far || first > distance))
        {
          const GPTYPE low = far ? 0 : first > distance ? first-distance-1 : 0;
          const GPTYPE high = far ? first-distance-1 : first-1;
          const size_t begin = std::lower_bound(maxEnds.begin(),maxEnds.end(),low)-maxEnds.begin();
          if (monotoneEnds)
            mark(begin,std::upper_bound(maxEnds.begin(),maxEnds.end(),high)-maxEnds.begin());
          else
            {
              const size_t end = startUpper(high);
              for (size_t r = begin; r < end; ++r)
                if (hits[r].GetFieldEnd() >= low && hits[r].GetFieldEnd() <= high)
                  { ++count; used[r] = 1; }
            }
        }

      // Right hits after this left hit: their starts determine the gap.
      if (direction != OPOBJ::BEFORE && last != limit)
        {
          const GPTYPE next = last+1;
          if (!far)
            mark(startLower(next),startUpper(distance > limit-next ? limit : next+distance));
          else if (distance <= limit-next)
            mark(startLower(next+distance),hits.size());
        }
      if (count) { pairs += count; evidence.AddEntry(hit); }
    }
  size_t active = 0;
  for (size_t r = 0; r < hits.size(); ++r)
    {
      active += starts[r];
      active -= stops[r];
      if (active || (!monotoneEnds && used[r])) evidence.AddEntry(hits[r]);
    }
  return pairs;
}
}
#endif
