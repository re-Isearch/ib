#include "integerlist.hxx"
#include "magic.hxx"
#include "typedscan.hxx"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

static void require(bool ok, const char *message)
{
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

using Rows = std::vector<INTEGERFLD>;
using Hit = std::pair<GPTYPE, INTEGERFLD::span_type>;
const INT16 smin = std::numeric_limits<INT4>::min();
const INT16 smax = std::numeric_limits<INT4>::max();
const INT16 umax = std::numeric_limits<UINT4>::max();
const INT16 maximum = INT16((UINT16(1) << 127) - 1);
const INT16 minimum = -maximum - 1;

static std::vector<BYTE> bytes(const STRING& file)
{
  FILE *fp = fopen(file, "rb");
  require(fp != nullptr, "open column bytes");
  std::vector<BYTE> data(size_t(GetFileSize(fp)));
  require(fread(data.data(), 1, data.size(), fp) == data.size(), "read column bytes");
  fclose(fp);
  return data;
}

static void write_bytes(const STRING& file, const std::vector<BYTE>& data)
{
  FILE *fp = fopen(file, "wb");
  require(fp && fwrite(data.data(), 1, data.size(), fp) == data.size() && fclose(fp) == 0,
          "write malformed fixture");
}

static Rows rows(const std::vector<INT16>& values, GPTYPE offset = 0)
{
  Rows result;
  for (size_t i = 0; i < values.size(); ++i) {
    const GPTYPE gp = offset + i * 100 + 1;
    const size_t span = i == 0 ? INTEGERFLD::MaxSpan() : i % 4;
    result.emplace_back(FC(gp, gp + span), values[i]);
  }
  return result;
}

static void build(const STRING& file, const Rows& data)
{
  file.Unlink();
  INTEGERLIST list;
  FILE *fp = list.OpenForAppend(file);
  require(fp != nullptr, "open raw integer column");
  // Exercise recovery of GP order as well as value sorting.
  for (auto i = data.rbegin(); i != data.rend(); ++i)
    require(i->Write(fp), "write full-width ingest rows");
  require(fclose(fp) == 0 && list.WriteIndex(file), "finalize adaptive column");
  const auto original = bytes(file);
  require(list.WriteIndex(file) && bytes(file) == original,
          "finalization of an untouched indexed field is an exact no-op");
}

static void encoding(const STRING& file, size_t count, bool compact, bool unsignedValues)
{
  const auto data = bytes(file);
  const size_t stride = compact ? INTEGERFLD::COMPACT_DISK_SIZE : INTEGERFLD::DISK_SIZE;
  require(data.size() == 16 + 2 * count * stride && data[0] == objINTEGERLIST &&
          data[1] == (compact ? 3 : 2) && data[2] == 1 && data[3] == unsignedValues &&
          getINT4(data.data(), 4) == stride && getINT8(data.data(), 8) == count,
          "canonical width, signedness, row size and exact file length");
}

static bool match(INT16 value, INT16 key, ZRelation_t relation)
{
  switch (relation) {
    case ZRelEQ: return value == key;
    case ZRelNE: return value != key;
    case ZRelLT: return value < key;
    case ZRelLE: return value <= key;
    case ZRelGT: return value > key;
    case ZRelGE: return value >= key;
    default: std::abort();
  }
}

static bool range_match(INT16 value, INT16 low, INT16 high, ZRelation_t relation)
{
  if (low > high) std::swap(low, high);
  switch (relation) {
    case ZRelEQ: case ZRelGE: return value >= low && value <= high;
    case ZRelGT: return value > low && value < high;
    case ZRelNE: case ZRelLT: return value < low || value > high;
    case ZRelLE: return value <= low || value >= high;
    default: std::abort();
  }
}

static void queries(const STRING& file, const Rows& data)
{
  INTEGERLIST list;
  MultiMMapSession maps;
  const ZRelation_t relations[] = {ZRelEQ, ZRelNE, ZRelLT, ZRelLE, ZRelGT, ZRelGE};
  const INT16 keys[] = {minimum, maximum, smin-1, smin, -1, 0, 42, smax,
                        smax+1, umax, umax+1, INT16(1) << 80};
  for (INT16 key : keys) {
    for (ZRelation_t relation : relations) {
      std::vector<Hit> expected, actual;
      for (const auto& row : data)
        if (match(row.GetValue(), key, relation))
          expected.emplace_back(row.GetGlobalStart(), row.GetSpan());
      require(list.VisitMappedMatches(file, key, relation, maps,
        [&](GPTYPE gp, UINT2 span) { actual.emplace_back(gp, span); }), "visit integer scalar relation");
      if (actual != expected) {
        std::cerr << "Key " << STRING(key) << ", relation " << INT(relation) << '\n';
        for (const auto& hit : expected) std::cerr << "expected " << hit.first << ':' << hit.second << '\n';
        for (const auto& hit : actual) std::cerr << "actual " << hit.first << ':' << hit.second << '\n';
      }
      require(actual == expected, "scalar boundary results retain values, GP order and spans");
    }
  }
  const std::pair<INT16, INT16> ranges[] = {
    {minimum, maximum}, {smin-1, -1}, {0, 42}, {smax, umax},
    {umax+1, maximum}, {42, 42}, {umax, smin}
  };
  for (const auto& range : ranges) {
    for (ZRelation_t relation : relations) {
      std::vector<Hit> expected, actual;
      for (const auto& row : data)
        if (range_match(row.GetValue(), range.first, range.second, relation))
          expected.emplace_back(row.GetGlobalStart(), row.GetSpan());
      require(list.VisitMappedRange(file, range.first, range.second, relation, maps,
        [&](GPTYPE gp, UINT2 span) { actual.emplace_back(gp, span); }), "visit integer range relation");
      require(actual == expected, "128-bit range endpoints remain exact in every encoding");
    }
  }
  require(list.LoadGpBlock(file) && list.GetCount() == data.size(), "load either encoding in GP order");
  for (size_t i = 0; i < data.size(); ++i)
    require(list[i].GetGlobalStart() == data[i].GetGlobalStart() &&
            list[i].GetValue() == data[i].GetValue() && list[i].GetSpan() == data[i].GetSpan(),
            "GP block materialization preserves values and geometry");
}

struct Database {
  STRING file;
  int error = 0;
  FIELDTYPE GetFieldType(const STRING&) { return FIELDTYPE::integer; }
  bool DfdtGetFileName(const STRING&, const FIELDTYPE& type, STRING *out) {
    require(type.IsInteger(), "logical integer type stays unchanged"); *out = file; return true;
  }
  void SetErrorCode(int value) { error = value; }
};

static void scans(const STRING& file, const Rows& data)
{
  std::vector<INT16> values;
  for (const auto& row : data) values.push_back(row.GetValue());
  std::sort(values.begin(), values.end());
  for (size_t start : {size_t(0), size_t(2), values.size()}) {
    for (INT limit : {-1, 1, 3}) {
      std::vector<std::pair<STRING, size_t>> expected;
      for (size_t i = start; i < values.size() && (limit < 0 || expected.size() < size_t(limit));) {
        const size_t first = i++;
        while (i < values.size() && values[i] == values[first]) ++i;
        expected.emplace_back(STRING(values[first]), i - first);
      }
      Database db{file};
      MultiMMapSession maps;
      SCANLIST counted;
      STRLIST strings;
      require(typed_scan::ScanField<INTEGER_INDEX_TRAITS>(&db, &counted, "VALUE", FIELDTYPE::integer,
                start, limit, maps, typed_scan::IntegerTerm()) == expected.size() &&
              typed_scan::ScanField<INTEGER_INDEX_TRAITS>(&db, &strings, "VALUE", FIELDTYPE::integer,
                start, limit, maps, typed_scan::IntegerTerm()) == expected.size() && db.error == 0,
              "scan either encoding through the shared traits");
      size_t i = 0;
      for (const STRLIST *p = strings.Next(); p != &strings; p = p->Next(), ++i) {
        const SCANOBJ entry = counted.GetEntry(i + 1);
        require(p->Value() == expected[i].first && entry.Term() == expected[i].first &&
                entry.Frequency() == expected[i].second, "exact ordered scan terms and counts");
      }
    }
  }
}

static void compact_rows()
{
  const STRING file("integer-storage.row");
  FILE *fp = fopen(file, "w+b");
  require(fp && INTEGERFLD(FC(7, 9), smin).WriteCompact(fp, false) &&
          INTEGERFLD(FC(10, 12), umax).WriteCompact(fp, true), "write signed and unsigned rows");
  const long position = ftell(fp);
  require(!INTEGERFLD(0, smin-1).WriteCompact(fp, false) &&
          !INTEGERFLD(0, smax+1).WriteCompact(fp, false) &&
          !INTEGERFLD(0, -1).WriteCompact(fp, true) &&
          !INTEGERFLD(0, umax+1).WriteCompact(fp, true) && ftell(fp) == position,
          "out-of-range compact writes cannot wrap or partially write");
  require(!INTEGERFLD(std::numeric_limits<GPTYPE>::max(), 1).Write(fp, 1) &&
          !INTEGERFLD(FC(std::numeric_limits<GPTYPE>::max()-2,
                         std::numeric_limits<GPTYPE>::max()), 1).Write(fp, 1) &&
          ftell(fp) == position, "reject GP overflow on import");
  rewind(fp);
  std::array<BYTE, INTEGERFLD::COMPACT_DISK_SIZE> raw;
  require(fread(raw.data(), 1, raw.size(), fp) == raw.size() &&
          raw[sizeof(GPTYPE)] == 0x80 && raw[sizeof(GPTYPE)+1] == 0 &&
          raw[sizeof(GPTYPE)+2] == 0 && raw[sizeof(GPTYPE)+3] == 0,
          "portable signed minimum bytes");
  require(fread(raw.data(), 1, raw.size(), fp) == raw.size() &&
          getINT4(raw.data(), sizeof(GPTYPE)) == std::numeric_limits<UINT4>::max(),
          "portable unsigned maximum bytes");
  rewind(fp);
  INTEGERFLD row;
  require(row.ReadCompact(fp, false) && row.GetValue() == smin && row.GetSpan() == 2 &&
          row.ReadCompact(fp, true) && row.GetValue() == umax && row.GetGlobalStart() == 10,
          "read compact boundaries without losing signedness");
  fclose(fp);
  file.Unlink();
}

static void widening_and_import()
{
  const STRING target("integer-storage-append.column"), source("integer-storage-import.column");
  Rows data = rows({smax, 0, 42});
  build(target, data);
  encoding(target, data.size(), true, false);
  INTEGERLIST list;
  auto append = [&](INT16 value, bool compact, bool unsignedValues) {
    MultiMMapSession maps;
    std::vector<Hit> before;
    require(list.VisitMappedMatches(target, 0, ZRelGE, maps,
      [&](GPTYPE gp, UINT2 span) { before.emplace_back(gp, span); }), "map before append");
    maps.Invalidate(target); // INDEX does this before rewriting a sidecar.
    FILE *fp = list.OpenForAppend(target);
    data.emplace_back(FC(data.size()*100+1, data.size()*100+3), value);
    require(fp && data.back().Write(fp) && fclose(fp) == 0 && list.WriteIndex(target),
            "append and finalize without narrowing the incoming value");
    encoding(target, data.size(), compact, unsignedValues);
    queries(target, data);
    scans(target, data);
  };
  append(umax, true, true); // Signed to unsigned, still four bytes.
  append(-1, false, false); // Negative plus upper unsigned half needs 128 bits.
  append(maximum, false, false);

  auto merge = [&](const Rows& imported, bool compact, bool unsignedValues) {
    build(source, imported);
    require(list.LoadGpBlock(source), "read source column using its own width");
    FILE *fp = INTEGERLIST().OpenForAppend(target);
    require(fp != nullptr, "expand destination column for import");
    for (const auto& row : list) {
      require(row.Write(fp, 10000), "apply import GP offset with exact value");
      INTEGERFLD moved(row);
      moved.SetGlobalStart(row.GetGlobalStart()+10000);
      data.push_back(moved);
    }
    std::sort(data.begin(), data.end(), [](const INTEGERFLD& a, const INTEGERFLD& b) {
      return a.GetGlobalStart() < b.GetGlobalStart();
    });
    require(fclose(fp) == 0 && INTEGERLIST().WriteIndex(target), "finalize mixed-width merge");
    encoding(target, data.size(), compact, unsignedValues);
    queries(target, data);
  };
  merge(rows({0, umax, 42}), false, false); // Compact source, wide destination.
  data = rows({-1, 42});
  build(target, data);
  merge(rows({minimum, maximum}), false, false); // Wide source, compact destination.
  data = rows({1, 2});
  build(target, data);
  merge(rows({3, 4}), true, false); // Both compact remain compact.
  target.Unlink(); source.Unlink();
}

static void malformed_columns()
{
  const STRING good("integer-storage-good.column"), bad("integer-storage-bad.column");
  build(good, rows({0, -1, smin}));
  const auto original = bytes(good);
  std::vector<std::vector<BYTE>> corrupt;
  auto mutate = [&](size_t index, BYTE value) { auto data = original; data[index] = value; corrupt.push_back(data); };
  mutate(0, 0); mutate(1, 4); mutate(2, 0); mutate(3, 2); mutate(7, INTEGERFLD::DISK_SIZE);
  mutate(15, 4); mutate(8, 0xff); mutate(15, 0);
  corrupt.push_back(std::vector<BYTE>(original.begin(), original.end()-1));
  corrupt.push_back(original); corrupt.back().push_back(0);
  corrupt.push_back(std::vector<BYTE>(original.begin(), original.begin()+15));
  for (const auto& data : corrupt) {
    write_bytes(bad, data);
    INTEGERLIST list;
    MultiMMapSession maps;
    require(!list.VisitMappedMatches(bad, 0, ZRelEQ, maps, [](GPTYPE, UINT2) {}) &&
            !list.LoadValueBlock(bad) && !list.LoadGpBlock(bad) && !list.OpenForAppend(bad) &&
            bytes(bad) == data, "reject malformed compact columns without altering them");
  }
  bad.Unlink();
  FILE *fp = INTEGERLIST().OpenForAppend(bad);
  require(fp && INTEGERFLD(1, 42).Write(fp) && fputc(0, fp) != EOF && fclose(fp) == 0,
          "prepare incomplete raw row");
  const auto partial = bytes(bad);
  std::vector<INTEGERFLD> loaded;
  require(!INTEGER_INDEX_TRAITS::LoadRawBlock(bad, &loaded) &&
          !INTEGERLIST().WriteIndex(bad) && !INTEGERLIST().OpenForAppend(bad) && bytes(bad) == partial,
          "raw rows are never silently dropped during append or finalization");
  const INTEGERFLD invalid(FC(1, 1+INTEGERFLD::MaxSpan()+1), 42);
  require(!INTEGER_INDEX_TRAITS::WriteIndex(good, {invalid}, {invalid}) && bytes(good) == original,
          "failed finalization keeps the previous column intact");
  good.Unlink(); bad.Unlink();
}

static void existing_wide_column()
{
  const STRING file("integer-storage-existing.column");
  const Rows data = rows({42, -1, smax});
  Rows sorted(data);
  std::sort(sorted.begin(), sorted.end(), [](const INTEGERFLD& a, const INTEGERFLD& b) {
    return a.GetValue() < b.GetValue();
  });
  FILE *fp = fopen(file, "wb");
  require(fp != nullptr, "open an existing version 2 fixture");
  putObjID(objINTEGERLIST, fp);
  require(::Write(UCHR(2), fp) == 1 && ::Write(UCHR(1), fp) == 1 &&
          ::Write(UCHR(0), fp) == 1 && ::Write(UINT4(INTEGERFLD::DISK_SIZE), fp) == 4 &&
          ::Write(UINT8(data.size()), fp) == 8, "write version 2 header");
  for (const auto& row : sorted) require(row.Write(fp), "write existing wide value block");
  for (const auto& row : data) require(row.Write(fp), "write existing wide GP block");
  require(fclose(fp) == 0, "close version 2 fixture");
  encoding(file, data.size(), false, false);
  queries(file, data);
  scans(file, data);
  const auto original = bytes(file);
  INTEGERLIST list;
  require(list.WriteIndex(file) && bytes(file) == original, "untouched wide columns stay valid");
  fp = list.OpenForAppend(file);
  require(fp && INTEGERFLD(1000, 7).Write(fp) && fclose(fp) == 0 && list.WriteIndex(file),
          "append to an existing wide column");
  encoding(file, data.size()+1, true, false);
  file.Unlink();
}

int main()
{
  compact_rows();
  const STRING file("integer-storage.column");
  const std::vector<std::vector<INT16>> fixtures = {
    {}, {smin, smax, -1, 0, 42, 42, 128, 255, 32768, 65535, 16777215},
    {0, umax, smax+1, 42, 42, INT16(0x80808080U)},
    {-1, umax, 0, 42}, {minimum, maximum, smin-1, umax+1, 0, 42, 42}
  };
  for (size_t i = 0; i < fixtures.size(); ++i) {
    const Rows data = rows(fixtures[i]);
    build(file, data);
    encoding(file, data.size(), i < 3, i == 2);
    queries(file, data);
    scans(file, data);
  }
  file.Unlink();
  widening_and_import();
  existing_wide_column();
  malformed_columns();
  std::cout << "Adaptive integer storage, full-domain queries, scans and mixed-width append/import passed\n";
}
