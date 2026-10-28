#include "common.hxx"
#include "index.hxx"
#include "peerresolver.hxx"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>
#include <vector>

static void require(bool ok, const char *message)
{
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

struct Database : IDBOBJ
{
  DFDT Fields;
  FCACHE Cache;
  std::map<std::string, STRING> Files;
  size_t Last = 0, PathLookups = 0, Contexts = 0;
  bool Debug = true;
  STRING Prefix;
  explicit Database(const char *prefix, size_t budget = sizeof(FC))
    : Cache(this, budget), Prefix(prefix) { }
  DFDT *GetDfdt() override { return &Fields; }
  bool GetDebugMode() const override { return Debug; }
  bool DfdtGetFileName(const STRING& name, STRING *path) override
  {
    ++PathLookups;
    auto i = Files.find(name.c_str());
    if (i == Files.end()) return false;
    *path = i->second; return true;
  }
  bool DfdtGetFieldRange(const STRING& name, FC *range) const override
    { return Fields.GetFcRange(name, range); }
  bool DfdtSetFieldRange(const STRING& name, const FC& range) override
    { return Fields.UpdateFcRange(name, range); }
  void Add(const char *name, const std::vector<FC>& zones, bool hint = true)
  {
    DFD dfd; dfd.SetFieldName(name); Fields.AddEntry(dfd);
    const STRING path = Prefix + STRING().form("-%zu.fc", Files.size());
    FILE *fp = fopen(path,"wb"); require(fp != NULL,"create field-coordinate fixture");
    for (const auto& fc : zones) ::Write(fc, fp);
    require(fclose(fp) == 0,"close field-coordinate fixture");
    Files[name] = path;
    if (hint) for (const auto& fc : zones) Fields.UpdateFcRange(name,fc);
  }
  std::unique_ptr<PEER_RESOLVER> CreatePeerResolver() override
  {
    ++Contexts;
    return std::unique_ptr<PEER_RESOLVER>(new MAPPED_PEER_RESOLVER(*this,Cache,Last));
  }
};

// Independent scalar reference preserving IDB::GetPeerFc's original order,
// field-name filtering, range hints, and contiguous containing-run behavior.
static FC scalar(Database& db, const FC& hit)
{
  const size_t total = db.Fields.GetTotalEntries();
  FC peer = hit; STRING peerName; size_t depth = 0; bool first = true, haveDepth = false;
  if (db.Last < 1 || db.Last > total) if ((db.Last = total / 3) < 1) db.Last = 1;
  const size_t start = db.Last - 1, end = total + start;
  for (size_t i = start; i < end; ++i)
    {
      const size_t x = i % total + 1;
      DFD field; db.Fields.GetEntry(x,&field); STRING name = field.GetFieldName();
      const size_t count = name.Count();
      if (count > 0 && depth == 0) haveDepth = true;
      else if (haveDepth && count == 0) continue;
      else if (depth && !name.Contains(peerName)) continue;
      FC range;
      if (db.Fields.GetFcRange(field.GetFileNumber(),&range) && !range.Contains(hit)) continue;
      const size_t n = db.Cache.LoadFieldCache(name,false);
      if (!n) continue;
      off_t low = 0, high = n, p = (low + high) / 2, previous;
      size_t found = 0;
      do
        {
          previous = p; const FC zone = db.Cache.GetRecordFc(p);
          if (zone.Contains(hit)) { found = p + 1; break; }
          if (zone.GetFieldStart() >= hit.GetFieldStart()) high = p;
          else if ((low = p + 1) > high) low = high;
        }
      while ((p = (low + high) / 2) != previous);
      if (!found) continue;
      size_t pos = found - 1;
      while (pos > 0 && db.Cache.GetRecordFc(pos - 1).Contains(hit)) --pos;
      FC zone = db.Cache.GetRecordFc(pos);
      if (first || (peer.Contains(zone) && zone.Contains(hit)))
        {
          first = false;
          if (peer != zone || name.GetLength() > peerName.GetLength())
            { db.Last = x; depth = (peerName = name).Count(); }
          peer = zone;
        }
      for (size_t j = pos + 1; j < n; ++j)
        {
          zone = db.Cache.GetRecordFc(j);
          if (!zone.Contains(hit)) break;
          if (peer.Contains(zone))
            { peer = zone; depth = (peerName = name).Count(); db.Last = x; }
        }
    }
  return peer;
}

static void populate(Database& db, bool hints)
{
  db.Add("ROOT",{FC(0,999)},hints);
  db.Add("ROOT/P",{FC(0,199),FC(50,149),FC(200,399),FC(400,599)},hints);
  db.Add("ROOT/P/EM",{FC(50,100),FC(60,90),FC(60,90),FC(300,350)},hints);
  db.Add("OTHER/Z",{FC(30,90),FC(70,160),FC(200,250)},hints);
  db.Add("FLAT",{FC(70,80),FC(700,800)},hints);
}

static void equivalence()
{
  for (bool hints : {false,true})
    {
      Database old(hints?"scalar-hint":"scalar"), fast(hints?"resolver-hint":"resolver");
      populate(old,hints); populate(fast,hints);
      for (size_t initial = 0; initial <= 7; ++initial)
        {
          old.Last = fast.Last = initial;
          auto resolver = fast.CreatePeerResolver();
          std::mt19937 random(104729);
          for (size_t i = 0; i < 600; ++i)
            {
              const GPTYPE start = random() % 1200;
              const FC hit(start,start + random() % 50);
              require(resolver->Resolve(hit) == scalar(old,hit),"prepared resolver agrees with scalar containment and field order");
              require(fast.Last == old.Last,"prepared resolver preserves the last-field cursor");
            }
        }
    }
  Database empty("empty"); auto resolver = empty.CreatePeerResolver();
  require(resolver->Resolve(FC(10,12)) == FC(10,12),"empty structural tables retain the input hit");
  Database stream("stream"); stream.Add("P",{FC(0,199),FC(200,399)});
  require(stream.Cache.LoadFieldCache("P",true) == 2,"open a stream-backed field fixture");
  auto disk = stream.CreatePeerResolver();
  require(disk->Resolve(FC(10,12)) == FC(0,199) && disk->Resolve(FC(210,212)) == FC(200,399),
          "prepared resolution also works with the stream backend");
  require(disk->GetStatistics().FieldMaps == 0,"an existing stream does not require a new mapping");
}

static void repeatedLookups()
{
  Database old("thrash-scalar"), fast("thrash-resolver");
  for (auto db : {&old,&fast})
    { db->Add("P",{FC(0,199),FC(200,399)}); db->Add("Q",{FC(0,499)}); db->Add("R",{FC(500,999)}); }
  std::vector<FC> expected;
  auto start = std::chrono::steady_clock::now();
  for (size_t i = 0; i < 395; ++i) expected.push_back(scalar(old,FC((i % 190)*2+5,(i % 190)*2+7)));
  const double oldMs = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  const auto before = fast.Cache.GetMappingStatistics();
  PEER_RESOLVER_STATISTICS stats;
  start = std::chrono::steady_clock::now();
  {
    auto resolver = fast.CreatePeerResolver();
    for (size_t i = 0; i < expected.size(); ++i)
      require(resolver->Resolve(FC((i % 190)*2+5,(i % 190)*2+7)) == expected[i],"mapping reuse preserves all 395 resolutions");
    stats = resolver->GetStatistics();
    require(fast.Cache.GetMappingStatistics().Evictions == before.Evictions,"field mappings survive the complete operation");
  }
  const double newMs = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  require(stats.FieldMaps == 2 && fast.PathLookups == 2,"each needed field is mapped and resolved by name only once");
  require(stats.FieldTests + stats.ContainerResultHits*3 == 395*3 &&
          stats.ContainerResultHits > 300 && stats.FieldSearches < 395 && stats.FieldResultHits > 0,
          "profile separates whole resolutions from per-field search and mapping reuse");
  require(old.Cache.GetMappingStatistics().Mappings > stats.FieldMaps*100,"scalar fixture reproduces repeated eviction and remapping");
  require(fast.Cache.CurrentMappedBytes() <= 2*sizeof(FC),"normal mapping budget is restored, allowing one oversized table");
  require(fast.Cache.GetTotal() && !fast.Cache.GetRecordFc(0).IsEmpty(),"active field remains valid after trimming the pool");
  std::cout << "395 lookups: scalar " << old.Cache.GetMappingStatistics().Mappings << " maps / " << old.PathLookups
            << " path lookups / " << oldMs << " ms; prepared " << stats.FieldMaps << " maps / " << fast.PathLookups
            << " path lookups / " << newMs << " ms\n";
}

static void searchWindows()
{
  for (bool hints : {false,true})
    {
      Database old("window-scalar"), fast("window-resolver");
      populate(old,hints); populate(fast,hints);
      for (size_t initial = 0; initial <= 7; ++initial)
        {
          old.Last = fast.Last = initial;
          auto resolver = fast.CreatePeerResolver();
          for (GPTYPE start = 0; start < 450; ++start)
            for (GPTYPE length : {GPTYPE(0),GPTYPE(1),GPTYPE(7),GPTYPE(25),GPTYPE(100),GPTYPE(400)})
              {
                const FC hit(start,start+length);
                require(resolver->Resolve(hit) == scalar(old,hit),
                        "cached search regions preserve nested, crossing and boundary containment");
                require(fast.Last == old.Last,"replayed field runs preserve cursor-dependent selection");
              }
        }
    }
  Database gaps("gap-windows");
  gaps.Add("P",{FC(0,9999)});
  gaps.Add("Q",{FC(0,10),FC(9000,9999)});
  auto resolver = gaps.CreatePeerResolver();
  for (GPTYPE i = 100; i < 2100; ++i)
    require(resolver->Resolve(FC(i,i+2)) == FC(0,9999),"cached misses retain the containing parent field");
  auto stats = resolver->GetStatistics();
  require(stats.FieldSearches == 2 && stats.FieldTests == 2 && stats.ContainerResultHits == 1999,
          "nearby hits reuse a complete resolution including proven misses without walking the field list");

  Database longRun("long-window-run");
  longRun.Add("P",std::vector<FC>(70,FC(0,9999)));
  auto capped = longRun.CreatePeerResolver();
  require(capped->Resolve(FC(10,12)) == FC(0,9999) && capped->Resolve(FC(20,22)) == FC(0,9999),
          "long duplicate runs retain their normal containment result");
  require(capped->Resolve(FC(10001,10002)) == FC(10001,10002) &&
          capped->Resolve(FC(30,32)) == FC(0,9999),
          "leaving the complete search region still checks a long run normally");
  require(capped->GetStatistics().FieldSearches == 2 && capped->GetStatistics().FieldResultHits == 0 &&
          capped->GetStatistics().ContainerResultHits == 1,
          "long runs reuse a constant-size final decision while retaining the per-field run cap");

  // The same containing span can hide children; replay must require the
  // complete search region, not just containment in the previous answer.
  Database old("edge-scalar"), fast("edge-resolver");
  const GPTYPE top = std::numeric_limits<GPTYPE>::max();
  for (auto db : {&old,&fast})
    {
      db->Add("P",{FC(0,999),FC(top-999,top)});
      db->Add("P/CHILD",{FC(100,200),FC(400,450),FC(top-800,top-700)});
      db->Add("DUPLICATES",std::vector<FC>(70,FC(0,999)));
    }
  auto edges = fast.CreatePeerResolver();
  const std::vector<FC> hits = {FC(0,0),FC(50,51),FC(100,101),FC(150,200),FC(150,201),
    FC(300,301),FC(420,421),FC(450,451),FC(998,999),FC(999,1000),FC(1000,1001),
    FC(top-999,top-998),FC(top-800,top-799),FC(top-750,top-700),FC(top-750,top-699),
    FC(top-1,top),FC(top,top),FC(20,10),FC()};
  for (size_t repeat = 0; repeat < 3; ++repeat)
    for (const auto& hit : hits)
      {
        require(edges->Resolve(hit) == scalar(old,hit),"search regions handle extrema, empty hits and long duplicate runs");
        require(fast.Last == old.Last,"edge lookups preserve field traversal state");
      }
}

static void completeResolutions()
{
  // A cached absence must return this hit, not the hit that established it.
  Database gaps("whole-misses");
  gaps.Add("P",{FC(0,10)}); gaps.Add("Q",{FC(9000,9999)});
  auto resolver = gaps.CreatePeerResolver();
  require(resolver->Resolve(FC(100,102)) == FC(100,102) &&
          resolver->Resolve(FC(200,202)) == FC(200,202),
          "complete cached misses retain each input interval");
  require(resolver->GetStatistics().ContainerResultHits == 1 &&
          resolver->GetStatistics().FieldTests == 2,
          "range-hint rejections also establish reusable complete search regions");

  Database hints("whole-hint-changes");
  hints.Add("P",{FC(0,999)},false);
  hints.Fields.UpdateFcRange("P",FC(0,10));
  auto changing = hints.CreatePeerResolver();
  require(changing->Resolve(FC(100,102)) == FC(100,102),"an initial range hint rejects the hit");
  hints.Fields.UpdateFcRange("P",FC(0,999));
  require(changing->Resolve(FC(200,202)) == FC(0,999),
          "updated range hints invalidate a complete cached absence");
  require(changing->Resolve(FC(300,302)) == FC(0,999),"the updated hint permits exact reuse");
  const size_t beforeClear = changing->GetStatistics().ContainerResultHits;
  require(hints.Fields.InvalidateFcRanges("whole-hint-changes"),"clear the fixture range hints");
  require(changing->Resolve(FC(400,402)) == FC(0,999) &&
          changing->GetStatistics().ContainerResultHits == beforeClear,
          "clearing range hints also invalidates the complete resolution proof");

  Database missing("whole-missing-table");
  missing.Add("A",{FC(200,299)},false); missing.Add("B",{FC(0,999)});
  const STRING missingPath = missing.Files["A"];
  require(std::remove(missingPath.c_str()) == 0,"temporarily remove one field table");
  auto retry = missing.CreatePeerResolver();
  require(retry->Resolve(FC(210,212)) == FC(0,999) &&
          retry->Resolve(FC(220,222)) == FC(0,999),"unavailable tables retain the normal fallback");
  FILE *restored = fopen(missingPath,"wb");
  require(restored != NULL,"restore the temporarily unavailable field table");
  ::Write(FC(200,299),restored);
  require(fclose(restored) == 0,"finish restoring the field table");
  require(missing.Cache.LoadMappedField("B",missing.Files["B"]) == 1,
          "switch the active field cache so its normal load path can retry the restored table");
  require(retry->Resolve(FC(230,232)) == FC(200,299),
          "a failed table read is retried instead of reused as a complete absence");

  // Vary table geometry, traversal cursor, hit length and access order, rather
  // than relying only on the hand-written nesting fixture. Keep all mappings
  // within the normal budget so this tests selection, not remapping speed.
  std::mt19937 random(15485863);
  for (bool hints : {false,true})
    for (bool high : {false,true})
      {
        Database old("whole-oracle",8*1024*1024), fast("whole-resolver",8*1024*1024);
        const GPTYPE base = high ? std::numeric_limits<GPTYPE>::max()-20000 : 0;
        for (size_t f = 0; f < 18; ++f)
          {
            std::vector<FC> zones;
            for (size_t j = 0; j < 40; ++j)
              {
                const GPTYPE start = base + random()%12000;
                const GPTYPE end = start + random()%1500;
                zones.emplace_back(start,end);
                if (j%13 == 0) zones.push_back(zones.back());
              }
            std::sort(zones.begin(),zones.end(),[](const FC& a,const FC& b)
              { return a.GetFieldStart() != b.GetFieldStart() ?
                  a.GetFieldStart() < b.GetFieldStart() : a.GetFieldEnd() < b.GetFieldEnd(); });
            const STRING name = f%3 ? STRING().form("F%02zu/P%zu",f/3,f%3) : STRING().form("F%02zu",f/3);
            old.Add(name.c_str(),zones,hints); fast.Add(name.c_str(),zones,hints);
          }
        auto prepared = fast.CreatePeerResolver();
        GPTYPE position = 0;
        for (size_t i = 0; i < 6000; ++i)
          {
            if (i%17 == 0) old.Last = fast.Last = random()%21;
            if (i%24 == 0) position = random()%14000;
            else position = (position+1)%14000;
            const GPTYPE length = i%5 ? 2 : random()%1000;
            const FC hit(base+position,base+position+length);
            require(prepared->Resolve(hit) == scalar(old,hit),
                    "complete resolution regions agree with scalar selection over randomized field tables");
            require(fast.Last == old.Last,
                    "complete resolution reuse preserves externally changed cursors and field traversal order");
          }
        require(prepared->GetStatistics().ContainerResultHits > 0,
                "randomized streams exercise complete resolution reuse");
      }
}

static void sparseFields(bool benchmark = false)
{
  const size_t fields = benchmark ? 324 : 64, records = benchmark ? 512 : 32, perRecord = 32;
  Database db("sparse-fields");
  for (size_t field = 0; field < fields; ++field)
    {
      std::vector<FC> zones;
      for (size_t record = 0; record < records; ++record)
        {
          const GPTYPE base = record * 100000;
          zones.emplace_back(base,base + (field ? 500 : 99999));
        }
      db.Add(STRING().form("F%03zu",field).c_str(),zones);
    }
  const auto start = std::chrono::steady_clock::now();
  auto resolver = db.CreatePeerResolver();
  for (size_t record = 0; record < records; ++record)
    for (size_t hit = 0; hit < perRecord; ++hit)
      {
        const GPTYPE base = record * 100000, pos = base+1000+hit*100;
        require(resolver->Resolve(FC(pos,pos+4)) == FC(base,base+99999),
                "many sparse fields preserve the parent container for every driver hit");
      }
  const double ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  const auto stats = resolver->GetStatistics();
  if (!benchmark)
    require(stats.ContainerResultHits > records*perRecord*9/10 &&
            stats.FieldTests < records*perRecord*fields/8,
            "sparse fields avoid whole descriptor walks within proven coordinate regions");
  std::cout << fields << " fields, " << records*perRecord << " hits: " << stats.FieldTests
            << " tests, " << stats.FieldSearches << " table searches, "
            << stats.ContainerResultHits << " complete resolution reuses, " << ms << " ms\n";
}

static void lifetimes()
{
  Database db("scoped");
  db.Add("A",{FC(0,20)}); db.Add("B",{FC(10,30)}); db.Add("C",{FC(20,40)});
  MultiMMapSession pool(sizeof(FC));
  {
    auto first = pool.RetainMappings();
    const auto a = pool.GetMemoryBase(db.Files["A"]);
    {
      auto second = pool.RetainMappings(); auto moved = std::move(second);
      require(pool.GetMemoryBase(db.Files["B"]) != NULL,"nested scope maps another field");
    }
    require(pool.GetMemoryBase(db.Files["C"]) != NULL && pool.CurrentBytes() == 3*sizeof(FC),
            "inner scope does not trim mappings held by an outer scope");
    require(a && getGPTYPE(a,0) == 0,"an early field view remains readable");
  }
  require(pool.CurrentBytes() == sizeof(FC) && pool.Size(db.Files["C"]) == sizeof(FC),"outer scope restores the LRU budget");
  try { auto hold = pool.RetainMappings(); pool.GetMemoryBase(db.Files["A"]); throw std::runtime_error("fixture"); }
  catch (const std::runtime_error&) { }
  require(pool.CurrentBytes() == sizeof(FC),"exception unwinding restores the mapping budget");
  {
    auto hold = pool.RetainMappings();
    pool.Invalidate(db.Files["A"]); require(pool.Size(db.Files["A"]) == 0,"explicit invalidation remains authoritative");
    pool.Clear(); pool.GetMemoryBase(db.Files["A"]); pool.GetMemoryBase(db.Files["B"]);
    require(pool.CurrentBytes() == 2*sizeof(FC),"clear does not end an active scope");
  }
  require(pool.CurrentBytes() == sizeof(FC),"a cleared pool still trims when its scope ends");
}

static atomicIRSET result(Database& db, const std::vector<IRESULT::hit_type>& hits)
{
  atomicIRSET set(&db); IRESULT row; row.SetIndex(1); row.SetAuxCount(1); row.SetScore(1);
  HITTABLE table; for (const auto& hit : hits) table.AddEntry(hit);
  row.SetHitTable(table); row.SetHitCount(hits.size()); set.FastAddEntry(row);
  return atomicIRSET(set);
}

static std::string peerProfile;
static int captureProfile(int, const char *message)
{
  if (std::string(message).find("PEER profile:") != std::string::npos) peerProfile = message;
  return 0;
}

static void integration()
{
  using Hit = IRESULT::hit_type;
  Database db("peer-context");
  require(_globalMessageLogger.Init(LOG_ALL,"peer-resolver",captureProfile),"enable resolver profile capture");
  db.Add("P",{FC(0,199),FC(200,399)}); db.Add("Q",{FC(0,499)});
  {
    auto left = result(db,{Hit(10,12,1),Hit(198,199,1)});
    auto right = result(db,{Hit(20,22,2),Hit(200,201,2)});
    left.Peer(right,GPTYPE(7));
    require(db.Contexts == 1,"one bounded operation creates one shared resolver");
    require(left.GetTotalEntries() == 1 && left[0].GetHitCount() == 1,
            "the prepared resolver rejects nearby hits across a structural boundary");
    require(left[0].GetHitTable().Size() == 2 && left[0].GetScore() == 2,
            "prepared structural lookup preserves evidence and score combination");
    require(db.Cache.CurrentMappedBytes() <= 2*sizeof(FC),"the bounded operation releases retained mappings");
    require(peerProfile.find("field_maps=2 ") != std::string::npos &&
            peerProfile.find("field_map_failures=0") != std::string::npos,
            "bounded debug profiles report statistics from the prepared resolver");
  }
  _globalMessageLogger.Init(static_cast<_MessageFunc_t>(NULL));
  _globalMessageLogger.Init(LOG_ALL & ~LOG_DEBUG,"peer-resolver",stderr);
  db.Debug = false;
  {
    const size_t contexts = db.Contexts;
    auto left = result(db,{Hit(10,12,1)}), right = result(db,{Hit(20,22,2)});
    left.Peer(right,GPTYPE(6));
    require(left.GetTotalEntries() == 0 && db.Contexts == contexts,
            "distance rejections do not build a structural resolver");
  }
  for (bool before : {false,true})
    {
      auto left = result(db,{Hit(20,22,1)}), right = result(db,{Hit(10,12,2)});
      if (before) left.BeforePeer(right,GPTYPE(7)); else left.AfterPeer(right,GPTYPE(7));
      require(left.GetTotalEntries() == (before?1:0),"ordered peers use the same prepared structural resolver");
    }
  {
    const size_t contexts = db.Contexts;
    auto left = result(db,{Hit(10,12,1),Hit(198,199,1)});
    auto right = result(db,{Hit(20,22,2),Hit(200,201,2)});
    left.Peer(right);
    require(db.Contexts == contexts+1 && left.GetTotalEntries() == 1 && left[0].GetHitCount() == 2,
            "unbounded peers also reuse the resolver and keep their distance-free pair count");
  }
  require(_globalMessageLogger.Init(LOG_ALL,"peer-resolver",captureProfile),"enable unbounded profile capture");
  db.Debug = true;
  for (int direction : {0,1,-1})
    {
      peerProfile.clear();
      auto left = result(db,{Hit(10,12,1),Hit(198,199,1)}), right = result(db,{Hit(20,22,2),Hit(200,201,2)});
      if (direction == 1) left.BeforePeer(right);
      else if (direction == -1) left.AfterPeer(right);
      else left.Peer(right);
      require(peerProfile.find("Unbounded PEER profile:") != std::string::npos &&
              peerProfile.find("records=1 left_hits=2 right_hits=2 positional_hits=2 container_lookups=2") != std::string::npos &&
              peerProfile.find("field_result_hits=") != std::string::npos &&
              peerProfile.find("container_result_hits=") != std::string::npos,
              "unbounded and ordered debug profiles expose actual structural work and cached search results");
    }
  _globalMessageLogger.Init(static_cast<_MessageFunc_t>(NULL));
  _globalMessageLogger.Init(LOG_ALL & ~LOG_DEBUG,"peer-resolver",stderr);
}

int main(int argc, char **argv)
{
  if (argc == 2 && std::string(argv[1]) == "--benchmark") { sparseFields(true); return 0; }
  equivalence(); repeatedLookups(); searchWindows(); completeResolutions(); sparseFields(); lifetimes(); integration();
  std::cout << "Prepared peer resolution, mapping reuse and lifetime regressions passed\n";
}
