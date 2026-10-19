#include "common.hxx"
#include "index.hxx"
#include "idbobj.hxx"
#include "lang-codes.hxx"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool ok, const char *message)
{
  if (!ok) throw std::runtime_error(message);
}

static const UCHR *bytes(const std::string& text)
{
  return reinterpret_cast<const UCHR *>(text.data());
}

static GPTYPE endpoint(FILE *fp)
{
  UCHR buffer[sizeof(GPTYPE)];
  require(fread(buffer, 1, sizeof(buffer), fp) == sizeof(buffer), "read SIS posting endpoint");
  return getGPTYPE(buffer, 0);
}

static void comparisons()
{
  require(SetGlobalCharset("UTF-8"), "set global UTF-8 directly");
  CHARSET utf8("UTF-8");
  std::vector<std::string> terms = {u8"𐐨", u8"中文", u8"я", u8"λ", u8"ö", u8"é", "z", "cafe", u8"café", "c"};
  std::sort(terms.begin(), terms.end());
  for (size_t i = 0; i < terms.size(); ++i)
    for (size_t j = 0; j < terms.size(); ++j) {
      const int expected = (i > j) - (i < j);
      const int actual = utf8.CompareSIS(bytes(terms[i]), terms[i].size(), bytes(terms[j]), terms[j].size());
      require((actual > 0) - (actual < 0) == expected, "SIS ordering agrees with unsigned token sorting");
      const int bounded = _ib_SisCompare(bytes(terms[i]), bytes(terms[j]), StringCompLength);
      require((bounded > 0) - (bounded < 0) == expected, "merge writer uses the same UTF-8 ordering");
      const int callback = utf8.SisCompare()(terms[i].c_str(), terms[j].c_str());
      require((callback > 0) - (callback < 0) == expected, "UTF-8 comparison callback is selected");
      const int counted = utf8.Compare()(terms[i].c_str(), terms[j].c_str(), StringCompLength);
      require((counted > 0) - (counted < 0) == expected, "bounded UTF-8 callback is selected");
    }
  // Exact-size buffers, with no NUL or readable continuation beyond the limit.
  for (size_t n = 0; n <= 4; ++n) {
    std::vector<UCHR> left(n, 0xf0), right(n, 0xf0);
    if (n) { left[0] = right[0] = 0xf0; left[n-1] = 0x90; right[n-1] = 0x91; }
    const int expected = n ? -1 : 0;
    require(_ib_SisCompare(left.data(), right.data(), n) == expected, "byte limit never decodes outside a SIS prefix");
    require(utf8.CompareSIS(left.data(), n, right.data(), n) == expected,
            "explicit SIS lengths never inspect adjacent metadata");
  }
  require(_ib_SisCompare(bytes("a next"), bytes("a"), 6) == 0, "cleaned whitespace terminates a merge key");
  CHARSET latin1("ISO-8859-1");
  const UCHR upper[1] = {'A'}, lower[1] = {'a'};
  require(latin1.CompareSIS(upper, 1, lower, 1) == 0, "bounded single-byte charset comparison retained");
  require(SetGlobalCharset("ISO-8859-1"), "restore single-byte global charset");
  require(_ib_SisCompare(bytes("A"), bytes("a"), 1) == 0, "global charset switches out of UTF-8 mode");
}

