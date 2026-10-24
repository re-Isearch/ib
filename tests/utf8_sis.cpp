#include "common.hxx"
#include "index.hxx"
#include "idbobj.hxx"
#include "lang-codes.hxx"
#include "magic.hxx"
#include "xml.hxx"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
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
  for (const std::string text : {u8"café", u8"中文", u8"𐐨", u8"café.rue", u8"foo.école", "0.word"})
    require(_ib_UTF8TermLength(bytes(text), text.size()) == text.size(), "term length advances over full UTF-8 characters");
  for (const std::string text : {"0..word", "0__word", "0.&word", "0._word", "0.@word", "0.0"})
    require(_ib_UTF8TermLength(bytes(text), text.size()) == 1,
            "source recovery cannot extend a numeric token through adjacent ASCII punctuation");
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
  DOCTYPE *doctype = nullptr;
  explicit Database(const char *name) : stem(name), mdt(stem, false) {
    MDTREC record;
    record.SetKey("fixture");
    require(record.SetLocalRecordRange(0, 0), "set fixture record range");
    require(mdt.AddEntry(record) == 1, "seed MDT partition metadata");
  }
  PMDT GetMainMdt() const override { return &mdt; }
  STRING ComposeDbFn(const CHR *extension) const override { return stem+extension; }
  DOCTYPE *GetDocTypePtr(const DOCTYPE_ID&) const override { return doctype; }
  bool IsStopWord(const UCHR *, STRINGINDEX, INT = 0) const override { return false; }
};

using Rows = std::map<std::string, std::vector<GPTYPE>>;

