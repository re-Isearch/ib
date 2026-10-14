// Fixed numerical columns: signed 128-bit ticks with 16 decimal places.

#include <array>
#include <limits>

#include "common.hxx"
#include "nlist.hxx"
#include "magic.hxx"

namespace {

enum : UCHR {
  NUMERIC_LAYOUT_RAW     = 0,
  NUMERIC_LAYOUT_INDEXED = 1
};

static constexpr UCHR NUMERIC_VERSION = 1;
static constexpr size_t NUMERIC_HEADER_SIZE = 16;


struct NUMERIC_HEADER
{
  UCHR  version;
  UCHR  layout;
  UCHR  flags;
  UINT4 record_size;
  UINT8 count;
};


bool ParseHeader(const BYTE *buf, size_t bytes, NUMERIC_HEADER *header)
{
  if (buf == NULL || header == NULL || bytes < NUMERIC_HEADER_SIZE)
    return false;

  if (buf[0] != (BYTE)objFIXEDNUMERICLIST)
    return false;

  header->version     = buf[1];
  header->layout      = buf[2];
  header->flags       = buf[3];
  header->record_size = getINT4(buf, 4);
  header->count       = getINT8(buf, 8);

  return header->version == NUMERIC_VERSION &&
         header->record_size == NUMERICFLD::DISK_SIZE &&
         header->flags == NUMERICOBJ::FRACTION_DIGITS;
}


bool ReadHeader(FILE *fp, NUMERIC_HEADER *header)
{
  if (fp == NULL || header == NULL)
    return false;

  std::array<BYTE, NUMERIC_HEADER_SIZE> buf;

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

  putObjID(objFIXEDNUMERICLIST, fp);

  if (::Write((UCHR)NUMERIC_VERSION, fp) != 1 ||
      ::Write(layout, fp) != 1 ||
      ::Write((UCHR)NUMERICOBJ::FRACTION_DIGITS, fp) != 1 ||
      ::Write((UINT4)NUMERICFLD::DISK_SIZE, fp) != 4 ||
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



bool NUMERIC_INDEX_TRAITS::LoadRawBlock(
    const STRING& FileName,
    std::vector<field_type> *result)
{
  if (result == NULL)
    return false;

  result->clear();

  FILE *fp = fopen(FileName, "rb");
  if (fp == NULL)
    return false;

  NUMERIC_HEADER header;

  if (!ReadHeader(fp, &header) ||
      header.layout != NUMERIC_LAYOUT_RAW)
  {
    fclose(fp);
    return false;
  }

  const off_t file_size = GetFileSize(FileName);

  if (file_size < (off_t)NUMERIC_HEADER_SIZE)
  {
    fclose(fp);
    return false;
  }

  const off_t bytes = file_size - (off_t)NUMERIC_HEADER_SIZE;

  const size_t count = (size_t)(bytes / NUMERICFLD::DISK_SIZE);

  const size_t remainder = (size_t)(bytes % NUMERICFLD::DISK_SIZE);


  if (remainder != 0)
  {
    message_log(LOG_ERROR,
                "Truncated NUMERIC raw index '%s' (%lu trailing byte(s))",
                FileName.c_str(),
                (unsigned long)remainder);
    fclose(fp);
    return false;
  }

  result->reserve(count);

  if (fseek(fp, NUMERIC_HEADER_SIZE, SEEK_SET) != 0)
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



bool NUMERIC_INDEX_TRAITS::LoadValueBlock(
    const STRING& FileName,
    std::vector<field_type> *result)
{
  if (result == NULL)
    return false;

  result->clear();

  FILE *fp = fopen(FileName, "rb");
  if (fp == NULL)
    return false;

  NUMERIC_HEADER header;

  if (!ReadHeader(fp, &header) ||
      header.layout != NUMERIC_LAYOUT_INDEXED)
  {
    fclose(fp);
    return false;
  }

  const UINT8 count = header.count;
  if (count > (UINT8(std::numeric_limits<off_t>::max()) - NUMERIC_HEADER_SIZE) /
              (2 * NUMERICFLD::DISK_SIZE) ||
      count > std::numeric_limits<size_t>::max() / NUMERICFLD::DISK_SIZE)
    { fclose(fp); return false; }

  const off_t expected =
      (off_t)NUMERIC_HEADER_SIZE +
      (off_t)(2 * count * NUMERICFLD::DISK_SIZE);

  if (GetFileSize(FileName) != expected)
  {
    message_log(LOG_ERROR,
                "Truncated NUMERIC index '%s'",
                FileName.c_str());

    fclose(fp);
    return false;
  }

  result->reserve((size_t)count);

  if (fseek(fp, NUMERIC_HEADER_SIZE, SEEK_SET) != 0)
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



bool NUMERIC_INDEX_TRAITS::MapIndexed(
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

  NUMERIC_HEADER header;

  if (!ParseHeader(base, bytes, &header) ||
      header.layout != NUMERIC_LAYOUT_INDEXED)
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
      (max_size - NUMERIC_HEADER_SIZE) /
      (2 * NUMERICFLD::DISK_SIZE))
    {
      Sessions.Invalidate(FileName);
      return false;
    }

  const size_t block_bytes = count * NUMERICFLD::DISK_SIZE;
  const size_t expected =
      NUMERIC_HEADER_SIZE + 2 * block_bytes;

  if (bytes != expected)
    {
      message_log(LOG_ERROR,
                  "Truncated NUMERIC index '%s'",
                  FileName.c_str());

      Sessions.Invalidate(FileName);
      return false;
    }

  view->value_block = base + NUMERIC_HEADER_SIZE;
  view->gp_block    = view->value_block + block_bytes;
  view->count       = count;

  return true;
}


bool NUMERIC_INDEX_TRAITS::WriteIndex(
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

  if (!WriteHeader(fp, NUMERIC_LAYOUT_INDEXED, count))
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



FILE *NUMERIC_INDEX_TRAITS::OpenForAppend(const STRING& FileName)
{
  //
  // Brand new field.
  //
  if (GetFileSize(FileName) == 0)
  {
    FILE *fp = fopen(FileName, "w+b");

    if (fp == NULL)
      return NULL;

    if (!WriteHeader(fp, NUMERIC_LAYOUT_RAW, 0))
    {
      fclose(fp);
      return NULL;
    }

    return fp; // positioned immediately after header
  }


  FILE *in = fopen(FileName, "rb");
  if (in == NULL)
    return NULL;

  NUMERIC_HEADER header;

  if (!ReadHeader(in, &header))
  {
    message_log(LOG_ERROR,
                "'%s' is not a fixed-point NUMERIC index; reindex required",
                FileName.c_str());

    fclose(in);
    return NULL;
  }


  //
  // Already a raw append stream.
  //
  if (header.layout == NUMERIC_LAYOUT_RAW)
  {
    fclose(in);

    const off_t bytes =
        GetFileSize(FileName) - (off_t)NUMERIC_HEADER_SIZE;

    if (bytes < 0 ||
        (bytes % NUMERICFLD::DISK_SIZE) != 0)
    {
      message_log(LOG_ERROR,
                  "NUMERIC append file '%s' has an incomplete record",
                  FileName.c_str());

      return NULL;
    }

    return fopen(FileName, "a+b");
  }


  if (header.layout != NUMERIC_LAYOUT_INDEXED)
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

  if (count > (UINT8(std::numeric_limits<off_t>::max()) - NUMERIC_HEADER_SIZE) /
              (2 * NUMERICFLD::DISK_SIZE) ||
      GetFileSize(FileName) != off_t(NUMERIC_HEADER_SIZE) + off_t(2)*count*NUMERICFLD::DISK_SIZE)
    { fclose(in); return NULL; }

  const off_t gp_offset =
      (off_t)NUMERIC_HEADER_SIZE +
      (off_t)(count * NUMERICFLD::DISK_SIZE);

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

  if (!WriteHeader(out, NUMERIC_LAYOUT_RAW, 0))
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


size_t NUMERICLIST::LoadTable(INT4 start, INT4 end, NumBlock block)
{
  Clear();
  std::vector<NUMERICFLD> rows;
  if (block == NONE)
    {
      if (!NUMERIC_INDEX_TRAITS::LoadRawBlock(FileName, &rows)) return 0;
    }
  else if (block == VAL_BLOCK)
    {
      if (!NUMERIC_INDEX_TRAITS::LoadValueBlock(FileName, &rows)) return 0;
    }
  else
    {
      MultiMMapSession maps;
      NUMERIC_INDEX_TRAITS::mapped_type view;
      if (!NUMERIC_INDEX_TRAITS::MapIndexed(FileName, maps, &view)) return 0;
      rows.reserve(view.count);
      for (size_t i = 0; i < view.count; ++i)
        rows.emplace_back(NUMERIC_INDEX_TRAITS::MappedGlobalStartAt(view.gp_block, i),
                          NUMERICOBJ::FromTicks(NUMERIC_INDEX_TRAITS::MappedValueAt(view.gp_block, i)));
    }
  const size_t first = start < 0 ? 0 : size_t(start);
  const size_t last = end < 0 ? rows.size() : std::min(rows.size(), size_t(end)+1);
  if (first >= last) return 0;
  Reserve(last-first);
  for (size_t i = first; i < last; ++i) Add(rows[i]);
  return GetCount();
}

SearchState NUMERICLIST::FindIndexes(const STRING& name, const NUMERICOBJ& key,
                                    ZRelation_t relation, INT4 *first, INT4 *last)
{
  FileName = name;
  if (first) *first = -1;
  if (last) *last = -1;
  if (!key.Ok() || !LoadValueBlock(name)) return NO_MATCH;
  const auto range = base_type::Find(key.GetTicks(), relation);
  if (range.empty() || range.last-1 > size_t(std::numeric_limits<INT4>::max())) return NO_MATCH;
  StartIndex = range.first; EndIndex = range.last; Relation = relation;
  if (first) *first = INT4(range.first);
  if (last) *last = INT4(range.last-1);
  return MATCH;
}

SearchState NUMERICLIST::Find(const STRING& name, const NUMERICOBJ& key,
                             ZRelation_t relation, INT4 *index)
{
  FileName = name;
  if (!LoadValueBlock(name)) return NO_MATCH;
  return Find(key, relation, index);
}
SearchState NUMERICLIST::Find(const NUMERICOBJ& key, ZRelation_t relation, INT4 *index)
{
  if (index) *index = -1;
  if (!key.Ok()) return NO_MATCH;
  const auto range = base_type::Find(key.GetTicks(), relation);
  if (range.empty()) return NO_MATCH;
  const size_t found = relation == ZRelLT || relation == ZRelLE ? range.last-1 : range.first;
  if (found > size_t(std::numeric_limits<INT4>::max())) return NO_MATCH;
  if (index) *index = INT4(found);
  return MATCH;
}
SearchState NUMERICLIST::Find(const STRING& name, GPTYPE key, ZRelation_t relation, INT4 *index)
{
  FileName = name;
  LoadTable(GP_BLOCK);
  return Find(key, relation, index);
}
SearchState NUMERICLIST::Find(GPTYPE key, ZRelation_t relation, INT4 *index)
{
  if (index) *index = -1;
  const auto lower = std::lower_bound(begin(), end(), key,
    [](const NUMERICFLD& field, GPTYPE value) { return field.GetGlobalStart() < value; });
  const auto upper = std::upper_bound(begin(), end(), key,
    [](GPTYPE value, const NUMERICFLD& field) { return value < field.GetGlobalStart(); });
  size_t found = GetCount();
  switch (relation) {
    case ZRelEQ: if (lower != upper) found = size_t(lower-begin()); break;
    case ZRelGE: found = size_t(lower-begin()); break;
    case ZRelGT: found = size_t(upper-begin()); break;
    case ZRelLT: if (lower != begin()) found = size_t(lower-begin()-1); break;
    case ZRelLE: if (upper != begin()) found = size_t(upper-begin()-1); break;
    default: break;
  }
  if (found >= GetCount() || found > size_t(std::numeric_limits<INT4>::max())) return NO_MATCH;
  if (index) *index = INT4(found);
  return MATCH;
}
void NUMERICLIST::Dump(ostream& os) const { for (const auto& field : *this) field.Dump(os); }
void NUMERICLIST::Dump(INT4 first, INT4 last, ostream& os) const {
  for (INT4 i = std::max(INT4(0), first); i <= last && size_t(i) < GetCount(); ++i) (*this)[size_t(i)].Dump(os);
}
