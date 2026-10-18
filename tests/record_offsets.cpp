#include "common.hxx"
#include "record.hxx"
#include "reclist.hxx"
#include "result.hxx"
#include "rset.hxx"
#include "mdt.hxx"
#include "magic.hxx"
#include <filesystem>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

static void require(bool ok, const char *message)
{
  if (!ok) throw std::runtime_error(message);
}

struct Scratch {
  std::filesystem::path previous = std::filesystem::current_path();
  std::filesystem::path directory = previous / "record-offsets-fixtures";
  Scratch() {
    require(std::filesystem::create_directory(directory), "create an isolated fixture directory");
    std::filesystem::current_path(directory);
  }
  ~Scratch() {
    std::filesystem::current_path(previous);
    std::filesystem::remove_all(directory);
  }
};

static std::vector<BYTE> bytes(const STRING& filename)
{
  FILE *fp = fopen(filename, "rb");
  require(fp != nullptr, "open fixture bytes");
  std::vector<BYTE> result(size_t(GetFileSize(fp)));
  require(fread(result.data(), 1, result.size(), fp) == result.size(), "read fixture bytes");
  fclose(fp);
  return result;
}

static void write_bytes(const STRING& filename, const std::vector<BYTE>& value)
{
  FILE *fp = fopen(filename, "wb");
  require(fp && fwrite(value.data(), 1, value.size(), fp) == value.size() && fclose(fp) == 0,
          "write fixture bytes");
}

static void wide_documents()
{
  static_assert(std::is_same<decltype(RECORD().GetRecordStart()), GPTYPE>::value, "wide RECORD start");
  static_assert(std::is_same<decltype(RECORD().GetRecordEnd()), GPTYPE>::value, "wide RECORD end");
  static_assert(std::is_same<decltype(RESULT().GetRecordStart()), GPTYPE>::value, "wide RESULT start");
  RECORD whole;
  whole.SetKey("whole-container");
  whole.SetRecordStart(0);
  whole.SetRecordEnd(GPTYPE(100000000000000ULL));
  require(whole.GetLength() == off_t(100000000000001ULL), "container ranges must not use a 32-bit span");
  RECORD patent;
  patent.SetKey("small-patent");
  patent.SetRecordStart((GPTYPE(16) << 30) + 17);
  patent.SetRecordEnd(patent.GetRecordStart()+511);
  patent.SetCategory(123);
  patent.SetPriority(7);
  patent.SetSegment(23);
  patent.SetBadRecord();
  const STRING queue("record.queue");
  FILE *fp = fopen(queue, "w+b");
  require(fp != nullptr, "open record queue");
  whole.Write(fp); patent.Write(fp);
  rewind(fp);
  RECORD restored;
  require(restored.Read(fp) && restored.GetKey() == whole.GetKey() &&
          restored.GetRecordEnd() == whole.GetRecordEnd(), "read a wide container from the queue");
  require(restored.Read(fp) && restored.GetRecordStart() == patent.GetRecordStart() &&
          restored.GetRecordEnd() == patent.GetRecordEnd() && restored.GetLength() == 512 &&
          restored.GetCategory() == 123 && restored.GetPriority() == 7 &&
          restored.GetSegment() == 23 && restored.IsBadRecord(), "read the next bounded record without misalignment");
  fclose(fp);
  RECLIST records;
  records.AddEntry(whole); records.AddEntry(patent);
  fp = fopen(queue, "w+b");
  records.Write(fp); rewind(fp);
  RECLIST loaded;
  require(loaded.Read(fp) && loaded.GetTotalEntries() == 2 && loaded.GetEntry(2, &restored) &&
          restored.GetRecordStart() == patent.GetRecordStart(), "record lists retain wide offsets");
  fclose(fp);
  write_bytes(queue, {objRECLISTGP, 0, 0, 0, 1, objRECORD});
  fp = fopen(queue, "rb");
  require(!loaded.Read(fp) && loaded.GetTotalEntries() == 2,
          "record lists propagate an incompatible child format without replacing valid data");
  fclose(fp);
  for (std::vector<BYTE> invalid : {std::vector<BYTE>{objRECORD},
                                   std::vector<BYTE>{objRECORDGP, 4},
                                   std::vector<BYTE>{objRECORDGP, 8}}) {
    write_bytes(queue, invalid);
    fp = fopen(queue, "rb");
    require(!restored.Read(fp), "reject old, wrong-width and incomplete record streams");
    fclose(fp);
  }
  patent.SetRecordStart(100);
  patent.SetRecordEnd(99);
  require(patent.GetLength() == -1, "reversed document ranges do not underflow");
}

