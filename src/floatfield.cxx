#include "floatfield.hxx"
#include <array>
#include <charconv>
#include <cctype>
#include <cstring>

bool float64::Parse(const STRING& text, DOUBLE *value)
{
  if (value) *value = 0;
  const char *first = text.c_str(), *last = first + text.GetLength();
  while (first != last && std::isspace((unsigned char)*first)) ++first;
  while (last != first && std::isspace((unsigned char)last[-1])) --last;
  if (first == last) return false;
  if (*first == '+') {
    if (++first == last || *first == '+' || *first == '-') return false;
  }
  DOUBLE parsed;
  const auto result = std::from_chars(first, last, parsed, std::chars_format::general);
  if (result.ec != std::errc() || result.ptr != last || !std::isfinite(parsed)) return false;
  if (value) *value = parsed;
  return true;
}

STRING float64::ToString(DOUBLE value)
{
  if (!std::isfinite(value)) return STRING();
  if (value == 0) return STRING("0");
  char text[64];
  const auto result = std::to_chars(text, text+sizeof(text), value, std::chars_format::general);
  return result.ec == std::errc() ? STRING(text, size_t(result.ptr-text)) : STRING();
}

DOUBLE FLOATFLD::DecodeValue(const BYTE *p)
{
  const UINT8 bits = getINT8(p, 1+sizeof(GPTYPE));
  DOUBLE value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

bool FLOATFLD::Write(FILE *fp) const
{
  if (!fp || !std::isfinite(Value)) return false;
  UINT8 bits;
  std::memcpy(&bits, &Value, sizeof(bits));
  return fputc(objFLOAT64FLD, fp) != EOF &&
         ::Write(GlobalStart, fp) == int(sizeof(GPTYPE)) && ::Write(bits, fp) == 8;
}

bool FLOATFLD::Write(FILE *fp, GPTYPE offset) const
{
  FLOATFLD field(*this);
  field.IncrementStart(offset);
  return field.Write(fp);
}

bool FLOATFLD::Read(FILE *fp)
{
  Value = std::numeric_limits<DOUBLE>::quiet_NaN();
  std::array<BYTE, DISK_SIZE> bytes;
  if (!fp || fread(bytes.data(), 1, bytes.size(), fp) != bytes.size() ||
      bytes[0] != objFLOAT64FLD) return false;
  const DOUBLE value = DecodeValue(bytes.data());
  if (!std::isfinite(value)) return false;
  GlobalStart = DecodeGlobalStart(bytes.data());
  Value = value;
  return true;
}
