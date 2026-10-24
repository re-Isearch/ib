#include "common.hxx"
#include "peerresolver.hxx"
#include "fcache.hxx"

struct MAPPED_PEER_RESOLVER::STATE
{
  struct FIELD
  {
    STRING Name;
    size_t Depth;
    INT FileNumber;
    STRING FileName;
    bool PathChecked = false, HasPath = false;
  };
  IDBOBJ& Db;
  DFDT *Fields;
  FCACHE& Cache;
  size_t& LastPeerField;
  std::vector<FIELD> Descriptors;
  MultiMMapSession::SCOPED_MAPPINGS Mappings;
  MultiMMapSession::STATISTICS Before;
  PEER_RESOLVER_STATISTICS Statistics;
  bool Profile;

  STATE(IDBOBJ& db, FCACHE& cache, size_t& last)
    : Db(db), Fields(db.GetDfdt()), Cache(cache), LastPeerField(last),
      Mappings(sizeof(void*) >= 8 ? cache.RetainMappings() : MultiMMapSession::SCOPED_MAPPINGS()),
      Before(cache.GetMappingStatistics()),
      Profile(db.GetDebugMode())
  {
    const size_t total = Fields ? Fields->GetTotalEntries() : 0;
    Descriptors.reserve(total);
    for (size_t i = 1; i <= total; ++i)
      {
        DFD dfd;
        Fields->GetEntry(i, &dfd);
        const STRING name = dfd.GetFieldName();
        Descriptors.push_back({name, name.Count(), dfd.GetFileNumber()});
      }
  }

  // Same binary search and containing-run traversal as IDB::GetPeerFc.
  size_t Find(const FC& hit, size_t total)
  {
    size_t low = 0, high = total, p = (high + low) / 2, old;
    do
      {
        old = p;
        const FC fc = Cache.GetRecordFc(p);
        if (fc.Contains(hit)) return p + 1;
        if (fc.GetFieldStart() >= hit.GetFieldStart()) high = p;
        else if ((low = p + 1) > high) low = high;
      }
    while ((p = (high + low) / 2) != old);
    return 0;
  }

  FC Resolve(const FC& hit)
  {
    const size_t total = Descriptors.size();
    if (!total) return hit;
    FC peer = hit;
    STRING peerName;
    size_t peerDepth = 0;
    bool first = true, haveDepth = false;
    if (LastPeerField < 1 || LastPeerField > total)
      if ((LastPeerField = total / 3) < 1) LastPeerField = 1;
    const size_t start = LastPeerField - 1;
    for (size_t i = start; i < total + start; ++i)
      {
        const size_t x = i % total;
        FIELD& field = Descriptors[x];
        if (Profile) ++Statistics.FieldTests;
        if (field.Depth > 0 && peerDepth == 0) haveDepth = true;
        else if (haveDepth && field.Depth == 0) continue;
        else if (peerDepth && !field.Name.Contains(peerName)) continue;

        FC range;
        // Ranges can be populated while loading a field, so read the current
        // DFDT entry rather than caching an initially missing range.
        const bool haveRange = Fields->GetFcRange(field.FileNumber, &range);
        if (haveRange && !range.Contains(hit)) continue;
        if (!field.PathChecked)
          {
            field.PathChecked = true;
            field.HasPath = Db.DfdtGetFileName(field.Name, &field.FileName);
          }
        if (!field.HasPath) continue;
        const size_t count = Cache.LoadMappedField(field.Name, field.FileName);
        if (!count) continue;
        if (!haveRange) Cache.GetRange(&range);
        if (Profile) ++Statistics.FieldSearches;
        size_t pos = Find(hit, count);
        if (!pos) continue;
        --pos;
        while (pos > 0 && Cache.GetRecordFc(pos - 1).Contains(hit)) --pos;
        FC fc = Cache.GetRecordFc(pos);
        if (first || (peer.Contains(fc) && fc.Contains(hit)))
          {
            first = false;
            if (peer != fc || field.Name.GetLength() > peerName.GetLength())
              {
                LastPeerField = x + 1;
                peerName = field.Name;
                peerDepth = field.Depth;
              }
            peer = fc;
          }
        for (size_t j = pos + 1; j < count; ++j)
          {
            fc = Cache.GetRecordFc(j);
            if (!fc.Contains(hit)) break;
            if (peer.Contains(fc))
              {
                peer = fc;
                peerName = field.Name;
                peerDepth = field.Depth;
                LastPeerField = x + 1;
              }
          }
      }
    return peer;
  }
};

MAPPED_PEER_RESOLVER::MAPPED_PEER_RESOLVER(IDBOBJ& db, FCACHE& cache, size_t& last)
  : State(new STATE(db, cache, last)) { }
MAPPED_PEER_RESOLVER::~MAPPED_PEER_RESOLVER() = default;
FC MAPPED_PEER_RESOLVER::Resolve(const FC& hit) { return State->Resolve(hit); }
PEER_RESOLVER_STATISTICS MAPPED_PEER_RESOLVER::GetStatistics() const
{
  auto stats = State->Statistics;
  const auto maps = State->Cache.GetMappingStatistics();
  stats.FieldMaps = maps.Mappings - State->Before.Mappings;
  stats.FieldCacheHits = maps.Reuses - State->Before.Reuses;
  stats.FieldMapFailures = maps.Failures - State->Before.Failures;
  return stats;
}
