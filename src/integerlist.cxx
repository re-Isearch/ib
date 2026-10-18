// Integer columns retain their full signed 128-bit domain. Indexed columns
// use four-byte values when every value fits a signed or unsigned 32-bit word.
#include <array>
#include <limits>

#include "common.hxx"
#include "integerlist.hxx"
#include "magic.hxx"

namespace {

enum : UCHR { RAW = 0, INDEXED = 1, WIDE_VERSION = 2, COMPACT_VERSION = 3,
              UNSIGNED_VALUES = 1 };
constexpr size_t HEADER_SIZE = 16;
static_assert(sizeof(INT4) == 4 && sizeof(UINT4) == 4, "32-bit integer words required");

struct Header {
  UCHR version = 0, layout = 0, flags = 0;
  UINT4 row_size = 0;
  UINT8 count = 0;
  bool compact() const { return version == COMPACT_VERSION; }
  bool unsigned_values() const { return (flags & UNSIGNED_VALUES) != 0; }
};

bool ParseHeader(const BYTE *buf, size_t bytes, Header *header)
{
  if (!buf || !header || bytes < HEADER_SIZE || buf[0] != objINTEGERLIST)
    return false;
  header->version = buf[1];
  header->layout = buf[2];
  header->flags = buf[3];
  header->row_size = getINT4(buf, 4);
  header->count = getINT8(buf, 8);
  if (header->layout != RAW && header->layout != INDEXED) return false;
  if (header->version == WIDE_VERSION) {
    if (header->flags != 0 || header->row_size != INTEGERFLD::DISK_SIZE) return false;
  } else if (header->version == COMPACT_VERSION) {
    if (header->layout != INDEXED || header->flags > UNSIGNED_VALUES ||
        header->row_size != INTEGERFLD::COMPACT_DISK_SIZE) return false;
  } else return false;
  return header->layout != RAW || header->count == 0;
}

// Divide the actual length before comparing the count, so malformed counts
// cannot overflow a multiplication or produce a partially mapped block.
bool ValidateSize(const Header& header, size_t bytes, size_t *count)
{
  if (bytes < HEADER_SIZE) return false;
  const size_t divisor = header.row_size * (header.layout == INDEXED ? 2 : 1);
  const size_t payload = bytes - HEADER_SIZE;
  if (payload % divisor != 0) return false;
  *count = payload / divisor;
  return header.layout == RAW || UINT8(*count) == header.count;
}

bool ReadHeader(FILE *fp, Header *header, size_t *count)
{
  std::array<BYTE, HEADER_SIZE> bytes;
  if (fseek(fp, 0, SEEK_SET) != 0 ||
      fread(bytes.data(), 1, bytes.size(), fp) != bytes.size() ||
      !ParseHeader(bytes.data(), bytes.size(), header)) return false;
  const off_t size = GetFileSize(fp);
  if (size < 0 || UINT16(size) > std::numeric_limits<size_t>::max()) return false;
  return ValidateSize(*header, size_t(size), count);
}

bool WriteHeader(FILE *fp, UCHR layout, UINT8 count,
                 bool compact = false, bool unsignedValues = false)
{
  putObjID(objINTEGERLIST, fp);
  return ::Write(UCHR(compact ? COMPACT_VERSION : WIDE_VERSION), fp) == 1 &&
         ::Write(layout, fp) == 1 &&
         ::Write(UCHR(unsignedValues ? UNSIGNED_VALUES : 0), fp) == 1 &&
         ::Write(UINT4(compact ? INTEGERFLD::COMPACT_DISK_SIZE : INTEGERFLD::DISK_SIZE), fp) == 4 &&
         ::Write(count, fp) == 8 && !ferror(fp);
}

bool ReadRow(FILE *fp, const Header& header, INTEGERFLD *field)
{
  return header.compact() ? field->ReadCompact(fp, header.unsigned_values()) : field->Read(fp);
}

bool Seek(FILE *fp, size_t offset)
{
  return offset <= size_t(std::numeric_limits<long>::max()) &&
         fseek(fp, long(offset), SEEK_SET) == 0;
}

STRING TempName(const STRING& filename)
{
  STRING tmp = filename + "~";
  for (size_t i = 0; FileExists(tmp); ++i)
    tmp.form("%s~.%lu", filename.c_str(), (unsigned long)i);
  return tmp;
}

bool Finish(FILE *fp, const STRING& tmp, const STRING& filename, bool ok)
{
  if (fclose(fp) != 0) ok = false;
  if (ok && RenameFile(tmp, filename) == 0) return true;
  if (ok) message_log(LOG_ERRNO, "Could not replace INTEGER column '%s'", filename.c_str());
  UnlinkFile(tmp);
  return false;
}

bool LoadBlock(const STRING& filename, std::vector<INTEGERFLD> *fields,
               UCHR layout, bool gpOrder)
{
  if (!fields) return false;
  fields->clear();
  FILE *fp = fopen(filename, "rb");
  if (!fp) return false;
  Header header;
  size_t count = 0;
  if (!ReadHeader(fp, &header, &count) || header.layout != layout ||
      !Seek(fp, HEADER_SIZE + (gpOrder && layout == INDEXED ? count * header.row_size : 0))) {
    fclose(fp);
    return false;
  }
  fields->reserve(count);
  for (size_t i = 0; i < count; ++i) {
    INTEGERFLD field;
    if (!ReadRow(fp, header, &field)) {
      fclose(fp);
      fields->clear();
      return false;
    }
    fields->push_back(field);
  }
  fclose(fp);
  return true;
}

} // namespace