static void spans()
{
  const GPTYPE start = GPTYPE(100000000000000ULL);
  MDTREC record;
  require(record.SetLocalRecordRange(start, start+511) && record.GetRecordSpan() == 511 &&
          record.GetLocalRecordStart() == start && record.GetLocalRecordEnd() == start+511 &&
          record.GetLength() == 511, "wide source position with a bounded span");
  MDTREC copy(record);
  require(copy.GetLocalRecordEnd() == start+511 && copy.HasValidRecordRange(), "copy the complete range state");
  copy.SetLocalRecordStart(start+1000);
  require(copy.GetLocalRecordEnd() == start+1511, "moving the start preserves the span");
  copy.FlipBytes(); copy.FlipBytes();
  require(copy.GetLocalRecordStart() == start+1000 && copy.GetRecordSpan() == 511,
          "byte swapping uses wide starts and 32-bit spans");

  const STRING file("span.raw");
  FILE *fp = fopen(file, "w+b");
  require(fp != nullptr, "open raw metadata fixture");
  BYTE padding[16] = {};
  require(fwrite(padding, 1, 16, fp) == 16, "write metadata header space");
  copy.SetDeleted(true);
  require(copy.Write(fp, 1) && copy.IsDeleted(fp, 1), "read deletion state at the actual packed-field offset");
  MDTREC restored;
  require(restored.Read(fp, 1) && restored.GetLocalRecordEnd() == start+1511 &&
          restored.GetDeleted(), "persist and restore start/span metadata");
  copy.SetDeleted(false);
  require(copy.Write(fp, 1) && !copy.IsDeleted(fp, 1), "read an undeleted packed row");
  copy.FlipBytes();
  require(copy.Write(fp, 1) && restored.Read(fp, 1), "write and read an opposite-endian row");
  restored.FlipBytes();
  require(restored.GetLocalRecordStart() == start+1000 && restored.GetRecordSpan() == 511,
          "opposite-endian spans reconstruct the same endpoint");
  fclose(fp);

  require(record.SetLocalRecordRange(start, start+MDTREC::MaxRecordSpan()) && record.GetRecordSpan() == MDTREC::MaxRecordSpan(),
          "the full UINT4 span is representable");
  require(!record.SetLocalRecordEnd(start+GPTYPE(MDTREC::MaxRecordSpan())+1) && !record.HasValidRecordRange() &&
          record.GetLength() == -1, "oversized record spans cannot truncate");
  fp = fopen(file, "w+b");
  require(fp && !record.Write(fp) && ftell(fp) == 0, "invalid spans cannot be written");
  fclose(fp);
  require(!record.SetLocalRecordRange(start, start-1), "reject reversed spans");
  require(record.SetLocalRecordRange(MAX_GPTYPE-1, MAX_GPTYPE), "endpoint at the GP limit");
  record.SetLocalRecordStart(MAX_GPTYPE);
  require(!record.HasValidRecordRange(), "moving a start cannot wrap its derived end");
}

