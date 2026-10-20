#include "common.hxx"
#include "index.hxx"
#include "idbobj.hxx"
#include "merge.hxx"
#include "magic.hxx"
#include <algorithm>
#include <cstring>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool ok, const char *message)
{
  if (!ok) throw std::runtime_error(message);
}
using Rows = std::map<std::string, std::vector<GPTYPE>>;
static constexpr UINT2 testMagic = 0x1234;
static constexpr size_t width = 32;

static MERGE::INPUT paths(const std::string& stem)
{
  return {STRING((stem+".inx").c_str()), STRING((stem+".sis").c_str())};
}
static void runFile(const MERGE::INPUT& path, const Rows& rows, size_t prefix = width,
                    BYTE charset = 128, UINT2 magic = testMagic)
{
  FILE *inx = fopen(path.index, "wb"), *sis = fopen(path.sis, "wb");
  require(inx && sis, "create shard fixture");
  putINT2(magic, inx);
  putc(int(prefix), sis); putc(charset, sis);
  GPTYPE total = 0;
  for (const auto& row : rows) {
    require(row.first.size() <= prefix && !row.second.empty(), "valid fixture row");
    putc(int(row.first.size()), sis);
    fwrite(row.first.data(), 1, row.first.size(), sis);
    for (size_t i = row.first.size(); i < prefix; ++i) putc(0xa5, sis);
    for (GPTYPE gp : row.second) { fwrite(&gp, sizeof(gp), 1, inx); ++total; }
    ::Write(total-1, sis);
  }
  require(fclose(inx) == 0 && fclose(sis) == 0, "close shard fixture");
}
static void verify(const MERGE::INPUT& path, const Rows& expected, size_t prefix = width,
                   BYTE charset = 128, UINT2 magic = testMagic)
{
  FILE *inx = fopen(path.index, "rb"), *sis = fopen(path.sis, "rb");
  require(inx && sis, "open merged fixture");
  require(getINT2(inx) == magic && getc(sis) == int(prefix) && getc(sis) == charset,
          "merged headers");
  GPTYPE total = 0;
  for (const auto& row : expected) {
    require(getc(sis) == int(row.first.size()), "merged SIS key length");
    std::vector<UCHR> key(prefix);
    require(fread(key.data(), 1, prefix, sis) == prefix &&
            std::memcmp(key.data(), row.first.data(), row.first.size()) == 0,
            "merged SIS ordering and prefix bytes");
    for (GPTYPE expectedGp : row.second) {
      GPTYPE actual;
      require(fread(&actual, sizeof(actual), 1, inx) == 1 && actual == expectedGp,
              "stable posting order and value");
      ++total;
    }
    UCHR endpoint[sizeof(GPTYPE)];
    require(fread(endpoint, 1, sizeof(endpoint), sis) == sizeof(endpoint) &&
            getGPTYPE(endpoint, 0) == total-1, "merged SIS posting endpoint");
  }
  require(getc(inx) == EOF && getc(sis) == EOF, "exact merged output lengths");
  fclose(inx); fclose(sis);
}
static void zipper(size_t count, size_t prefix)
{
  const std::string stem = "zip-"+std::to_string(count)+"-"+std::to_string(prefix);
  std::vector<MERGE::INPUT> sources;
  Rows expected;
  GPTYPE next = sizeof(GPTYPE) > 4 ? GPTYPE(1) << 40 : 10000;
  for (size_t i = 0; i < count; ++i) {
    Rows rows;
    if (i%7 != 3) {
      const std::vector<std::string> words = {
        "a", "common", "tail-"+std::to_string(i), u8"abé", u8"abê",
        u8"ab中", u8"ab𐐨", u8"café", u8"中文", u8"𐐨"
      };
      for (const auto& word : words) {
        const auto key = word.substr(0, prefix);
        rows[key].push_back(++next);
        expected[key].push_back(next);
      }
    }
    sources.push_back(paths(stem+"-"+std::to_string(i)));
    runFile(sources.back(), rows, prefix);
  }
  const auto output = paths(stem+"-out");
  MERGE merge; MERGE::STATS stats;
  std::vector<STRING> common;
  require(merge.Zip(sources, output, testMagic, 100000000, 1, &common, &stats),
          "zip all shards");
  verify(output, expected, prefix);
  GPTYPE postings = 0;
  size_t commonCount = 0;
  for (const auto& row : expected) {
    postings += row.second.size();
    commonCount += row.second.size() > 1;
  }
  require(stats.postings == postings && stats.terms == expected.size() &&
          common.size() == commonCount, "zip counts and common-word aggregation");
  require(stats.passes == (count > MERGE::MaxFanIn ? 2 : 1), "bounded fan-in pass count");
  for (const auto& path : sources)
    require(std::filesystem::exists(path.index.c_str()) && std::filesystem::exists(path.sis.c_str()),
            "engine leaves publication and input retirement to INDEX");
}
static void patchByte(const STRING& path, size_t offset, UCHR byte)
{
  std::fstream fp(path.c_str(), std::ios::binary|std::ios::in|std::ios::out);
  fp.seekp(offset); fp.put(char(byte));
  require(bool(fp), "patch malformed fixture");
}
static void failures()
{
  MERGE merge;
  for (int kind = 0; kind < 9; ++kind) {
    const std::string stem = "bad-"+std::to_string(kind);
    std::vector<MERGE::INPUT> input = {paths(stem+"-1"), paths(stem+"-2")};
    const auto output = paths(stem+"-out");
    runFile(input[0], {{"a",{1}},{"b",{2}}});
    runFile(input[1], {{"a",{3}}}, kind == 0 ? 31 : width,
            kind == 1 ? 1 : 128, kind == 2 ? UINT2(testMagic+1) : testMagic);
    if (kind == 3)
      std::filesystem::resize_file(input[0].sis.c_str(), std::filesystem::file_size(input[0].sis.c_str())-1);
    if (kind == 4)
      std::filesystem::resize_file(input[0].index.c_str(), std::filesystem::file_size(input[0].index.c_str())-1);
    if (kind == 5) patchByte(input[0].sis, 3, 'z'); // reversed dictionary
    if (kind == 6) patchByte(input[0].sis, 2+width+sizeof(GPTYPE), 9); // out-of-bounds endpoint
    if (kind == 7) { // valid rows that fail to cover an extra posting
      FILE *fp = fopen(input[0].index, "ab"); GPTYPE gp = 4;
      require(fp && fwrite(&gp, sizeof(gp), 1, fp) == 1, "append orphan posting");
      fclose(fp);
    }
    if (kind == 8) patchByte(input[0].sis, 0, 0);
    require(!merge.Zip(input, output, testMagic, 10000, 100), "malformed merge is rejected");
    for (const auto& path : input)
      require(std::filesystem::exists(path.index.c_str()) && std::filesystem::exists(path.sis.c_str()),
              "rejected merge retains every input");
    require(!std::filesystem::exists(output.index.c_str()) &&
            !std::filesystem::exists(output.sis.c_str()), "failed output is removed");
  }
  const auto input = paths("capacity"), output = paths("capacity-out");
  runFile(input, {{"a",{1,2,3}}});
  require(!merge.Zip({input}, output, testMagic, 3, 100), "capacity boundary is checked");
  const auto empty = paths("empty"), emptyOut = paths("empty-out");
  runFile(empty, {});
  require(merge.Zip({empty,empty}, emptyOut, testMagic, 100, 100), "empty shards merge");
  verify(emptyOut, {});
}
static void largeAndLatin1()
{
  MERGE merge;
  std::vector<GPTYPE> a(200000), b(200000);
  for (size_t i=0; i<a.size(); ++i) { a[i]=GPTYPE(i+1); b[i]=GPTYPE(a.size()+i+1); }
  const auto left=paths("large-left"), right=paths("large-right"), output=paths("large-out");
  runFile(left, {{"common",a}}); runFile(right, {{"common",b}});
  require(merge.Zip({left,right},output,testMagic,1000000,100), "copy lists spanning multiple buffers");
  a.insert(a.end(),b.begin(),b.end()); verify(output,{{"common",a}});
  const auto latinLeft=paths("latin-left"), latinRight=paths("latin-right"), latinOut=paths("latin-out");
  runFile(latinLeft,{{"a",{1}},{"b",{2}}},width,1);
  runFile(latinRight,{{"A",{3}},{"z",{4}}},width,1);
  require(merge.Zip({latinLeft,latinRight},latinOut,testMagic,1000,100), "merge single-byte charset");
  verify(latinOut,{{"a",{1,3}},{"b",{2}},{"z",{4}}},width,1);
  const auto prefix=paths("long-length"), prefixOut=paths("long-length-out");
  runFile(prefix,{{"abc",{7}}},3);
  patchByte(prefix.sis,2,200);
  require(merge.Zip({prefix},prefixOut,testMagic,1000,100), "bound original term length by stored prefix width");
  FILE *fp=fopen(prefixOut.sis,"rb");
  require(fp && fseek(fp,2,SEEK_SET)==0 && getc(fp)==200, "preserve original SIS length metadata");
  fclose(fp);
  const MERGE::INPUT dead=paths("dead-worker");
  const MERGE::INPUT owned={dead.index+".zip.0.0",dead.sis+".zip.1.2"};
  const STRING unrelated=dead.index+".zip.keep";
  for (const auto& name : {owned.index,owned.sis,unrelated}) {
    FILE *file=fopen(name,"wb"); require(file != nullptr,"create temporary cleanup fixture"); fclose(file);
  }
  MERGE::RemoveTemporaries(dead);
  require(!std::filesystem::exists(owned.index.c_str()) &&
          !std::filesystem::exists(owned.sis.c_str()) &&
          std::filesystem::exists(unrelated.c_str()),"stale cleanup matches only owned numbered intermediates");
  require(UnlinkFile(unrelated) == 0, "remove owned unrelated cleanup fixture");
}

