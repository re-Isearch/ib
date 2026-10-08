#include "common.hxx"
#include "fchits.hxx"
#include "integerlist.hxx"
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

static void require(bool ok, const char* message)
{
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

static void aggregation_regressions()
{
  for (bool legacy : {false, true}) {
    FCHITS hits;
    size_t growths = 0, capacity = 0;
    for (size_t i = 0; i < 4096; ++i) {
      const FC fc(i * 3 + 1, i * 3 + 2);
      if (legacy) {
        FCT one(fc);
        hits.Append(one);
      } else {
        FCHITS one{FCHIT(fc)};
        hits.Append(one);
      }
      if (hits.Capacity() != capacity) { ++growths; capacity = hits.Capacity(); }
    }
    require(hits.Size() == 4096 && hits.IsNormalized(), "ordered singleton aggregation");
    require(growths < 64, "singleton appends must retain amortized buffer growth");
    for (size_t i = 0; i < hits.Size(); ++i)
      require(hits[i].GetFieldStart() == i * 3 + 1, "aggregation retains coordinates");
    hits.Append(hits);
    hits.Normalize();
    require(hits.Size() == 4096, "self-append remains safe and normalizable");
  }
  HITTABLE original;
  original.AddEntry(FCHIT(FC(1, 2)));
  HITTABLE snapshot(original);
  original.AddEntry(FCHIT(FC(3, 4)));
  require(snapshot.Size() == 1 && original.Size() == 2, "copy-on-write snapshot isolation");
}

static bool matches(INT16 value, INT16 key, ZRelation_t relation)
{
  switch (relation) {
    case ZRelEQ: return value == key;
    case ZRelNE: return value != key;
    case ZRelLT: return value < key;
    case ZRelLE: return value <= key;
    case ZRelGT: return value > key;
    case ZRelGE: return value >= key;
    default: return false;
  }
}

static void integer_regressions()
{
  const STRING file("typed-search-regression.column");
  UnlinkFile(file);
  std::vector<INT16> values;
  for (size_t i = 0; i < 64; ++i) values.push_back(INT16(i % 16) - 8);
  const INT16 maximum = INT16((UINT16(1) << 127) - 1);
  const INT16 minimum = -maximum - 1;
  values.push_back(minimum); values.push_back(maximum);
  INTEGERLIST list;
  FILE* out = list.OpenForAppend(file);
  require(out != nullptr, "open integer fixture");
  for (size_t i = 0; i < values.size(); ++i)
    require(INTEGERFLD(FC(i * 100 + 1, i * 100 + 1 + i % 3), values[i]).Write(out),
            "write integer geometry and signed value");
  require(fclose(out) == 0 && list.WriteIndex(file), "build integer fixture");
  MultiMMapSession maps;
  for (INT16 key : {minimum, maximum, INT16(-100), INT16(-8), INT16(-7),
                   INT16(-1), INT16(0), INT16(7), INT16(8), INT16(100)}) {
    for (ZRelation_t relation : {ZRelEQ, ZRelNE, ZRelLT, ZRelLE, ZRelGT, ZRelGE}) {
      using hit = std::pair<GPTYPE, INTEGERFLD::span_type>;
      std::vector<hit> expected, actual;
      for (size_t i = 0; i < values.size(); ++i)
        if (matches(values[i], key, relation)) expected.emplace_back(i * 100 + 1, i % 3);
      require(list.VisitMappedMatches(file, key, relation, maps,
          [&](GPTYPE gp, INTEGERFLD::span_type span) { actual.emplace_back(gp, span); }),
          "visit integer relation");
      require(actual == expected, "selective and broad integer ranges retain GP order and spans");
    }
  }
  maps.Clear();
  UnlinkFile(file);
}

int main()
{
  aggregation_regressions();
  integer_regressions();
  std::cout << "Typed search regressions passed\n";
}