bool INTEGER_INDEX_TRAITS::LoadRawBlock(const STRING& filename,
                                        std::vector<field_type> *fields)
{
  return LoadBlock(filename, fields, RAW, false);
}

bool INTEGER_INDEX_TRAITS::LoadValueBlock(const STRING& filename,
                                          std::vector<field_type> *fields)
{
  return LoadBlock(filename, fields, INDEXED, false);
}

bool INTEGER_INDEX_TRAITS::LoadGpBlock(const STRING& filename,
                                       std::vector<field_type> *fields)
{
  return LoadBlock(filename, fields, INDEXED, true);
}

bool INTEGER_INDEX_TRAITS::IsIndexed(const STRING& filename)
{
  FILE *fp = fopen(filename, "rb");
  if (!fp) return false;
  Header header;
  size_t count = 0;
  const bool ok = ReadHeader(fp, &header, &count) && header.layout == INDEXED;
  fclose(fp);
  return ok;
}

bool INTEGER_INDEX_TRAITS::MapIndexed(const STRING& filename,
                                      MultiMMapSession& sessions, mapped_type *view)
{
  if (!view) return false;
  *view = mapped_type{};
  const BYTE *base = sessions.GetMemoryBase(filename, MapNormal);
  if (!base) return false;
  Header header;
  size_t count = 0;
  if (!ParseHeader(base, sessions.Size(filename), &header) || header.layout != INDEXED ||
      !ValidateSize(header, sessions.Size(filename), &count)) {
    sessions.Invalidate(filename);
    return false;
  }
  view->value_block = {base + HEADER_SIZE, header.row_size, header.unsigned_values()};
  view->gp_block = {base + HEADER_SIZE + count * header.row_size,
                    header.row_size, header.unsigned_values()};
  view->count = count;
  return true;
}

bool INTEGER_INDEX_TRAITS::WriteIndex(const STRING& filename,
    const std::vector<field_type>& valueBlock, const std::vector<field_type>& gpBlock)
{
  if (valueBlock.size() != gpBlock.size()) return false;
  bool signed32 = true, unsigned32 = true;
  for (const auto *block : {&valueBlock, &gpBlock}) {
    for (const auto& field : *block) {
      signed32 &= INTEGERFLD::FitsSigned32(field.GetValue());
      unsigned32 &= INTEGERFLD::FitsUnsigned32(field.GetValue());
    }
  }
  const bool compact = signed32 || unsigned32;
  const bool unsignedValues = !signed32 && unsigned32;
  const STRING tmp = TempName(filename);
  FILE *fp = fopen(tmp, "wb");
  if (!fp) return false;
  bool ok = WriteHeader(fp, INDEXED, UINT8(valueBlock.size()), compact, unsignedValues);
  for (const auto *block : {&valueBlock, &gpBlock}) {
    for (const auto& field : *block) {
      if (!ok) break;
      ok = compact ? field.WriteCompact(fp, unsignedValues) : field.Write(fp);
    }
  }
  return Finish(fp, tmp, filename, ok);
}

FILE *INTEGER_INDEX_TRAITS::OpenForAppend(const STRING& filename)
{
  // Raw streams always accept the full integer domain. Finalization picks
  // a compact encoding from the actual values, so no incoming key can wrap.
  if (GetFileSize(filename) == 0) {
    FILE *fp = fopen(filename, "w+b");
    if (!fp) return NULL;
    if (!WriteHeader(fp, RAW, 0)) { fclose(fp); return NULL; }
    return fp;
  }

  FILE *in = fopen(filename, "rb");
  if (!in) return NULL;
  Header header;
  size_t count = 0;
  if (!ReadHeader(in, &header, &count)) {
    message_log(LOG_ERROR, "Invalid or incomplete INTEGER column '%s'", filename.c_str());
    fclose(in);
    return NULL;
  }
  if (header.layout == RAW) {
    fclose(in);
    return fopen(filename, "a+b");
  }

  // Expand the GP block, preserving field spans and signedness. Appending
  // to a 32-bit column and importing into a 128-bit column use the same rows.
  if (!Seek(in, HEADER_SIZE + count * header.row_size)) { fclose(in); return NULL; }
  const STRING tmp = TempName(filename);
  FILE *out = fopen(tmp, "wb");
  if (!out) { fclose(in); return NULL; }
  bool ok = WriteHeader(out, RAW, 0);
  for (size_t i = 0; i < count && ok; ++i) {
    field_type field;
    ok = ReadRow(in, header, &field) && field.Write(out);
  }
  fclose(in);
  if (!Finish(out, tmp, filename, ok)) return NULL;
  return fopen(filename, "a+b");
}