static void bufferedWriteFailure()
{
#if !defined(_WIN32) && defined(RLIMIT_FSIZE) && defined(SIGXFSZ)
  const auto input=paths("write-failure"), output=paths("write-failure-out");
  runFile(input,{{"a",std::vector<GPTYPE>(100,7)}});
  struct Limit {
    struct rlimit previous;
    using Handler = void (*)(int);
    Handler handler;
    Limit() {
      require(getrlimit(RLIMIT_FSIZE,&previous)==0,"read process file-size limit");
      handler=std::signal(SIGXFSZ,SIG_IGN);
      require(handler!=SIG_ERR,"ignore file-size signal during write-error test");
      struct rlimit limit=previous; limit.rlim_cur=64;
      if (setrlimit(RLIMIT_FSIZE,&limit)!=0) {
        std::signal(SIGXFSZ,handler);
        throw std::runtime_error("set process file-size limit");
      }
    }
    ~Limit() { setrlimit(RLIMIT_FSIZE,&previous); std::signal(SIGXFSZ,handler); }
  };
  bool result;
  {
    Limit limit;
    MERGE merge;
    result=merge.Zip({input},output,testMagic,10000,100);
  }
  require(!result && std::filesystem::exists(input.index.c_str()) &&
          !std::filesystem::exists(output.index.c_str()) &&
          !std::filesystem::exists(output.sis.c_str()),
          "buffered output failure leaves the input and removes both temporary outputs");
#endif
}