static void keys_and_lengths()
{
  SetGlobalCharset("UTF-8");
  CHARSET utf8("UTF-8");
  auto compare = utf8.SisKeys();
  const auto key = [](const std::string& value, size_t match) {
    std::vector<UCHR> result(value.size()+2);
    result[0] = UCHR(match); result[1] = UCHR(value.size());
    std::memcpy(result.data()+2, value.data(), value.size());
    return result;
  };
  const auto term = [](const std::string& value) {
    std::vector<UCHR> result(value.size()+1);
    result[0] = UCHR(value.size());
    std::memcpy(result.data()+1, value.data(), value.size());
    return result;
  };
  auto query = key(u8"é", 2), stored = term(u8"é");
  require(compare(query.data(), stored.data()) == 0, "exact UTF-8 key requires no trailing sentinel");
  stored = term(u8"école");
  require(compare(query.data(), stored.data()) < 0, "exact query rejects a longer UTF-8 word");
  query[0] = 0;
  require(compare(query.data(), stored.data()) == 0, "truncated UTF-8 search matches a prefix");
  query = key(std::string("\xc3", 1), 1);
  require(compare(query.data(), stored.data()) < 0, "mid-sequence exact query is rejected");
  query = key(u8"école", 6); stored = term(u8"é");
  require(compare(query.data(), stored.data()) > 0, "short stored word bounds a longer query");
  for (const std::string text : {u8"café", u8"中文", u8"𐐨", u8"café.rue", u8"foo.école"})
    require(_ib_UTF8TermLength(bytes(text), text.size()) == text.size(), "term length advances over full UTF-8 characters");
  const std::string followed = u8"café. next";
  require(_ib_UTF8TermLength(bytes(followed), followed.size()) == std::string(u8"café").size(),
          "trailing punctuation ends a merge term");
  STRING folded;
  const std::string raw = u8"CAFÉ ЖИЗНЬ";
  folded.SetTermLower(bytes(raw), raw.size());
  require(folded == u8"café жизнь", "merge term preparation preserves continuation bytes and folds complete characters");
}

struct Scratch {
  std::filesystem::path previous = std::filesystem::current_path();
  std::filesystem::path path = previous / "utf8-sis-fixtures";
  Scratch() {
    require(std::filesystem::create_directory(path), "create owned SIS fixtures");
    std::filesystem::current_path(path);
  }
  ~Scratch() { std::filesystem::current_path(previous); std::filesystem::remove_all(path); }
};

struct Database : IDBOBJ {
  STRING stem;
  mutable MDT mdt;
  explicit Database(const char *name) : stem(name), mdt(stem, false) {
    MDTREC record;
    record.SetKey("fixture");
    require(record.SetLocalRecordRange(0, 0), "set fixture record range");
    require(mdt.AddEntry(record) == 1, "seed MDT partition metadata");
  }
  PMDT GetMainMdt() const override { return &mdt; }
  STRING ComposeDbFn(const CHR *extension) const override { return stem+extension; }
};

using Rows = std::map<std::string, std::vector<GPTYPE>>;

static void partition(INDEX& index, Database& db, int number, size_t width, const Rows& rows)
{
  const STRING suffix = STRING().form(".%d", number);
  FILE *sis = fopen(db.ComposeDbFn(DbExtDict)+suffix, "wb");
  FILE *inx = fopen(db.stem+".inx"+suffix, "wb");
  require(sis && inx, "create partition files");
  putINT2(0, inx);
  putc(int(width), sis); putc(Charset2Id("UTF-8"), sis);
  GPTYPE total = 0;
  for (const auto& row : rows) {
    require(row.first.size() <= width, "fixture keys fit their SIS window");
    putc(int(row.first.size()), sis);
    fwrite(row.first.data(), 1, row.first.size(), sis);
    for (size_t i = row.first.size(); i < width; ++i) putc(0xa5, sis); // deliberately non-NUL padding
    for (GPTYPE gp : row.second) { require(index.GpFwrite(gp, inx) == 1, "write fixture posting"); ++total; }
    require(::Write(total-1, sis) == int(sizeof(GPTYPE)), "write SIS posting endpoint");
  }
  require(fclose(sis) == 0 && fclose(inx) == 0, "close partition files");
}