static void sparse_file_and_mdt()
{
#ifndef _WIN32
  const GPTYPE positions[] = {GPTYPE(MDTREC::MaxRecordSpan())-4, (GPTYPE(1)<<32)+1024, (GPTYPE(16)<<30)+7};
  const char *payloads[] = {"patent-crossing-4GiB", "patent-above-4GiB", "patent-near-16GiB"};
  const STRING source("patents.sparse");
  FILE *fp = fopen(source, "w+b");
  require(fp != nullptr, "open sparse patent source");
  require(fwrite("low-offset-decoy", 1, 16, fp) == 16, "write a low-offset decoy");
  for (size_t i = 0; i < 3; ++i)
    require(fseeko(fp, off_t(positions[i]), SEEK_SET) == 0 &&
            fwrite(payloads[i], 1, std::strlen(payloads[i]), fp) == std::strlen(payloads[i]),
            "write a small payload at a large source position");
  require(fclose(fp) == 0 && GetFileSize(source) > off_t(GPTYPE(16)<<30), "sparse source exceeds 16 GiB");
  RECORD tail(source);
  tail.SetRecordStart(positions[2]); tail.SetRecordEnd(0);
  require(tail.GetLength() == off_t(std::strlen(payloads[2])), "to-EOF length subtracts the wide start");

  const STRING stem("patent-index");
  GPTYPE expectedNext = 0;
  {
    MDT table(stem, false);
    require(table.Ok(), "create the wide-offset document table");
    for (size_t i = 0; i < 3; ++i) {
      MDTREC record(&table);
      record.SetKey(STRING("patent-")+STRING(INT(i)));
      record.SetFullFileName(source);
      require(record.SetLocalRecordRange(positions[i], positions[i]+std::strlen(payloads[i])-1),
              "construct bounded patent metadata");
      // The source position and the logical index GP are separate domains.
      record.SetGlobalFileStart(expectedNext-positions[i]);
      require(table.AddEntry(record) == i+1, "index bounded records at large source offsets");
      FC range;
      require(table.LookupByGp(expectedNext, &range) == i+1 &&
              range.GetFieldStart() == expectedNext && range.GetLength() == std::strlen(payloads[i]),
              "GP lookup uses the bounded record range, not the source offset");
      expectedNext += std::strlen(payloads[i]);
    }
    require(table.GetNextGlobal() == expectedNext, "large source gaps do not inflate logical record lengths");
    MDTREC invalid(&table);
    invalid.SetKey("oversized");
    require(!invalid.SetLocalRecordRange(0, GPTYPE(100000000000000ULL)) &&
            table.AddEntry(invalid) == 0 && table.GetTotalEntries() == 3,
            "a whole container cannot enter the bounded MDT");
  }
  {
    MDT table(stem, false);
    require(table.Ok() && table.GetTotalEntries() == 3 && table.GetNextGlobal() == expectedNext,
            "reopen wide-start/span records and lookup caches");
    for (size_t i = 0; i < 3; ++i) {
      MDTREC metadata(&table);
      require(table.GetEntry(i+1, &metadata) && metadata.GetLocalRecordStart() == positions[i] &&
              metadata.GetLocalRecordEnd() == positions[i]+std::strlen(payloads[i])-1,
              "reopened metadata retains all high source-position bits");
      RESULT result(metadata);
      STRING retrieved;
      result.GetRecordData(&retrieved);
      require(retrieved == payloads[i], "retrieve the patent from its true large-file position");
      FILE *out = fopen("result.cache", "w+b");
      require(out != nullptr, "open result cache");
      result.Write(out); rewind(out);
      RESULT restored;
      require(restored.Read(out) && restored.GetRecordStart() == positions[i] &&
              restored.GetRecordEnd() == result.GetRecordEnd(), "result persistence retains wide source endpoints");
      fclose(out);
      restored.GetRecordData(&retrieved);
      require(retrieved == payloads[i], "retrieval survives result serialization");
      RSET results;
      results.AddEntry(restored);
      out = fopen("result-set.cache", "w+b");
      results.Write(out); rewind(out);
      RSET loaded;
      require(loaded.Read(out) && loaded.GetTotalEntries() == 1 &&
              loaded.GetEntry(1, &restored) && restored.GetRecordStart() == positions[i],
              "result-set persistence retains wide child offsets");
      fclose(out);
      const auto validSet = bytes("result-set.cache");
      auto badSet = validSet;
      badSet[0] = objRSET;
      write_bytes("result-set.cache", badSet);
      out = fopen("result-set.cache", "rb");
      require(!loaded.Read(out) && loaded.GetTotalEntries() == 0, "reject an old result-set format");
      fclose(out);
      badSet = validSet;
      badSet[5] = objRESULT;
      write_bytes("result-set.cache", badSet);
      out = fopen("result-set.cache", "rb");
      require(!loaded.Read(out) && loaded.GetTotalEntries() == 0,
              "result sets propagate an incompatible child format");
      fclose(out);
    }
  }
  const STRING mdt = stem+DbExtMdt;
  const auto valid = bytes(mdt);
  auto incompatible = valid;
  incompatible[3] = 'T'; // Old magic, with a deliberately identical row size/length.
  write_bytes(mdt, incompatible);
  {
    MDT rejected(stem, false);
    require(!rejected.Ok() && rejected.GetTotalEntries() == 0,
            "reject old layouts before mapping or rebuilding caches");
  }
  require(bytes(mdt) == incompatible, "opening an incompatible MDT must not rewrite it");
#endif
}

int main()
{
  if (sizeof(GPTYPE) < 8) {
    std::cout << "Large source-offset regression requires a 64-bit index build\n";
    return 0;
  }
  try {
    Scratch scratch;
    wide_documents();
    spans();
    sparse_file_and_mdt();
    std::cout << "Wide document ranges, bounded MDT spans, persistence and large-file retrieval passed; MDTREC="
              << sizeof(MDTREC) << " bytes\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