struct Database : IDBOBJ {
  STRING stem;
  mutable MDT mdt;
  explicit Database(const char *name) : stem(name), mdt(stem, false) {
    MDTREC record; record.SetKey("fixture");
    require(record.SetLocalRecordRange(0, 0), "set fixture record range");
    require(mdt.AddEntry(record) == 1, "seed MDT");
  }
  PMDT GetMainMdt() const override { return &mdt; }
  STRING ComposeDbFn(const CHR *ext) const override { return stem+ext; }
};
static void integration(bool invalid = false, bool renameFailure = false, bool lockFailure = false)
{
  const char *name = invalid ? "invalid-index" : renameFailure ? "rename-index" :
                     lockFailure ? "lock-index" : "live-index";
  Database db(name);
  INDEX index(&db, db.stem+".inx");
  // Same native posting header written by INDEX::FlushIndexFiles().
  const UINT2 magic = UINT2(((sizeof(GPTYPE) == 4 ? 0 : 1) << 14) |
                           ((IsBigEndian() ? objINDEXl : objINDEXm) << 5) |
                           (index.Version()%100));
  Rows expected;
  for (int i = 1; i <= 10; ++i) {
    const MERGE::INPUT path = {db.stem+STRING().form(".inx.%d",i),
                               db.ComposeDbFn(DbExtDict)+STRING().form(".%d",i)};
    const Rows rows = {{"a",{GPTYPE(i)}},{u8"é",{GPTYPE(100+i)}}};
    runFile(path, rows, width, 128, magic);
    for (const auto& row : rows)
      expected[row.first].insert(expected[row.first].end(), row.second.begin(), row.second.end());
  }
  if (invalid) patchByte(db.ComposeDbFn(DbExtDict)+".7", 0, 31);
  if (renameFailure) std::filesystem::create_directory(db.ComposeDbFn(DbExtDict).c_str());
  if (lockFailure) std::filesystem::create_directory((db.stem+".inx.lock").c_str());
  require(db.mdt.SetIndexNum(10), "publish ten input shards");
  FILE *reader = fopen(db.stem+".inx.1", "rb");
  require(reader != nullptr, "open reader before merge");
  const bool result = index.MergeIndexFiles();
  require(result == !(invalid || renameFailure || lockFailure), "INDEX propagates merge failure");
  require(db.mdt.GetIndexNum() == (result ? 0 : 10), "INDEX publishes only a completed pair");
  require(std::filesystem::exists((db.stem+".inx.lock").c_str()) == lockFailure,
          "optimizer lock is released only when owned");
  // A reader holding an input stream remains able to finish after publication.
  require(getINT2(reader) == magic, "old reader retains its posting stream");
  GPTYPE first;
  require(fread(&first, sizeof(first), 1, reader) == 1 && first == 1, "old reader completes");
  fclose(reader);
  if (result) verify({db.stem+".inx",db.ComposeDbFn(DbExtDict)}, expected, width,128,magic);
  for (int i = 1; i <= 10; ++i) {
    require(std::filesystem::exists((db.stem+STRING().form(".inx.%d",i)).c_str()) != result,
            "input retirement follows successful publication");
  }
}
int main()
{
  const auto previous = std::filesystem::current_path();
  const auto directory = previous/"zip-merge-fixtures";
  try {
    require(std::filesystem::create_directory(directory), "create owned merge fixtures");
    std::filesystem::current_path(directory);
    for (size_t count : {size_t(1),size_t(2),size_t(3),size_t(10),size_t(130)})
      zipper(count, width);
    zipper(10, 3); // SIS prefixes ending within UTF-8 sequences.
    failures(); largeAndLatin1(); bufferedWriteFailure();
    integration(); integration(true); integration(false,true); integration(false,false,true);
    for (const auto& item : std::filesystem::directory_iterator("."))
      require(item.path().filename().string().find(".zip.") == std::string::npos,
              "intermediate runs are cleaned");
    std::filesystem::current_path(previous);
    std::filesystem::remove_all(directory);
    std::cout << "Buffered zipper: stable postings, UTF-8 prefixes, 130 shards, failure handling and INDEX publication passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::filesystem::current_path(previous);
    std::cerr << error.what() << " (fixtures: " << directory << ")\n";
    return 1;
  }
}