static void partition(INDEX& index, Database& db, int number, size_t width, const Rows& rows,
                      BYTE charset = 128)
{
  const STRING suffix = STRING().form(".%d", number);
  FILE *sis = fopen(db.ComposeDbFn(DbExtDict)+suffix, "wb");
  FILE *inx = fopen(db.stem+".inx"+suffix, "wb");
  require(sis && inx, "create partition files");
  const UINT2 magic = UINT2(((sizeof(GPTYPE) == 4 ? 0 : 1) << 14) |
                           ((IsBigEndian() ? objINDEXl : objINDEXm) << 5) |
                           (index.Version()%100));
  putINT2(magic, inx);
  putc(int(width), sis); putc(charset, sis);
  GPTYPE total = 0;
  const CHARSET collation(charset);
  std::vector<const Rows::value_type *> ordered;
  for (const auto& row : rows) ordered.push_back(&row);
  std::sort(ordered.begin(),ordered.end(),[&](const auto *left, const auto *right) {
    return collation.CompareSIS(bytes(left->first),left->first.size(),
                                bytes(right->first),right->first.size()) < 0;
  });
  for (const auto *entry : ordered) {
    const auto& row = *entry;
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

struct SourceIndex {
  Database db;
  INDEX index;
  Rows rows;
  std::map<std::string, size_t> terms;
  size_t width, records = 0;
  SourceIndex(const char *name, size_t prefix) : db(name), index(&db,db.stem+".inx"), width(prefix) { }
  GPTYPE store(const std::string& source, const std::string& afterRecord = {}) {
    const STRING name = db.stem+STRING().form("-%u.txt",unsigned(++records));
    std::ofstream file(name.c_str(),std::ios::binary);
    file << source << afterRecord; file.close();
    require(bool(file), "write indirect source fixture");
    const GPTYPE gp = db.mdt.GetNextGlobal();
    MDTREC record(&db.mdt);
    record.SetKey(STRING().form("source-%u",unsigned(records)));
    record.SetFullFileName(name);
    if (db.doctype) record.SetDocumentType(db.doctype->Name());
    record.SetGlobalFileStart(gp);
    require(record.SetLocalRecordRange(0,source.size()-1), "bound indirect source fixture");
    require(db.mdt.AddEntry(record) != 0, "add source metadata");
    return gp;
  }
  void add(const std::string& source, const std::string& term, const std::string& afterRecord = {}) {
    const GPTYPE gp = store(source,afterRecord);
    rows[term.substr(0,width)].push_back(gp);
    ++terms[term];
  }
  void publish(BYTE charset = 128) {
    partition(index,db,1,width,rows,charset);
    require(db.mdt.SetIndexNum(1), "publish source dictionary and postings");
  }
};

static void indirect_and_dump()
{
  SetGlobalCharset("UTF-8");
  SourceIndex fixture("source-utf8",3);
  fixture.add("abCAR","abcar"); // Same SIS prefix, descending full term order is valid.
  fixture.add(u8"abCAFÉ\u00a0ignored",u8"abcafé");
  fixture.add(u8"abcafé",u8"abcafé","outside-the-record");
  fixture.add(u8"abE\u0301COLE",std::string("abe\1\1cole"));
  fixture.add(u8"abfoo.école",u8"abfoo.école");
  fixture.add(u8"abTAIL\u00d7ignored","abtail");
  fixture.add(u8"abЖИЗНЬ",u8"abжизнь");
  fixture.add(u8"ab中文",u8"ab中文");
  fixture.add(u8"ab\U0001E900",u8"ab\U0001E922"); // Four-byte case mapping at record end.
  fixture.add(std::string("abcut\xc3"),"abcut");
  fixture.add(std::string("abcut\xe4\xb8"),"abcut");
  fixture.add(std::string("abcut\xf0\x90\x90"),"abcut");
  fixture.publish();
  fixture.index.SetSisLimit(12); // Dump must use the dictionary's actual width (3).
  require(SetGlobalCharset("ISO-8859-1"), "start dump with a different global charset");
  std::ostringstream dump;
  require(fixture.index.Dump(0,dump,false) == 0, "valid UTF-8 source postings pass Dump");
  require(GetGlobalCharset() == Charset2Id("ISO-8859-1"), "Dump restores its caller's charset");
  for (const auto& text : {u8"abCAFÉ",u8"abЖИЗНЬ",u8"ab中文",u8"ab\U0001E900",u8"abE\u0301COLE"})
    require(dump.str().find(text) != std::string::npos, "Dump preserves complete original UTF-8 display text");
  require(dump.str().find("ignored") == std::string::npos, "Unicode separators terminate displayed terms");
  require(dump.str().find("outside-the-record") == std::string::npos, "indirect reads stop at inclusive record end");

  SetGlobalCharset("UTF-8");
  SCANLIST list;
  require(fixture.index.Scan(&list,NulString,STRING("ab"),-1) == fixture.terms.size(),
          "prefix scan expands every truncated UTF-8 SIS key");
  std::map<std::string,size_t> actual;
  for (const auto *p = list.GetPtratomicSCANLIST()->Next(); p != list.GetPtratomicSCANLIST(); p = p->Next())
    actual[p->Term().toStdString()] += p->Frequency();
  require(actual == fixture.terms, "indirect terms retain whole folded UTF-8, separators and frequencies");

  SourceIndex boundary("source-boundary",StringCompLength);
  const std::string prefix(StringCompLength-1,'a');
  boundary.add(prefix+u8"Ёfirst",prefix+u8"ёfirst");
  boundary.add(prefix+u8"ёsecond",prefix+u8"ёsecond");
  boundary.publish();
  std::ostringstream bounded;
  require(boundary.index.Dump(0,bounded,false) == 0,
          "Dump folds complete characters before comparing a prefix split inside UTF-8");
  const std::string shown = bounded.str();
  for (size_t offset = 0; offset < shown.size();) {
    uint32_t cp;
    const int length = _to_ucs4(bytes(shown)+offset,bytes(shown)+shown.size(),&cp);
    require(length > 0, "Dump never emits a partial UTF-8 display character");
    offset += size_t(length);
  }

  SourceIndex lexical("source-order",20);
  lexical.add(u8"êcole",u8"êcole"); lexical.add(u8"école",u8"école");
  lexical.publish();
  // Reverse the stored keys and their posting groups, keeping endpoints valid.
  FILE *sis = fopen(lexical.db.ComposeDbFn(DbExtDict)+".1","r+b");
  FILE *inx = fopen(lexical.db.stem+".inx.1","r+b");
  require(sis && inx, "open deliberately disordered dictionary");
  std::vector<UCHR> first(lexical.width+1), second(lexical.width+1);
  const long secondRow = long(2+lexical.width+1+sizeof(GPTYPE));
  require(fseek(sis,2,SEEK_SET) == 0 && fread(first.data(),1,first.size(),sis) == first.size() &&
          fseek(sis,secondRow,SEEK_SET) == 0 && fread(second.data(),1,second.size(),sis) == second.size(),
          "read dictionary key windows");
  require(fseek(sis,2,SEEK_SET) == 0 && fwrite(second.data(),1,second.size(),sis) == second.size() &&
          fseek(sis,secondRow,SEEK_SET) == 0 && fwrite(first.data(),1,first.size(),sis) == first.size(),
          "reverse dictionary key windows");
  const GPTYPE a = lexical.index.GpFread(off_t(0),inx), b = lexical.index.GpFread(inx);
  require(fseek(inx,2,SEEK_SET) == 0 && lexical.index.GpFwrite(b,inx) == 1 &&
          lexical.index.GpFwrite(a,inx) == 1, "reverse the associated posting groups");
  fclose(sis); fclose(inx);
  std::ostringstream errors;
  require(lexical.index.Dump(0,errors,true) == 1 && errors.str().find("term order") != std::string::npos,
          "Dump detects stored Unicode ordering errors beyond the first byte");
  require(errors.str().find(u8"StoredKey=\"école\"") != std::string::npos &&
          errors.str().find(u8"PreviousKey=\"êcole\"") != std::string::npos,
          "dictionary order diagnostics identify both stored keys");

  SourceIndex prefixPostings("source-prefix-postings",3);
  prefixPostings.add("abcz","abcz"); prefixPostings.add("abca","abca");
  std::reverse(prefixPostings.rows.begin()->second.begin(),prefixPostings.rows.begin()->second.end());
  prefixPostings.publish();
  std::ostringstream prefixErrors;
  require(prefixPostings.index.Dump(0,prefixErrors,true) == 0,
          "different full terms sharing a SIS prefix can restart their posting GPs");
  require(prefixPostings.index.Dump(1,prefixErrors,true) == 0,
          "skipping postings still selects the correct SIS range");

  SourceIndex postings("source-postings",20);
  postings.add(u8"ÉCOLE",u8"école"); postings.add(u8"école",u8"école");
  auto& bucket = postings.rows.begin()->second;
  std::reverse(bucket.begin(),bucket.end());
  postings.publish();
  std::ostringstream gpErrors;
  require(postings.index.Dump(0,gpErrors,true) == 1 && gpErrors.str().find("posting order") != std::string::npos,
          "Dump detects reversed postings across Unicode case variants");

  require(SetGlobalCharset("ISO-8859-1"), "select single-byte source charset");
  SourceIndex latin("source-latin1",3);
  latin.add(std::string("abCAF\xc9"),std::string("abcaf\xe9"));
  latin.publish(Charset2Id("ISO-8859-1"));
  std::ostringstream singleByte;
  require(latin.index.Dump(0,singleByte,false) == 0 && singleByte.str().find("abCAF\xc9") != std::string::npos,
          "Dump retains single-byte case and classification rules");
  SCANLIST latinList;
  require(latin.index.Scan(&latinList,NulString,STRING("ab"),-1) == 1 &&
          latinList.GetEntry(1).Term() == STRING("abcaf\xe9"), "single-byte indirect term folding is unchanged");
  SetGlobalCharset("UTF-8");
}

static void xml_indirect_and_dump()
{
  for (const char *charset : {"ISO-8859-1", "UTF-8"}) {
    SourceIndex fixture(charset[0] == 'U' ? "xml-utf8" : "xml-latin1",3);
    XML xml(&fixture.db,"XML");
    fixture.db.doctype = &xml;
    require(SetGlobalCharset(charset), "select XML fixture charset");
    std::string repeated;
    for (size_t i = 0; i < 1000; ++i) repeated += "&amp;";
    const std::vector<std::string> sources = {
      "&amp;c. &amp;foobar foo&amp;bar &amp; amp",
      "<LINE>ABCDEFGHIJKLMNOPQRSTUVWXYZ &amp;c. &amp;foobar &amp;&amp;word</LINE>",
      "<LINE>AT&amp;T A&amp;Z A&moon &quot;answer&quot;</LINE>",
      "<LINE>&#38;foobar foo&#46;bar alpha&#32;beta &#233;COLE &amp;amp;term</LINE>",
      "<LINE>"+repeated+"word</LINE>"
    };
    std::map<GPTYPE,std::string> postingTerms;
    for (const auto& source : sources) {
      const GPTYPE base = fixture.store(source);
      MDTREC record(&fixture.db.mdt);
      require(fixture.db.mdt.GetMdtRecord(base,&record) != 0, "read XML source metadata");
      std::vector<UCHR> normalized(source.begin(),source.end());
      normalized.resize(source.size()+StringCompLength+1,0);
      std::vector<GPTYPE> positions(source.size()+1);
      const size_t count = xml.ParseWords(normalized.data(),source.size(),0,positions.data(),positions.size());
      require(count > 0, "real XML parser produces entity-normalized postings");
      for (size_t i = 0; i < count; ++i) {
        const std::string term(reinterpret_cast<const char *>(normalized.data()+positions[i]));
        UCHR recovered[StringCompLength*2+5] = {};
        const int length = xml.GetTerm(record.GetFullFileName(),reinterpret_cast<char *>(recovered),
                                       positions[i],StringCompLength+4);
        GPTYPE recoveredPositions[StringCompLength+5];
        const size_t recoveredCount = length > 0 ? xml.DOCTYPE::ParseWords(recovered,length,0,
            recoveredPositions,StringCompLength+5) : 0;
        require(recoveredCount > 0 && recoveredPositions[0] == 0 &&
                std::string(reinterpret_cast<const char *>(recovered)) == term,
                "XML source recovery agrees with real ingestion at every GP without decoding twice");
        fixture.rows[term.substr(0,fixture.width)].push_back(base+positions[i]);
        postingTerms.emplace(base+positions[i],term);
        ++fixture.terms[term];
      }
    }
    // Model one ingestion shard: full sort keys, then GP tie-breaking.
    for (auto& row : fixture.rows)
      std::sort(row.second.begin(),row.second.end(),[&](GPTYPE left, GPTYPE right) {
        const int order = _ib_SisCompare(bytes(postingTerms.at(left)),bytes(postingTerms.at(right)),StringCompLength);
        return order ? order < 0 : left < right;
      });
    fixture.publish(Charset2Id(charset));
    std::ostringstream errors;
    require(fixture.index.Dump(0,errors,true) == 0,
            "entity-normalized XML postings pass the source order check");
    SCANLIST list;
    std::map<std::string,size_t> expected, actual;
    for (const auto& term : fixture.terms)
      if (term.first.compare(0,3,"foo") == 0) expected.insert(term);
    require(fixture.index.Scan(&list,NulString,STRING("foo"),-1) == expected.size(),
            "XML prefix scan expands the entity-normalized SIS bucket");
    for (const auto *p = list.GetPtratomicSCANLIST()->Next(); p != list.GetPtratomicSCANLIST(); p = p->Next())
      actual[p->Term().toStdString()] += p->Frequency();
    require(actual == expected, "XML source recovery preserves full terms and frequencies");
    fixture.db.doctype = nullptr;
  }
  SetGlobalCharset("UTF-8");
}

static void numeric_punctuation_after_merge()
{
  SourceIndex fixture("xml-zero-merge",3);
  XML xml(&fixture.db,"XML");
  fixture.db.doctype = &xml;
  SetGlobalCharset("UTF-8");
  const std::vector<std::string> sources = {
    "<LINE>0..alpha 0._beta 0.&gamma 0.0 0.1 0.omega</LINE>",
    "<LINE>0 0__delta 0 0.@epsilon 0 0.omega</LINE>"
  };
  size_t zeros = 0;
  for (size_t shard = 0; shard < sources.size(); ++shard) {
    const auto& source = sources[shard];
    const GPTYPE base = fixture.store(source);
    std::vector<UCHR> normalized(source.begin(),source.end());
    normalized.resize(source.size()+StringCompLength+1,0);
    std::vector<GPTYPE> positions(source.size()+1);
    const size_t count = xml.ParseWords(normalized.data(),source.size(),0,positions.data(),positions.size());
    require(count > 0, "tokenize numeric XML fixture through the real doctype");
    Rows rows;
    for (size_t i = 0; i < count; ++i) {
      const std::string term(reinterpret_cast<const char *>(normalized.data()+positions[i]));
      rows[term.substr(0,fixture.width)].push_back(base+positions[i]);
      if (term == "0") ++zeros;
    }
    partition(fixture.index,fixture.db,int(shard+1),fixture.width,rows);
  }
  require(zeros > 2 && fixture.db.mdt.SetIndexNum(2), "publish numeric XML ingestion shards");
  std::ostringstream before, after;
  const long beforeErrors = fixture.index.Dump(0,before,true);
  require(fixture.index.MergeIndexFiles(), "zip numeric XML shards without retokenizing");
  const long afterErrors = fixture.index.Dump(0,after,true);
  if (beforeErrors || afterErrors) std::cerr << before.str() << after.str();
  require(beforeErrors == 0 && afterErrors == 0,
          "numeric punctuation has identical token boundaries before and after merging");
  FILE *sis = fopen(fixture.db.ComposeDbFn(DbExtDict),"rb");
  require(sis && getc(sis) == int(fixture.width) && getc(sis) == Charset2Id("UTF-8") &&
          getc(sis) == 1 && getc(sis) == '0', "merged dictionary retains the singleton zero key");
  require(fseek(sis,long(fixture.width-1),SEEK_CUR) == 0 && endpoint(sis)+1 == zeros,
          "merge preserves every singleton zero posting");
  fclose(sis);
  SCANLIST list;
  require(fixture.index.Scan(&list,NulString,STRING("0."),-1) == 1 &&
          list.GetEntry(1).Term() == STRING("0.omega") && list.GetEntry(1).Frequency() == 2,
          "numeric compound scan retains a single dot followed by a letter");
  fixture.db.doctype = nullptr;
}

struct TextIngestDatabase : Database {
  DFDT dfdt;
  DOCTYPE text;
  bool firstCharacterOnly = false;
  explicit TextIngestDatabase(const char *name) : Database(name), text(this,"TEXT") { doctype = &text; }
  size_t GetIndexingMemory() const override { return 32768; }
  PDFDT GetMainDfdt() override { return &dfdt; }
  STRING GetDbFileStem() const override { return stem; }
  void SelectRegions(const RECORD& record, FCT *regions) const override {
    regions->AddEntry(FC(0,firstCharacterOnly ? 0 : record.GetRecordEnd()-record.GetRecordStart()));
  }
  GPTYPE ParseWords(const DOCTYPE_ID&, UCHR *buffer, GPTYPE length, GPTYPE start,
                   GPTYPE *postings, GPTYPE capacity) override {
    return text.ParseWords(buffer,length,start,postings,capacity);
  }
};

static void shared_ingestion_check()
{
  TextIngestDatabase db("shared-ingestion");
  INDEX index(&db,db.stem+".inx");
  index.SetMergeStatus(iNothing);
  index.SetSisLimit(20);
  SetGlobalCharset("UTF-8");
  FILE *queue = fopen("shared-ingestion.queue","w+b");
  require(queue != nullptr, "create shared tokenizer ingestion queue");
  for (size_t i = 0; i < 3; ++i) {
    const STRING filename = db.stem+STRING().form("-%u.json",unsigned(i));
    std::string source = u8"{\"text\":\"0—X 00−Y 000∞ 0000€ 000m₀ 0\",\"number\":0.001,\"filler\":\"";
    for (size_t j = 0; j < 64; ++j) source += "z ";
    source += "\"}\n";
    source.resize(8192,' ');
    std::ofstream file(filename.c_str(),std::ios::binary);
    file << source; file.close();
    require(bool(file), "write raw UTF-8 ingestion source with JSON syntax");
    RECORD record(filename);
    record.SetDocumentType("TEXT");
    record.SetRecordEnd(source.size()-1);
    record.SetKey(STRING().form("shared-%u",unsigned(i)));
    putc('#',queue); record.Write(queue);
  }
  rewind(queue);
  require(index.AddRecordList(queue), "ingest through the real shared tokenizer, sort and flush path");
  fclose(queue);
  require(db.mdt.GetIndexNum() > 1, "shared ingestion creates multiple real shards");
  std::ostringstream before, after;
  const long beforeErrors = index.Dump(0,before,true);
  require(index.MergeIndexFiles(), "zip real shared-tokenizer shards");
  const long afterErrors = index.Dump(0,after,true);
  if (beforeErrors || afterErrors) std::cerr << before.str() << after.str();
  require(beforeErrors == 0 && afterErrors == 0,
          "stored numeric keys validate before and after merge despite longer recovered source text");
}

static void selected_region_check()
{
  TextIngestDatabase db("selected-region");
  db.firstCharacterOnly = true;
  INDEX index(&db,db.stem+".inx");
  SetGlobalCharset("UTF-8");
  std::ofstream source("selected-region.txt"); source << "0more"; source.close();
  FILE *queue = fopen("selected-region.queue","w+b");
  require(queue != nullptr, "create selected region ingestion queue");
  RECORD record("selected-region.txt");
  record.SetDocumentType("TEXT"); record.SetRecordEnd(4); record.SetKey("selected-zero");
  putc('#',queue); record.Write(queue); rewind(queue);
  require(index.AddRecordList(queue), "index a region ending inside the original source word");
  fclose(queue);
  std::ostringstream errors;
  require(index.Dump(0,errors,true) == 0, "stored token boundaries retain the selected region end");
}

static void dictionary_integrity_checks()
{
  SetGlobalCharset("UTF-8");
  SourceIndex mismatch("source-mismatch",20);
  mismatch.add("0<&\"tail","999"); mismatch.publish();
  std::ostringstream errors;
  require(mismatch.index.Dump(0,errors,true) == 1 &&
          errors.str().find("source term mismatch") != std::string::npos &&
          errors.str().find("StoredKey=\"999\"") != std::string::npos &&
          errors.str().find("SourceOffset=\"0\"") != std::string::npos &&
          errors.str().find("Context=\"0&lt;&amp;&quot;tail\"") != std::string::npos,
          "a wrong posting association reports the stored key, raw offset and escaped context");
  for (int kind = 0; kind < 5; ++kind) {
    const std::string name = "broken-dictionary-"+std::to_string(kind);
    SourceIndex broken(name.c_str(),20);
    broken.add("abc","abc"); broken.publish();
    const STRING path = broken.db.ComposeDbFn(DbExtDict)+".1";
    if (kind == 0) std::filesystem::resize_file(path.c_str(),std::filesystem::file_size(path.c_str())-1);
    if (kind == 1) {
      FILE *fp = fopen(broken.db.stem+".inx.1","ab");
      require(fp && broken.index.GpFwrite(broken.rows.begin()->second.front(),fp) == 1,
              "append a posting not covered by the SIS");
      fclose(fp);
    }
    if (kind == 2) {
      std::fstream fp(path.c_str(),std::ios::binary|std::ios::in|std::ios::out);
      fp.seekp(2); fp.put(char(broken.width+1));
      require(bool(fp), "write an oversized dictionary key length");
    }
    if (kind == 3) require(std::filesystem::remove(path.c_str()), "remove a required dictionary");
    if (kind == 4) {
      FILE *fp = fopen(path,"r+b");
      require(fp && fseek(fp,long(2+broken.width+1),SEEK_SET) == 0 &&
              ::Write(GPTYPE(1),fp) == int(sizeof(GPTYPE)), "write an out-of-range posting endpoint");
      fclose(fp);
    }
    std::ostringstream failure;
    require(broken.index.Dump(0,failure,true) == 1 && failure.str().find("broken SIS") != std::string::npos,
            "the checker rejects malformed or missing SIS data and incomplete posting coverage");
  }
  SourceIndex unreadable("broken-posting",20);
  unreadable.add("abc","abc"); unreadable.publish();
  FILE *fp = fopen(unreadable.db.stem+".inx.1","r+b");
  require(fp && fseek(fp,2,SEEK_SET) == 0 && unreadable.index.GpFwrite(GPTYPE(-1),fp) == 1,
          "replace a posting with the reserved EOF value");
  fclose(fp);
  std::ostringstream earlyEnd;
  require(unreadable.index.Dump(0,earlyEnd,true) == 1 && earlyEnd.str().find("INX read failure") != std::string::npos,
          "an early posting-stream sentinel is a reported index error");
  Database empty("empty-dictionary");
  INDEX emptyIndex(&empty,empty.stem+".inx");
  partition(emptyIndex,empty,1,20,Rows{});
  require(empty.mdt.SetIndexNum(1), "publish an empty index pair");
  std::ostringstream emptyErrors;
  require(emptyIndex.Dump(0,emptyErrors,true) == 0, "an empty dictionary and posting stream validate");
}

int main()
{
  try {
    comparisons(); keys_and_lengths();
    Scratch scratch;
    writer();
    differing_widths();
    indirect_and_dump();
    xml_indirect_and_dump();
    numeric_punctuation_after_merge();
    shared_ingestion_check();
    selected_region_check();
    dictionary_integrity_checks();
    collapse(32, "whole");
    collapse(3, "prefix"); // splits 2/3/4-byte sequences without reading GP bytes
    std::cout << "UTF-8 SIS ordering, byte limits, callbacks, source recovery, Dump checks and real three-partition collapse passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
