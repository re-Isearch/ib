// integerlist.cxx

#include <array>
#include <limits>

#include "common.hxx"
#include "integerlist.hxx"
#include "magic.hxx"

namespace {

enum : UCHR {
  INTEGER_LAYOUT_RAW     = 0,
  INTEGER_LAYOUT_INDEXED = 1
};

static constexpr UCHR INTEGER_VERSION = 2;
static constexpr size_t INTEGER_HEADER_SIZE = 16;


struct INTEGER_HEADER
{
  UCHR  version;
  UCHR  layout;
  UCHR  flags;
  UINT4 record_size;
  UINT8 count;
};


bool ParseHeader(const BYTE *buf, size_t bytes, INTEGER_HEADER *header)
{
  if (buf == NULL || header == NULL || bytes < INTEGER_HEADER_SIZE)
    return false;

  if (buf[0] != (BYTE)objINTEGERLIST)
    return false;

  header->version     = buf[1];
  header->layout      = buf[2];
  header->flags       = buf[3];
  header->record_size = getINT4(buf, 4);
  header->count       = getINT8(buf, 8);

  return header->version == INTEGER_VERSION &&
         header->record_size == INTEGERFLD::DISK_SIZE;
}


bool ReadHeader(FILE *fp, INTEGER_HEADER *header)
{
  if (fp == NULL || header == NULL)
    return false;

  std::array<BYTE, INTEGER_HEADER_SIZE> buf;

  if (fseek(fp, 0, SEEK_SET) != 0)
    return false;

  if (fread(buf.data(), 1, buf.size(), fp) != buf.size())
    return false;

  return ParseHeader(buf.data(), buf.size(), header);
}


bool WriteHeader(FILE *fp, UCHR layout, UINT8 count)
{
  if (fp == NULL)
    return false;

  putObjID(objINTEGERLIST, fp);

  if (::Write((UCHR)INTEGER_VERSION, fp) != 1 ||
      ::Write(layout, fp) != 1 ||
      ::Write((UCHR)0, fp) != 1 ||
      ::Write((UINT4)INTEGERFLD::DISK_SIZE, fp) != 4 ||
      ::Write(count, fp) != 8)
    return false;

  return true;
}


STRING TempName(const STRING& fn)
{
  STRING tmp = fn + "~";

  for (size_t i = 0; FileExists(tmp); ++i)
    tmp.form("%s~.%lu", fn.c_str(), (unsigned long)i);

  return tmp;
}


bool ReplaceFile(const STRING& tmp, const STRING& fn)
{
  //
  // POSIX rename() replaces atomically. RenameFile() should therefore
  // normally succeed directly on our target platforms.
  //
  if (RenameFile(tmp, fn) == 0)
    return true;

  message_log(LOG_ERRNO,
              "Could not replace '%s' with '%s'",
              fn.c_str(), tmp.c_str());

  return false;
}

} // namespace



