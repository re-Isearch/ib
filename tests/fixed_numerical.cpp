#include "nlist.hxx"
#include "intlist.hxx"
#include "typedscan.hxx"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <locale>
#include <vector>

static void require(bool ok, const char *message)
{
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

static NUMERICOBJ number(const char *text)
{
  const NUMERICOBJ value{STRING(text)};
  require(value.Ok(), text);
  return value;
}

static void parsing()
{
  const char *cases[][2] = {
    {"0.1", "0.1"}, {"  +00012.3400  ", "12.34"}, {"-.5", "-0.5"},
    {"-0", "0"}, {".0000000000000001", "0.0000000000000001"},
    {"6.053984e-5", "0.00006053984"}, {"1234567890123456e-16", "0.1234567890123456"},
    {"0.00012294500029437535", "0.0001229450002944"},
    {"-0.00012294500029437535", "-0.0001229450002944"},
    {"1.00000000000000005", "1"}, {"1.00000000000000015", "1.0000000000000002"},
    {"1.000000000000000051", "1.0000000000000001"},
    {"-0.00000000000000015", "-0.0000000000000002"}, {"5e-17", "0"},
    {"5.00001e-17", "0.0000000000000001"}, {"9e-999999999999999999", "0"},
    {"17014118346046923173168.7303715884105727", "17014118346046923173168.7303715884105727"},
    {"-17014118346046923173168.7303715884105728", "-17014118346046923173168.7303715884105728"},
    {"18446744073709551615", "18446744073709551615"}, {"0xffffffffffffffff", "18446744073709551615"},
    {"127.0.0.1", "2130706433"}
  };
  for (const auto& test : cases)
    {
      const NUMERICOBJ value = number(test[0]);
      require(value.ToString() == test[1], test[0]);
      require(number(value.ToString().c_str()) == value, "exact decimal round trip");
    }
  const char *invalid[] = {"", "+", ".", "1e", "1e-", "NaN", "Infinity", "1e999999999999999999",
    "1.2x", "17014118346046923173168.7303715884105728",
    "-17014118346046923173168.7303715884105729"};
  for (const char *text : invalid)
    require(!NUMERICOBJ(STRING(text)).Ok(), text);
  require(!NUMERICOBJ(std::numeric_limits<long double>::infinity()).Ok(), "reject infinite callback");
  require(!NUMERICOBJ(std::numeric_limits<long double>::quiet_NaN()).Ok(), "reject NaN callback");
  require(number("-2").IsIntegral() && !number("-0.1").IsIntegral(), "integrality is exact");

  struct Comma : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
  const std::locale previous = std::locale::global(std::locale(std::locale::classic(), new Comma));
  require(NUMERICOBJ(1.25L) == number("1.25"), "floating boundary ignores ambient locale");
  std::locale::global(previous);
}

static void arithmetic()
{
  const INT16 maximum = INT16((UINT16(1) << 127)-1), minimum = -maximum-1;
  const NUMERICOBJ max = NUMERICOBJ::FromTicks(maximum), min = NUMERICOBJ::FromTicks(minimum);
  const NUMERICOBJ tick = NUMERICOBJ::FromTicks(1);
  require(number("0.1") + number("0.2") == number("0.3"), "exact decimal addition");
  require(tick + UINT8(18446744073709551615ULL) == number("18446744073709551615.0000000000000001"),
          "mixed integer arithmetic preserves all integer bits");
  require(number("100000000000000") * number("10000000") == number("1000000000000000000000"),
          "256-bit multiplication intermediate");
  require(number("1000000000000000000000") / number("10000000") == number("100000000000000"),
          "256-bit division intermediate");
  require((tick * number("0.5")) == number("0"), "multiplication rounds ties to even zero");
  require(NUMERICOBJ::FromTicks(3) * number("0.5") == NUMERICOBJ::FromTicks(2), "multiply ties to even");
  require(NUMERICOBJ::FromTicks(-3) / number("2") == NUMERICOBJ::FromTicks(-2), "divide ties to even");
  require(max * number("1") == max && min / number("1") == min, "full-width boundary arithmetic");
  require(!(max + tick).Ok() && !(min - tick).Ok(), "addition/subtraction reject overflow");
  require(!(max * number("2")).Ok() && !(min / number("-1")).Ok(), "multiply/divide reject overflow");
  require(!(tick / number("0")).Ok() && !(NUMERICOBJ() + tick).Ok(), "invalid arithmetic propagates");
  require(NUMERICOBJ::FromScaled(1, 50000) == number("0.00002"), "exact monetary rescaling");
  require(NUMERICOBJ(MONETARYOBJ(STRING("12.34568"))) == number("12.34568"), "currency to numerical is exact");
  require(MONETARYOBJ(number("12.34568")).GetTicks() == 617284, "numerical to currency is exact");
  MONETARYOBJ large_money;
  large_money.SetTicks(INT16(UINT16(1)<<89)+1);
  const NUMERICOBJ converted(large_money);
  require(converted.Ok() && converted.GetTicks() == large_money.GetTicks()*INT16(200000000000ULL),
          "128-bit monetary conversion never passes through floating point");
  require(MONETARYOBJ(converted).GetTicks() == large_money.GetTicks(), "full-width monetary round trip");

  const STRING object_file("fixed-numerical-object.bin");
  FILE *fp = fopen(object_file, "w+b");
  require(fp != nullptr, "open object round-trip fixture");
  const NUMERICOBJ values[] = {min, tick, max, NUMERICOBJ()};
  for (const NUMERICOBJ& value : values) value.Write(fp);
  rewind(fp);
  for (const NUMERICOBJ& value : values) {
    NUMERICOBJ restored;
    require(restored.Read(fp) && restored == value, "portable numeric object persistence");
  }
  NUMERICOBJ extra;
  require(!extra.Read(fp) && !extra.Ok(), "truncated object invalidates destination");
  fclose(fp);
  object_file.Unlink();
}

struct Database {
  STRING file;
  int error = 0;
  FIELDTYPE GetFieldType(const STRING&) { return FIELDTYPE::numerical; }
  bool DfdtGetFileName(const STRING&, const FIELDTYPE&, STRING *out) { *out = file; return true; }
  void SetErrorCode(int value) { error = value; }
};

static void columns()
{
  const STRING file("fixed-numerical.column");
  file.Unlink();
  NUMERICLIST list;
  list.SetFileName(file);
  FILE *fp = list.OpenForAppend(file);
  require(fp != nullptr, "create fixed numerical column");
  const char *values[] = {"17014118346046923173168.7303715884105727", "9007199254740992.0000000000000002",
    "9007199254740992.0000000000000001", "0.1", "0.1", "0", "-0.0000000000000001",
    "-17014118346046923173168.7303715884105728"};
  for (size_t i = 0; i < 8; ++i)
    require(NUMERICFLD(GPTYPE(i*100+1), number(values[i])).Write(fp), "write exact numerical record");
  require(fclose(fp) == 0 && list.WriteIndex(file), "index unsorted fixed numerical values");
  MultiMMapSession maps;
  const NUMERICOBJ key = number("9007199254740992.0000000000000001");
  const ZRelation_t relations[] = {ZRelEQ, ZRelLT, ZRelLE, ZRelGT, ZRelGE, ZRelNE};
  const size_t counts[] = {1, 5, 6, 2, 3, 7};
  for (size_t i = 0; i < 6; ++i) {
    std::vector<GPTYPE> hits;
    require(list.VisitMappedMatches(file, key.GetTicks(), relations[i], maps,
      [&](GPTYPE gp, UINT2) { hits.push_back(gp); }), "search fixed numerical column");
    require(hits.size() == counts[i], "exact relation match count");
    if (relations[i] == ZRelEQ) require(hits[0] == 201, "16th decimal digit selects the correct record");
  }
  Database db{file};
  SCANLIST scan;
  require(typed_scan::ScanField<NUMERIC_INDEX_TRAITS>(&db, &scan, "VALUE", FIELDTYPE::numerical,
    0, -1, maps, typed_scan::NumericTerm()) == 7, "scan distinct fixed numerical terms");
  const char *terms[] = {values[7], values[6], values[5], values[3], values[2], values[1], values[0]};
  for (size_t i = 0; i < 7; ++i) {
    const SCANOBJ term = scan.GetEntry(i+1);
    require(term.Term() == terms[i] && term.Frequency() == (i == 3 ? 2 : 1), "exact scan terms/counts");
  }
  STRLIST strings;
  require(typed_scan::ScanField<NUMERIC_INDEX_TRAITS>(&db, &strings, "VALUE", FIELDTYPE::numerical,
    5, 2, maps, typed_scan::NumericTerm()) == 2, "fixed numerical scan paging");
  require(strings.Next()->Value() == values[2] && strings.Prev()->Value() == values[1], "exact paged terms");

  maps.Invalidate(file);
  fp = list.OpenForAppend(file);
  require(fp && NUMERICFLD(801, number("0.2")).Write(fp) && fclose(fp) == 0 && list.WriteIndex(file),
          "append to a previously indexed column");
  require(list.LoadTable(GP_BLOCK) == 9 && list.GetGlobalStart(8) == 801, "import loads GP order");
  require(list.GetNumericValue(2) == key, "GP block preserves all decimal digits");
  INT4 index = -1;
  require(list.Find(file, key, ZRelEQ, &index) == MATCH, "exact field-data lookup API");

  maps.Clear();
  fp = fopen(file, "r+b");
  require(fp && fseek(fp, 3, SEEK_SET) == 0 && fputc(15, fp) != EOF && fclose(fp) == 0, "alter scale header");
  NUMERIC_INDEX_TRAITS::mapped_type view;
  require(!NUMERIC_INDEX_TRAITS::MapIndexed(file, maps, &view), "reject incompatible decimal scale");
  require(list.OpenForAppend(file) == nullptr, "reject appending to incompatible scale");
  file.Unlink();
  fp = fopen(file, "wb");
  require(fp && fputc(objNLIST, fp) != EOF && fclose(fp) == 0, "create old floating-column marker");
  require(!NUMERIC_INDEX_TRAITS::MapIndexed(file, maps, &view), "reject obsolete floating columns");
  require(list.OpenForAppend(file) == nullptr, "old numerical columns require reindexing");
  maps.Clear();
  file.Unlink();
}

static void ranges()
{
  NUMERICALRANGE range(STRING("[-0.0000000000000001,0.0000000000000001]"));
  require(range.Ok() && range.GetStart() == number("-0.0000000000000001") &&
          range.GetEnd() == number("0.0000000000000001"), "exact range endpoints");
  require(range.SetRange(STRING("-1.25e-5--0.5e-6")) && range.GetStart() == number("-0.0000125") &&
          range.GetEnd() == number("-0.0000005"), "negative scientific range endpoints");
  require(!range.SetRange(STRING("1e-3")), "scalar scientific notation is not a range");
  require(!range.SetRange(STRING("1,nan")) && !range.Defined(), "invalid range clears endpoints");
  require(range.SetRange(STRING("1..2")), "decimal range separator");
  range.Clear();
  require(!range.Defined(), "cleared numerical range is undefined");
  INTERVALFLD field;
  field.SetGlobalStart(42);
  field.SetStartValue(number("9007199254740992.0000000000000001"));
  field.SetEndValue(number("9007199254740992.0000000000000002"));
  const STRING interval_file("fixed-numerical-interval.bin");
  FILE *fp = fopen(interval_file, "w+b");
  require(fp != nullptr, "open interval round-trip fixture");
  field.Write(fp);
  require(ftell(fp) == long(INTERVALFLD::DISK_SIZE), "interval disk stride excludes object padding");
  rewind(fp);
  INTERVALFLD restored;
  restored.Read(fp);
  require(restored.GetGlobalStart() == 42 && restored.GetStartValue() == field.GetStartValue() &&
          restored.GetEndValue() == field.GetEndValue(), "exact interval persistence");
  fclose(fp);
  interval_file.Unlink();

  const STRING file("fixed-numerical-interval.column");
  file.Unlink();
  INTERVALLIST list;
  fp = list.OpenForAppend(file);
  require(fp != nullptr, "create fixed interval column");
  field.Write(fp);
  INTERVALFLD lower;
  lower.SetGlobalStart(84);
  lower.SetStartValue(number("-17014118346046923173168.7303715884105728"));
  lower.SetEndValue(number("-0.0000000000000001"));
  lower.Write(fp);
  require(fclose(fp) == 0, "write interval fixtures");
  list.WriteIndex(file);
  list.Clear(); list.SetFileName(file);
  require(list.LoadTable(0, -1, START_BLOCK) == 2 && list.GetStartValue(0) == lower.GetStartValue(),
          "interval sorting compares endpoints without overflowing their difference");
  list.Clear(); list.SetFileName(file);
  require(list.LoadTable(0, -1, PTR_BLOCK) == 2 && list.GetGlobalStart(0) == 42,
          "interval loader uses portable disk strides");
  INT4 index = -1;
  require(list.Find(file, field.GetStartValue(), ZRelGE, START_BLOCK, &index) == MATCH && index == 1,
          "interval search preserves the 16th decimal digit");
  list.SetTruncatedSearch(true);
  require(list.Find(file, field.GetStartValue(), ZRelGE, START_BLOCK, &index) == MATCH && index == 1,
          "truncated interval search retains the full integer portion");
  list.SetTruncatedSearch(false);
  fp = list.OpenForAppend(file);
  require(fp != nullptr, "append to indexed fixed intervals");
  lower.SetGlobalStart(126); lower.Write(fp);
  require(fclose(fp) == 0, "write appended interval");
  list.WriteIndex(file);
  list.Clear(); list.SetFileName(file);
  require(list.LoadTable(0, -1, PTR_BLOCK) == 3 && list.GetGlobalStart(2) == 126,
          "interval reindex retains all records");
  file.Unlink();
  fp = fopen(file, "wb");
  require(fp && fputc(objINTLIST, fp) != EOF && fclose(fp) == 0, "create obsolete interval marker");
  require(!list.OpenForAppend(file), "obsolete floating interval files require reindexing");
  file.Unlink();
}

int main()
{
  parsing(); arithmetic(); columns(); ranges();
  std::cout << "Fixed numerical parsing, arithmetic, persistence, search, scans and ranges passed\n";
}