static void collapse(size_t width, const char *stem)
{
  Database db(stem);
  INDEX index(&db, db.stem+".inx");
  const std::vector<std::vector<std::string>> chunks = {
    {"c", "z", u8"abé", u8"café", u8"école", u8"中文", u8"𐐨"},
    {"ca", "z", u8"abê", u8"ab中", u8"café", u8"ö", u8"λ", u8"𐑀"},
    {"aa", u8"ab𐐨", u8"caféteria", u8"école", u8"я", u8"中文", u8"𐐨"}
  };
  Rows expected;
  GPTYPE gp = 100;
  for (size_t i = 0; i < chunks.size(); ++i) {
    Rows rows;
    for (const auto& word : chunks[i]) {
      const std::string prefix = word.substr(0, width);
      rows[prefix].push_back(++gp);
      expected[prefix].push_back(gp);
    }
    partition(index, db, int(i+1), width, rows);
  }
  require(db.mdt.SetIndexNum(3), "set partition count");
  while (db.mdt.GetIndexNum() > 1)
    require(index.CollapseIndexFiles(), "collapse real UTF-8 SIS and posting files");
  require(db.mdt.GetIndexNum() == 0, "final collapsed index is installed");
  FILE *sis = fopen(db.ComposeDbFn(DbExtDict), "rb");
  FILE *inx = fopen(db.stem+".inx", "rb");
  require(sis && inx && getc(sis) == int(width) && getc(sis) == Charset2Id("UTF-8"), "collapsed SIS header");
  require(fseek(inx, 2, SEEK_SET) == 0, "skip index magic");
  GPTYPE total = 0;
  for (const auto& row : expected) {
    require(getc(sis) == int(row.first.size()), "collapsed term byte length");
    std::vector<UCHR> text(width);
    require(fread(text.data(), 1, width, sis) == width &&
            std::memcmp(text.data(), row.first.data(), row.first.size()) == 0, "collapsed dictionary keeps unsigned UTF-8 order");
    for (GPTYPE gp : row.second) {
      require(index.GpFread(inx) == gp, "collapse preserves every posting and combines equal keys");
      ++total;
    }
    require(endpoint(sis) == total-1, "collapsed posting endpoint");
  }
  require(getc(sis) == EOF && getc(inx) == EOF, "collapse emits exactly the expected keys and postings");
  fclose(sis); fclose(inx);
}

static void writer()
{
  SetGlobalCharset("UTF-8");
  Database db("writer");
  INDEX index(&db, "writer.inx");
  FILE *fp = fopen("writer.keys", "w+b");
  require(fp != nullptr, "open SIS writer fixture");
  const std::vector<std::string> words = {"z", u8"café", u8"école", u8"中文", u8"𐐨"};
  std::vector<std::string> ordered(words);
  std::sort(ordered.begin(), ordered.end());
  for (const auto& word : ordered) index.SisWrite(STRING(word.c_str()), fp);
  index.SisWrite(STRING(), fp);
  rewind(fp);
  for (const auto& word : ordered) {
    require(getc(fp) == int(word.size()), "writer records complete UTF-8 byte lengths");
    std::vector<UCHR> text(index.GetSisLimit());
    require(fread(text.data(), 1, text.size(), fp) == text.size() &&
            std::memcmp(text.data(), word.data(), word.size()) == 0, "writer retains the folded UTF-8 term");
    require(std::all_of(text.begin()+word.size(), text.end(), [](UCHR c) { return c == 0; }),
            "short STRING keys are padded within their allocation bounds");
    endpoint(fp);
  }
  require(getc(fp) == EOF, "writer emits every distinct term once");
  fclose(fp);
}

static void differing_widths()
{
  Database db("widths");
  INDEX index(&db, "widths.inx");
  const Rows rows = {{u8"é", {1}}};
  partition(index, db, 1, 2, rows);
  partition(index, db, 2, 3, rows);
  require(db.mdt.SetIndexNum(2), "set mixed-width partition metadata");
  require(!index.CollapseIndexFiles() && db.mdt.GetIndexNum() == 2,
          "incompatible prefix widths fail before replacing input files");
  require(std::filesystem::exists("widths.inx.1") && std::filesystem::exists("widths.inx.2") &&
          !std::filesystem::exists("widths.inx"), "rejected collapse retains its source partitions");
}

int main()
{
  try {
    comparisons(); keys_and_lengths();
    Scratch scratch;
    writer();
    differing_widths();
    collapse(32, "whole");
    collapse(3, "prefix"); // splits 2/3/4-byte sequences without reading GP bytes
    std::cout << "UTF-8 SIS ordering, byte limits, callbacks, term preparation and real three-partition collapse passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