bool INTEGER_INDEX_TRAITS::LoadRawBlock(
    const STRING& FileName,
    std::vector<field_type> *result)
{
  if (result == NULL)
    return false;

  result->clear();

  FILE *fp = fopen(FileName, "rb");
  if (fp == NULL)
    return false;

  INTEGER_HEADER header;

  if (!ReadHeader(fp, &header) ||
      header.layout != INTEGER_LAYOUT_RAW)
  {
    fclose(fp);
    return false;
  }

  const off_t file_size = GetFileSize(FileName);

  if (file_size < (off_t)INTEGER_HEADER_SIZE)
  {
    fclose(fp);
    return false;
  }

  const off_t bytes = file_size - (off_t)INTEGER_HEADER_SIZE;

  const size_t count = (size_t)(bytes / INTEGERFLD::DISK_SIZE);

  const size_t remainder = (size_t)(bytes % INTEGERFLD::DISK_SIZE);


#if 0 /* DEBUG */

if (remainder != 0)
{
  const off_t tail =
      (off_t)INTEGER_HEADER_SIZE +
      (off_t)(count * INTEGERFLD::DISK_SIZE);

  std::array<BYTE, INTEGER_HEADER_SIZE> extra {};

  if (remainder == INTEGER_HEADER_SIZE &&
      fseek(fp, tail, SEEK_SET) == 0 &&
      fread(extra.data(), 1, extra.size(), fp) == extra.size())
  {
    message_log(LOG_WARN,
        "INTEGER tail: "
        "%02x %02x %02x %02x "
        "%02x %02x %02x %02x "
        "%02x %02x %02x %02x "
        "%02x %02x %02x %02x",
        extra[0], extra[1], extra[2], extra[3],
        extra[4], extra[5], extra[6], extra[7],
        extra[8], extra[9], extra[10], extra[11],
        extra[12], extra[13], extra[14], extra[15]);
  }

  message_log(LOG_WARN,
      "INTEGER raw index '%s': size=%lld, records=%lu, "
      "record_size=%lu, trailing=%lu",
      FileName.c_str(),
      (long long)file_size,
      (unsigned long)count,
      (unsigned long)INTEGERFLD::DISK_SIZE,
      (unsigned long)remainder);
}

#else
  if (remainder != 0)
  {
    message_log(LOG_WARN,
                "INTEGER raw index '%s' has %lu trailing byte(s); ignoring them",
                FileName.c_str(),
                (unsigned long)remainder);
  }
#endif

  result->reserve(count);

  if (fseek(fp, INTEGER_HEADER_SIZE, SEEK_SET) != 0)
  {
    fclose(fp);
    return false;
  }

  for (size_t i = 0; i < count; ++i)
  {
    field_type field;

    if (!field.Read(fp))
    {
      fclose(fp);
      result->clear();
      return false;
    }

    result->push_back(field);
  }

  fclose(fp);
  return true;
}



bool INTEGER_INDEX_TRAITS::LoadValueBlock(
    const STRING& FileName,
    std::vector<field_type> *result)
{
  if (result == NULL)
    return false;

  result->clear();

  FILE *fp = fopen(FileName, "rb");
  if (fp == NULL)
    return false;

  INTEGER_HEADER header;

  if (!ReadHeader(fp, &header) ||
      header.layout != INTEGER_LAYOUT_INDEXED)
  {
    fclose(fp);
    return false;
  }

  const UINT8 count = header.count;

  const off_t expected =
      (off_t)INTEGER_HEADER_SIZE +
      (off_t)(2 * count * INTEGERFLD::DISK_SIZE);

  if (GetFileSize(FileName) < expected)
  {
    message_log(LOG_ERROR,
                "Truncated INTEGER index '%s'",
                FileName.c_str());

    fclose(fp);
    return false;
  }

  result->reserve((size_t)count);

  if (fseek(fp, INTEGER_HEADER_SIZE, SEEK_SET) != 0)
  {
    fclose(fp);
    return false;
  }

  for (UINT8 i = 0; i < count; ++i)
  {
    field_type field;

    if (!field.Read(fp))
    {
      fclose(fp);
      result->clear();
      return false;
    }

    result->push_back(field);
  }

  fclose(fp);
  return true;
}



bool INTEGER_INDEX_TRAITS::MapIndexed(
    const STRING& FileName,
    MultiMMapSession& Sessions,
    mapped_type *view)
{
  if (view == NULL)
    return false;

  *view = mapped_type{};

  //
  // Deliberately MapNormal.  Query selectivity is unknown here: equality
  // touches only a few pages, while inequalities may consume a substantial
  // contiguous slice.  Let the VM use its normal heuristics.
  //
  const unsigned char *base =
      Sessions.GetMemoryBase(FileName, MapNormal);

  if (base == NULL)
    return false;

  const size_t bytes = Sessions.Size(FileName);

  INTEGER_HEADER header;

  if (!ParseHeader(base, bytes, &header) ||
      header.layout != INTEGER_LAYOUT_INDEXED)
    {
      Sessions.Invalidate(FileName);
      return false;
    }

  const size_t count = static_cast<size_t>(header.count);

  if (static_cast<UINT8>(count) != header.count)
    {
      Sessions.Invalidate(FileName);
      return false;
    }

  const size_t max_size = std::numeric_limits<size_t>::max();

  if (count >
      (max_size - INTEGER_HEADER_SIZE) /
      (2 * INTEGERFLD::DISK_SIZE))
    {
      Sessions.Invalidate(FileName);
      return false;
    }

  const size_t block_bytes = count * INTEGERFLD::DISK_SIZE;
  const size_t expected =
      INTEGER_HEADER_SIZE + 2 * block_bytes;

  if (bytes < expected)
    {
      message_log(LOG_ERROR,
                  "Truncated INTEGER index '%s'",
                  FileName.c_str());

      Sessions.Invalidate(FileName);
      return false;
    }

  view->value_block = base + INTEGER_HEADER_SIZE;
  view->gp_block    = view->value_block + block_bytes;
  view->count       = count;

  return true;
}


