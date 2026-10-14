#include "floatlist.hxx"
#include "nlist.hxx"
#include "typedscan.hxx"
#include "fieldtype_promotion.hxx"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <locale>
#include <vector>

static void require(bool ok, const char *message)
{
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

static DOUBLE value(const char *text)
{
  DOUBLE result;
  require(float64::Parse(STRING(text), &result), text);
  return result;
}

static void parsing()
{
  require(value("  +1.25e-100  ") == 1.25e-100, "decimal/scientific input");
  require(value("1e-300") == 1e-300 && value("1e300") == 1e300, "wide exponent range");
  require(value("1.7976931348623157e308") == std::numeric_limits<DOUBLE>::max(), "maximum finite value");
  require(value("5e-324") == std::numeric_limits<DOUBLE>::denorm_min(), "smallest subnormal");
  require(std::signbit(value("-0")), "parse IEEE negative zero");
  require(value("1.0000000000000002") == std::nextafter(1.0, 2.0), "adjacent binary64 values remain distinct");
  for (const char *text : {"", "+", "+-1", ".", "1e", "1,25", "1x", "1e309", "1e-999",
                           "NaN", "nan(1)", "inf", "-Infinity", "0x1p4"}) {
    DOUBLE output = 42;
    require(!float64::Parse(STRING(text), &output) && output == 0, text);
  }
  for (DOUBLE number : {0.1, -0.1, 1e-300, 1e300, std::nextafter(1.0, 2.0),
                       std::numeric_limits<DOUBLE>::max(), std::numeric_limits<DOUBLE>::denorm_min()})
    require(value(float64::ToString(number).c_str()) == number, "shortest decimal term round-trips");
  require(float64::ToString(0.1) == "0.1", "scan output avoids unnecessary decimal digits");
  require(float64::ToString(-0.0) == "0", "signed zeros have one scan term");
  require(float64::ToString(std::numeric_limits<DOUBLE>::infinity()).IsEmpty(), "nonfinite values have no term");

  struct Comma : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
  const std::locale previous = std::locale::global(std::locale(std::locale::classic(), new Comma));
  require(value("1.25") == 1.25 && float64::ToString(1.25) == "1.25", "locale-independent parser and formatter");
  std::locale::global(previous);
}

static void metadata()
{
  require(FIELDTYPE::numerical == 3 && FIELDTYPE::currency == 14 &&
          FIELDTYPE::float64 == FIELDTYPE::special+1, "preserve existing field-type IDs");
  for (const char *name : {"float64", "double", "binary64", "floating", "xs:double", "xs:float"})
    require(FIELDTYPE(name).IsFloat64(), "floating aliases identify binary64");
  const FIELDTYPE type((BYTE)FIELDTYPE::float64);
  require(type.IsNumeric() && !type.IsNumerical() && !type.IsExtern() && !type.IsExternal() &&
          STRING(type.c_str()) == "float64" && STRING(type.datatype()) == "f64", "native floating metadata");
  require(FIELDTYPE((INT)FIELDTYPE::float64).IsFloat64(), "integer field-type constructor accepts new ID");
  STRLIST types;
  type.AvailableTypesList(&types);
  bool found = false;
  for (const STRLIST *p = types.Next(); p != &types; p = p->Next()) found |= p->Value() == "float64";
  require(found, "available datatype list includes float64");
  ATTRLIST attrs;
  attrs.AttrSetFieldType(FIELDTYPE::numerical);
  attrs.AttrSetFieldType(type);
  require(!attrs.AttrGetFieldNumerical() && attrs.AttrGetFieldType().IsFloat64(),
          "floating type is not shadowed by a stale fixed numerical flag");
  struct Untouched {
    bool IsIntegralNumber() const { std::abort(); }
    bool IsNumber() const { std::abort(); }
    bool IsCurrency() const { std::abort(); }
  } input;
  require(field_type_inference::GuessNumericPromotion(type, input, []() -> bool { std::abort(); }).IsFloat64(),
          "explicit float64 fields require no promotion predicates or freeze lookups");
}

static void rows()
{
  const STRING filename("float64-row.bin");
  FILE *fp = fopen(filename, "w+b");
  require(fp && FLOATFLD(42, 1.0).Write(fp) && FLOATFLD(43, -0.0).Write(fp), "write native IEEE rows");
  require(ftell(fp) == long(2*FLOATFLD::DISK_SIZE), "disk rows exclude object padding");
  rewind(fp);
  std::vector<BYTE> bytes(FLOATFLD::DISK_SIZE);
  require(fread(bytes.data(), 1, bytes.size(), fp) == bytes.size(), "read portable row bytes");
  const BYTE one[] = {0x3f, 0xf0, 0, 0, 0, 0, 0, 0};
  require(bytes[0] == objFLOAT64FLD &&
          std::memcmp(bytes.data()+1+sizeof(GPTYPE), one, sizeof(one)) == 0, "network-order IEEE 1.0 encoding");
  rewind(fp);
  FLOATFLD field;
  require(field.Read(fp) && field.GetGlobalStart() == 42 && field.GetValue() == 1.0, "read binary64 row");
  require(field.Read(fp) && std::signbit(field.GetValue()), "persist signed zero bits");
  require(!field.Read(fp) && !std::isfinite(field.GetValue()), "truncated rows invalidate destination");
  require(!FLOATFLD(0, std::numeric_limits<DOUBLE>::quiet_NaN()).Write(fp) &&
          !FLOATFLD(0, std::numeric_limits<DOUBLE>::infinity()).Write(fp), "reject unordered/nonfinite stored values");
  fclose(fp); filename.Unlink();
}

struct Database {
  STRING file;
  int error = 0;
  FIELDTYPE GetFieldType(const STRING&) { return FIELDTYPE::float64; }
  bool DfdtGetFileName(const STRING&, const FIELDTYPE& requested, STRING *out) {
    require(requested.IsFloat64(), "resolve native floating sidecar"); *out = file; return true;
  }
  void SetErrorCode(int value) { error = value; }
};

static void columns()
{
  const STRING filename("float64.column"), imported("float64-import.column");
  filename.Unlink(); imported.Unlink();
  FLOATLIST list;
  FILE *fp = list.OpenForAppend(filename);
  require(fp != nullptr, "open floating append stream");
  const DOUBLE max = std::numeric_limits<DOUBLE>::max(), tiny = std::numeric_limits<DOUBLE>::denorm_min();
  const DOUBLE numbers[] = {max, 1e300, std::nextafter(1.0, 2.0), 1.0, 0.1, tiny, 0.0, -0.0,
                            -tiny, -0.1, -1e300, -max, 0.1, 1e-300};
  for (size_t i = 0; i < 14; ++i)
    require(FLOATFLD(GPTYPE(i*100+1), numbers[i]).Write(fp), "write floating fixture");
  require(fclose(fp) == 0 && list.WriteIndex(filename), "sort and index unsorted floating rows");
  MultiMMapSession maps;
  const ZRelation_t relations[] = {ZRelEQ, ZRelLT, ZRelLE, ZRelGT, ZRelGE, ZRelNE};
  const std::vector<GPTYPE> expected[] = {
    {301}, {401,501,601,701,801,901,1001,1101,1201,1301},
    {301,401,501,601,701,801,901,1001,1101,1201,1301}, {1,101,201}, {1,101,201,301},
    {1,101,201,401,501,601,701,801,901,1001,1101,1201,1301}
  };
  for (size_t i = 0; i < 6; ++i) {
    std::vector<GPTYPE> hits;
    require(list.VisitMappedMatches(filename, 1.0, relations[i], maps,
      [&](GPTYPE gp, UINT2) { hits.push_back(gp); }), "visit native floating matches");
    require(hits == expected[i], "exact relation results in GP order");
  }
  std::vector<GPTYPE> zeros;
  require(list.VisitMappedMatches(filename, -0.0, ZRelEQ, maps,
    [&](GPTYPE gp, UINT2) { zeros.push_back(gp); }) && zeros == std::vector<GPTYPE>({601,701}),
          "positive and negative zero compare numerically equal");
  std::vector<GPTYPE> tiny_hits;
  require(list.VisitMappedMatches(filename, value("1e-300"), ZRelEQ, maps,
    [&](GPTYPE gp, UINT2) { tiny_hits.push_back(gp); }) && tiny_hits == std::vector<GPTYPE>({1301}),
          "tiny query value never passes through fixed point");

  const char *terms[] = {"-1.7976931348623157e+308", "-1e+300", "-0.1", "-5e-324", "0", "5e-324",
                         "1e-300", "0.1", "1", "1.0000000000000002", "1e+300", "1.7976931348623157e+308"};
  Database db{filename};
  SCANLIST scan;
  require(typed_scan::ScanField<FLOAT_INDEX_TRAITS>(&db, &scan, "VALUE", FIELDTYPE::float64,
    0, -1, maps, typed_scan::Float64Term()) == 12 && db.error == 0, "scan native floating column");
  for (size_t i = 0; i < 12; ++i) {
    const SCANOBJ entry = scan.GetEntry(i+1);
    require(entry.Term() == terms[i] && entry.Frequency() == (i == 4 || i == 7 ? 2 : 1),
            "round-trip floating scan terms, numeric order and duplicate counts");
  }
  STRLIST strings;
  require(typed_scan::ScanField<FLOAT_INDEX_TRAITS>(&db, &strings, "VALUE", FIELDTYPE::float64,
    7, 2, maps, typed_scan::Float64Term()) == 2 && strings.Next()->Value() == "1e-300" &&
    strings.Prev()->Value() == "0.1", "floating scan paging uses row offsets");
  DOUBLE recovered;
  require(list.ReadValue(filename, FC(1301, 1399), &recovered) && recovered == 1e-300,
          "field retrieval preserves tiny floating values");
  require(!list.ReadValue(filename, FC(2, 100), &recovered), "field lookup does not read the next record");

  require(list.LoadGpBlock(filename) && list.GetCount() == 14, "load GP block for import");
  fp = FLOATLIST().OpenForAppend(imported);
  require(fp != nullptr, "create floating import destination");
  for (const FLOATFLD& field : list) require(field.Write(fp, 10000), "import exact floating bits with GP offset");
  require(fclose(fp) == 0 && FLOATLIST().WriteIndex(imported), "index imported floating column");
  require(list.ReadValue(imported, FC(11301, 11399), &recovered) && recovered == 1e-300, "import preserves values");
  maps.Clear();
  fp = list.OpenForAppend(filename);
  require(fp && FLOATFLD(1401, 0.25).Write(fp) && fclose(fp) == 0 && list.WriteIndex(filename),
          "append to indexed floating column");
  require(list.LoadGpBlock(filename) && list.GetCount() == 15 && list.GetGlobalStart(14) == 1401,
          "append retains GP order and every previous row");

  NUMERIC_INDEX_TRAITS::mapped_type wrong;
  require(!NUMERIC_INDEX_TRAITS::MapIndexed(filename, maps, &wrong), "fixed numerical reader rejects floating columns");
  maps.Clear();
  filename.Unlink(); imported.Unlink();
}

static void malformed()
{
  const STRING filename("float64-invalid.column");
  filename.Unlink();
  FLOATLIST list;
  FILE *fp = list.OpenForAppend(filename);
  require(fp && fputc(1, fp) != EOF && fclose(fp) == 0, "create truncated raw row");
  std::vector<FLOATFLD> rows;
  require(!FLOAT_INDEX_TRAITS::LoadRawBlock(filename, &rows) && !list.OpenForAppend(filename) &&
          !list.WriteIndex(filename), "reject incomplete raw rows without rewriting them");
  filename.Unlink();
  fp = list.OpenForAppend(filename);
  require(fp && fclose(fp) == 0 && list.WriteIndex(filename), "empty indexed floating column");
  MultiMMapSession maps;
  FLOAT_INDEX_TRAITS::mapped_type view;
  require(FLOAT_INDEX_TRAITS::MapIndexed(filename, maps, &view) && view.count == 0, "map empty floating column");
  maps.Clear();
  fp = fopen(filename, "r+b");
  require(fp && fseek(fp, 8, SEEK_SET) == 0, "modify record count");
  for (int i = 0; i < 8; ++i) require(fputc(255, fp) != EOF, "write oversized record count");
  require(fclose(fp) == 0 && !FLOAT_INDEX_TRAITS::MapIndexed(filename, maps, &view) &&
          !list.OpenForAppend(filename), "reject overflowing header counts");
  maps.Clear(); filename.Unlink();
  fp = fopen(filename, "wb");
  require(fp && fputc(objNLIST, fp) != EOF && fclose(fp) == 0 && !list.OpenForAppend(filename),
          "legacy floating layouts are not native binary64 columns");
  filename.Unlink();
}

int main()
{
  parsing(); metadata(); rows(); columns(); malformed();
  std::cout << "Binary64 parsing, metadata, IEEE persistence, search, scans, append and import passed\n";
}
