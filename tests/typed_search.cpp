#include "common.hxx"
#include "fchits.hxx"
#include "integerlist.hxx"
#include "monetarylist.hxx"
#include "numbers.hxx"
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


static void monetary_regressions()
{
  const MONETARYOBJ whole("12");
  const MONETARYOBJ exact("12.00");
  const MONETARYOBJ negative("-12.37");
  const MONETARYOBJ grouped("$1,234");
  const MONETARYOBJ grouped_exact("$1,234.00");
  const MONETARYOBJ invalid("12.");

  require(whole.Ok() && whole.GetTicks() == INT16(12) * 50000 &&
          !whole.HasExplicitFraction(), "whole monetary lexical intent");
  require(exact.Ok() && exact.GetTicks() == whole.GetTicks() &&
          exact.HasExplicitFraction(), "explicit monetary fraction intent");
  require(negative.Ok() && negative.GetTicks() == -INT16(618500),
          "signed monetary ticks");
  require(grouped.Ok() && grouped.GetTicks() == INT16(1234) * 50000 &&
          !grouped.HasExplicitFraction(), "currency grouping remains grouping");
  require(grouped_exact.Ok() &&
          grouped_exact.GetTicks() == INT16(1234) * 50000 &&
          grouped_exact.HasExplicitFraction(), "grouped exact monetary value");
  require(!invalid.Ok(), "trailing monetary decimal remains invalid");

  const MONETARYRANGE range("-12.50--10.25");
  require(range.Ok() && range.GetStart().GetTicks() == -INT16(625000) &&
          range.GetEnd().GetTicks() == -INT16(512500),
          "signed monetary range parsing");

  const STRING file("typed-search-monetary.column");
  UnlinkFile(file);

  const char *source[] = {
    "11.99", "12.00", "12.10", "12.99", "13.00",
    "-12.99", "-12.37", "-12.00", "-11.99",
    "-0.50", "0.00", "0.75"
  };

  MONETARYLIST list;
  FILE *out = list.OpenForAppend(file);
  require(out != nullptr, "open monetary fixture");

  std::vector<INT16> values;
  for (size_t i = 0; i < sizeof(source)/sizeof(source[0]); ++i)
    {
      const MONETARYOBJ value(source[i]);
      require(value.Ok(), "parse monetary fixture value");
      values.push_back(value.GetTicks());
      require(MONETARYFLD(FC(i * 100 + 1, i * 100 + 2),
                          value.GetTicks()).Write(out),
              "write monetary geometry and ticks");
    }

  require(fclose(out) == 0 && list.WriteIndex(file),
          "build monetary fixture");

  MultiMMapSession maps;

  const auto check_bucket = [&](INT16 low, INT16 high,
                                std::vector<size_t> expected_indexes,
                                const char *message)
  {
    using hit = std::pair<GPTYPE, MONETARYFLD::span_type>;
    std::vector<hit> expected, actual;

    for (size_t i : expected_indexes)
      expected.emplace_back(i * 100 + 1, 1);

    require(list.VisitMappedRange(
        file, low, high, ZRelEQ, maps,
        [&](GPTYPE gp, MONETARYFLD::span_type span)
        {
          actual.emplace_back(gp, span);
        }), message);

    require(actual == expected, message);
  };

  check_bucket(INT16(12) * 50000,
               INT16(13) * 50000 - 1,
               {1, 2, 3},
               "positive whole-unit monetary bucket");

  check_bucket(-INT16(13) * 50000 + 1,
               -INT16(12) * 50000,
               {5, 6, 7},
               "negative whole-unit monetary bucket");

  check_bucket(-INT16(50000) + 1,
               INT16(50000) - 1,
               {9, 10, 11},
               "zero monetary bucket spans both signs");

  std::vector<std::pair<GPTYPE, MONETARYFLD::span_type>> ranged;
  require(list.VisitMappedRange(
      file,
      MONETARYOBJ("12.10").GetTicks(),
      MONETARYOBJ("12.99").GetTicks(),
      ZRelEQ,
      maps,
      [&](GPTYPE gp, MONETARYFLD::span_type span)
      {
        ranged.emplace_back(gp, span);
      }),
      "monetary range visit");
  require(ranged == std::vector<std::pair<GPTYPE, MONETARYFLD::span_type>>{
          {201, 1}, {301, 1}},
          "monetary inclusive range retains GP order");

  maps.Clear();
  UnlinkFile(file);
}

int main()
{
  aggregation_regressions();
  integer_regressions();
  monetary_regressions();
  std::cout << "Typed search regressions passed\n";
}