bool INTEGER_INDEX_TRAITS::WriteIndex(
    const STRING& FileName,
    const std::vector<field_type>& valueBlock,
    const std::vector<field_type>& gpBlock)
{
  if (valueBlock.size() != gpBlock.size())
    return false;

  const STRING tmp = TempName(FileName);

  FILE *fp = fopen(tmp, "wb");
  if (fp == NULL)
    return false;

  const UINT8 count = (UINT8)valueBlock.size();

  if (!WriteHeader(fp, INTEGER_LAYOUT_INDEXED, count))
  {
    fclose(fp);
    UnlinkFile(tmp);
    return false;
  }

  for (const auto& field : valueBlock)
  {
    if (!field.Write(fp))
    {
      fclose(fp);
      UnlinkFile(tmp);
      return false;
    }
  }

  for (const auto& field : gpBlock)
  {
    if (!field.Write(fp))
    {
      fclose(fp);
      UnlinkFile(tmp);
      return false;
    }
  }

  if (fclose(fp) != 0)
  {
    UnlinkFile(tmp);
    return false;
  }

  return ReplaceFile(tmp, FileName);
}



FILE *INTEGER_INDEX_TRAITS::OpenForAppend(const STRING& FileName)
{
  //
  // Brand new field.
  //
  if (GetFileSize(FileName) == 0)
  {
    FILE *fp = fopen(FileName, "w+b");

    if (fp == NULL)
      return NULL;

    if (!WriteHeader(fp, INTEGER_LAYOUT_RAW, 0))
    {
      fclose(fp);
      return NULL;
    }

    return fp; // positioned immediately after header
  }


  FILE *in = fopen(FileName, "rb");
  if (in == NULL)
    return NULL;

  INTEGER_HEADER header;

  if (!ReadHeader(in, &header))
  {
    message_log(LOG_ERROR,
                "'%s' is not an INTEGER index",
                FileName.c_str());

    fclose(in);
    return NULL;
  }


  //
  // Already a raw append stream.
  //
  if (header.layout == INTEGER_LAYOUT_RAW)
  {
    fclose(in);

    const off_t bytes =
        GetFileSize(FileName) - (off_t)INTEGER_HEADER_SIZE;

    if (bytes < 0 ||
        (bytes % INTEGERFLD::DISK_SIZE) != 0)
    {
      message_log(LOG_ERROR,
                  "INTEGER append file '%s' has an incomplete record",
                  FileName.c_str());

      return NULL;
    }

    return fopen(FileName, "a+b");
  }


  if (header.layout != INTEGER_LAYOUT_INDEXED)
  {
    fclose(in);
    return NULL;
  }


  //
  // Convert the optimized representation back into a raw GP-ordered
  // append stream.
  //
  // IMPORTANT: copy the GP block, not the value block.
  //
  const UINT8 count = header.count;

  const off_t gp_offset =
      (off_t)INTEGER_HEADER_SIZE +
      (off_t)(count * INTEGERFLD::DISK_SIZE);

  if (fseek(in, gp_offset, SEEK_SET) != 0)
  {
    fclose(in);
    return NULL;
  }

  const STRING tmp = TempName(FileName);

  FILE *out = fopen(tmp, "wb");
  if (out == NULL)
  {
    fclose(in);
    return NULL;
  }

  if (!WriteHeader(out, INTEGER_LAYOUT_RAW, 0))
  {
    fclose(in);
    fclose(out);
    UnlinkFile(tmp);
    return NULL;
  }

  for (UINT8 i = 0; i < count; ++i)
  {
    field_type field;

    if (!field.Read(in) || !field.Write(out))
    {
      fclose(in);
      fclose(out);
      UnlinkFile(tmp);
      return NULL;
    }
  }

  fclose(in);

  if (fclose(out) != 0)
  {
    UnlinkFile(tmp);
    return NULL;
  }

  if (!ReplaceFile(tmp, FileName))
  {
    UnlinkFile(tmp);
    return NULL;
  }

  return fopen(FileName, "a+b");
}
