#include "floatfield.hxx"
#include <array>
#include <charconv>
#include <cctype>
#include <cstring>

// CMake probes the floating overloads using the deployment target. A libc++
// header may declare them even when the target OS does not supply the symbols.
#ifndef IB_HAVE_FLOAT_FROM_CHARS
# if defined(_LIBCPP_VERSION)
#  define IB_HAVE_FLOAT_FROM_CHARS 0
# else
#  define IB_HAVE_FLOAT_FROM_CHARS 1
# endif
#endif
#ifndef IB_HAVE_FLOAT_TO_CHARS
# if defined(_LIBCPP_VERSION)
#  define IB_HAVE_FLOAT_TO_CHARS 0
# else
#  define IB_HAVE_FLOAT_TO_CHARS 1
# endif
#endif

#if !IB_HAVE_FLOAT_FROM_CHARS
# include <cerrno>
# include <cstdlib>
# include <locale.h>
# if __has_include(<xlocale.h>)
#  include <xlocale.h>
# endif
# include <string>

namespace {
struct NumericLocale {
  locale_t value = newlocale(LC_ALL_MASK, "C", nullptr);
  ~NumericLocale() { if (value) freelocale(value); }
};

bool DecimalSyntax(const char *first, const char *last, bool *nonzero)
{
  if (first != last && *first == '-') ++first;
  bool digits = false;
  const auto mantissa = [&]() {
    while (first != last && *first >= '0' && *first <= '9') {
      digits = true;
      *nonzero |= *first++ != '0';
    }
  };
  mantissa();
  if (first != last && *first == '.') { ++first; mantissa(); }
  if (!digits) return false;
  if (first != last && (*first == 'e' || *first == 'E')) {
    ++first;
    if (first != last && (*first == '+' || *first == '-')) ++first;
    const char *exponent = first;
    while (first != last && *first >= '0' && *first <= '9') ++first;
    if (first == exponent) return false;
  }
  return first == last;
}

bool ParseDecimal(const char *first, const char *last, DOUBLE *value)
{
  bool nonzero = false;
  // strtod_l also accepts hex, infinity and NaN; accept decimal syntax only.
  if (!DecimalSyntax(first, last, &nonzero)) return false;
  static const NumericLocale locale;
  if (!locale.value) return false;
  const std::string text(first, last);
  char *end;
  const int saved_errno = errno;
  errno = 0;
  const DOUBLE parsed = strtod_l(text.c_str(), &end, locale.value);
  const int error = errno;
  errno = saved_errno;
  if (end != text.c_str()+text.size() || !std::isfinite(parsed) ||
      (parsed == 0 && nonzero) ||
      (error && error != ERANGE) ||
      (error == ERANGE && std::fabs(parsed) > std::numeric_limits<DOUBLE>::min()))
    return false;
  // A representable subnormal can set ERANGE; retain it rather than dropping it.
  *value = parsed;
  return true;
}
}
#endif

#if !IB_HAVE_FLOAT_TO_CHARS
# include <locale>
# include <sstream>
#endif

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
#if IB_HAVE_FLOAT_FROM_CHARS
  const auto result = std::from_chars(first, last, parsed, std::chars_format::general);
  if (result.ec != std::errc() || result.ptr != last || !std::isfinite(parsed)) return false;
#else
  if (!ParseDecimal(first, last, &parsed)) return false;
#endif
  if (value) *value = parsed;
  return true;
}

STRING float64::ToString(DOUBLE value)
{
  if (!std::isfinite(value)) return STRING();
  if (value == 0) return STRING("0");
#if IB_HAVE_FLOAT_TO_CHARS
  char text[64];
  const auto result = std::to_chars(text, text+sizeof(text), value, std::chars_format::general);
  return result.ec == std::errc() ? STRING(text, size_t(result.ptr-text)) : STRING();
#else
  std::ostringstream output;
  output.imbue(std::locale::classic());
  for (int precision = 1; precision <= std::numeric_limits<DOUBLE>::max_digits10; ++precision) {
    output.str("");
    output.clear();
    output.precision(precision);
    output << value;
    if (!output) return STRING();
    const std::string text = output.str();
    const STRING term(text.data(), text.size());
    DOUBLE parsed;
    if (Parse(term, &parsed) && parsed == value) return term;
  }
  return STRING();
#endif
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
