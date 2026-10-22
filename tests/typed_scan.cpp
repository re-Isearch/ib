#include "integerlist.hxx"
#include "monetarylist.hxx"
#include "typedscan.hxx"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

static void require(bool ok, const char *message)
{
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

struct Expected {
  const char *term;
  size_t frequency;
};

struct ScanDatabase {
  FIELDTYPE type;
  STRING filename;
  int error = 0;
  unsigned field_reads = 0, file_reads = 0;
  bool resolve = true;

  FIELDTYPE GetFieldType(const STRING&) { ++field_reads; return type; }
  bool DfdtGetFileName(const STRING&, const FIELDTYPE& requested, STRING *out) {
    ++file_reads;
    require(requested == type, "resolve the native datatype sidecar");
    *out = filename;
    return resolve;
  }
  void SetErrorCode(int code) { error = code; }
};

static void check(const SCANLIST& list, const std::vector<Expected>& expected)
{
  require(list.GetTotalEntries() == expected.size(), "distinct scan term count");
  for (size_t i = 0; i < expected.size(); ++i)
    {
      const SCANOBJ entry = list.GetEntry(i + 1);
      require(entry.Term() == expected[i].term, "exact terms in numeric order");
      require(entry.Frequency() == expected[i].frequency, "occurrence frequency including singletons");
    }
}

static void check(const STRLIST& list, const std::vector<Expected>& expected)
{
  require(list.GetTotalEntries() == expected.size(), "string scan term count");
  size_t i = 0;
  for (const STRLIST *p = list.Next(); p != &list; p = p->Next())
    require(p->Value() == expected[i++].term, "exact string terms in numeric order");
}

template <class List>
static void write_column(const STRING& file, const std::vector<INT16>& values)
{
  file.Unlink();
  List list;
  FILE *out = list.OpenForAppend(file);
  require(out != nullptr, "open native scan fixture");
  // Insert in descending order to ensure the scan follows the value block.
  for (size_t i = 0; i < values.size(); ++i)
    require(INTEGERFLD(FC(i * 100 + 1, i * 100 + 2), values[values.size()-i-1]).Write(out),
            "write native scan value");
  require(fclose(out) == 0 && list.WriteIndex(file), "build native scan fixture");
}

template <class Traits, class Format>
static void scan_regressions(INT type, const STRING& file, const std::vector<Expected>& expected,
                             Format format)
{
  ScanDatabase db{FIELDTYPE(type), file};
  MultiMMapSession maps;
  SCANLIST scan;
  require(typed_scan::ScanField<Traits>(&db, &scan, "VALUE", FIELDTYPE(type), 0, -1, maps, format)
            == expected.size(), "scan all distinct native values");
  require(db.error == 0, "native scan succeeds");
  check(scan, expected);

  STRLIST strings;
  require(typed_scan::ScanField<Traits>(&db, &strings, "VALUE", FIELDTYPE(type), 0, -1, maps, format)
            == expected.size(), "scan native values without frequencies");
  check(strings, expected);

  SCANLIST snapshot(scan);
  scan.FastAddEntry("tail", 7);
  check(snapshot, expected);
  std::vector<Expected> extended(expected);
  extended.push_back({"tail", 7});
  check(scan, extended);

  scan.Clear();
  require(typed_scan::ScanField<Traits>(&db, &scan, "VALUE", FIELDTYPE(type), 0, 1, maps, format)
            == 1, "one requested value");
  check(scan, {expected[0]});
  scan.Clear();
  require(typed_scan::ScanField<Traits>(&db, &scan, "VALUE", FIELDTYPE(type), 2, 2, maps, format)
            == 2, "start within an equal-value run");
  check(scan, {{expected[1].term, 1}, expected[2]});

  strings.Clear();
  require(typed_scan::ScanField<Traits>(&db, &strings, "VALUE", FIELDTYPE(type), 2, 2, maps, format)
            == 2, "string scan start and limit");
  check(strings, {{expected[1].term, 1}, expected[2]});

  db.field_reads = db.file_reads = 0;
  require(typed_scan::ScanField<Traits>(&db, &scan, "VALUE", FIELDTYPE(type), 0, 0, maps, format)
            == 0 && db.field_reads == 0 && db.file_reads == 0,
          "zero request does not access metadata or storage");
  check(scan, {{expected[1].term, 1}, expected[2]});
  scan.Clear();
  require(typed_scan::ScanField<Traits>(&db, &scan, "VALUE", FIELDTYPE(type),
            std::numeric_limits<size_t>::max(), -1, maps, format) == 0 && scan.IsEmpty(),
          "out-of-range start is empty");
  require(typed_scan::ScanField<Traits>(&db, (SCANLIST *)nullptr, "VALUE", FIELDTYPE(type),
            0, -1, maps, format) == 0, "null output is safe");
  require(typed_scan::ScanField<Traits>((ScanDatabase *)nullptr, &scan, "VALUE", FIELDTYPE(type),
            0, -1, maps, format) == 0, "null database is safe");

  db.type = FIELDTYPE::text;
  db.file_reads = 0;
  require(typed_scan::ScanField<Traits>(&db, &scan, "VALUE", FIELDTYPE(type), 0, -1, maps, format)
            == 0 && db.file_reads == 0, "incompatible field type does not open a column");
  db.type = FIELDTYPE(type);
  db.resolve = false;
  require(typed_scan::ScanField<Traits>(&db, &scan, "VALUE", FIELDTYPE(type), 0, -1, maps, format)
            == 0 && db.error == 123 && scan.IsEmpty(), "failed filename resolution reports an error");
}

static void monetary_format_regressions(INT16 minimum, INT16 maximum, INT16 large)
{
  require(MONETARYOBJ().ToString().IsEmpty(), "invalid currency has no scan term");
  const INT16 ticks[] = {minimum, -500001, -500000, -1, 0, 1, 2, 625000, large, large+1, maximum};
  const char *terms[] = {
    "-3402823669209384634633746074317682.11456", "-10.00002", "-10.00", "-0.00002",
    "0.00", "0.00002", "0.00004", "12.50", "25353012004564588029934064.10752",
    "25353012004564588029934064.10754", "3402823669209384634633746074317682.11454"
  };
  for (size_t i = 0; i < sizeof(ticks)/sizeof(ticks[0]); ++i)
    {
      MONETARYOBJ value;
      value.SetTicks(ticks[i]);
      require(value.ToString() == terms[i], "full-range exact currency formatting");
      const MONETARYOBJ restored(value.ToString());
      require(restored.Ok() && restored.GetTicks() == ticks[i] && restored.HasExplicitFraction(),
              "currency scan term round-trips with exact query semantics");
    }
}

static void storage_regressions(const STRING& integer_file, const STRING& currency_file)
{
  MultiMMapSession maps;
  SCANLIST scan;
  ScanDatabase db{FIELDTYPE::integer, currency_file};
  require(typed_scan::ScanField<INTEGER_INDEX_TRAITS>(&db, &scan, "VALUE", FIELDTYPE::integer,
            0, -1, maps, typed_scan::IntegerTerm()) == 0 && db.error == 123,
          "integer scan rejects monetary storage identity");
  db.type = FIELDTYPE::currency;
  db.filename = integer_file;
  db.error = 0;
  require(typed_scan::ScanField<MONETARY_INDEX_TRAITS>(&db, &scan, "VALUE", FIELDTYPE::currency,
            0, -1, maps, typed_scan::CurrencyTerm()) == 0 && db.error == 123,
          "currency scan rejects integer storage identity");

  const STRING empty_file("typed-scan-empty.column");
  write_column<INTEGERLIST>(empty_file, {});
  db.type = FIELDTYPE::integer;
  db.filename = empty_file;
  db.error = 0;
  require(typed_scan::ScanField<INTEGER_INDEX_TRAITS>(&db, &scan, "VALUE", FIELDTYPE::integer,
            0, -1, maps, typed_scan::IntegerTerm()) == 0 && db.error == 0 && scan.IsEmpty(),
          "empty indexed column is a successful empty scan");
  maps.Clear();
  empty_file.Unlink();
  db.error = 0;
  require(typed_scan::ScanField<INTEGER_INDEX_TRAITS>(&db, &scan, "VALUE", FIELDTYPE::integer,
            0, -1, maps, typed_scan::IntegerTerm()) == 0 && db.error == 123,
          "missing sidecar reports an error");

  const STRING broken_file("typed-scan-broken.column");
  FILE *out = fopen(broken_file, "wb");
  require(out && fwrite("bad", 1, 3, out) == 3 && fclose(out) == 0, "create malformed sidecar");
  db.filename = broken_file;
  db.error = 0;
  require(typed_scan::ScanField<INTEGER_INDEX_TRAITS>(&db, &scan, "VALUE", FIELDTYPE::integer,
            0, -1, maps, typed_scan::IntegerTerm()) == 0 && db.error == 123 && scan.IsEmpty(),
          "malformed sidecar is rejected");
  maps.Clear();
  broken_file.Unlink();
}

int main()
{
  const INT16 maximum = INT16((UINT16(1) << 127) - 1), minimum = -maximum - 1;
  const INT16 large = INT16(UINT16(1) << 100);
  monetary_format_regressions(minimum, maximum, large);

  const STRING integer_file("typed-scan-integer.column"), currency_file("typed-scan-currency.column");
  write_column<INTEGERLIST>(integer_file, {minimum, -100, -100, -2, -1, 0, 0, 2, 10, 10, large, large+1, maximum});
  scan_regressions<INTEGER_INDEX_TRAITS>(FIELDTYPE::integer, integer_file,
    {{"-170141183460469231731687303715884105728", 1}, {"-100", 2}, {"-2", 1}, {"-1", 1},
     {"0", 2}, {"2", 1}, {"10", 2}, {"1267650600228229401496703205376", 1},
     {"1267650600228229401496703205377", 1}, {"170141183460469231731687303715884105727", 1}},
    typed_scan::IntegerTerm());
  write_column<MONETARYLIST>(currency_file, {minimum, -500000, -500000, -1, 0, 0, 1, 2,
                                            500000, 500001, 500001, large, large+1, maximum});
  scan_regressions<MONETARY_INDEX_TRAITS>(FIELDTYPE::currency, currency_file,
    {{"-3402823669209384634633746074317682.11456", 1}, {"-10.00", 2}, {"-0.00002", 1},
     {"0.00", 2}, {"0.00002", 1}, {"0.00004", 1}, {"10.00", 1}, {"10.00002", 2},
     {"25353012004564588029934064.10752", 1}, {"25353012004564588029934064.10754", 1},
     {"3402823669209384634633746074317682.11454", 1}}, typed_scan::CurrencyTerm());
  storage_regressions(integer_file, currency_file);
  integer_file.Unlink();
  currency_file.Unlink();
  std::cout << "Native integer/currency scans, exact terms, counts and paging passed\n";
}
