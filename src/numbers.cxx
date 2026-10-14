/*
Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
#include <stdlib.h>
#include "common.hxx"
#include "numbers.hxx"

#include <array>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include "fixed128.hxx"

bool NUMERICOBJ::SetWhole(INT16 value)
{
  Invalidate();
  INT16 ticks;
  if (__builtin_mul_overflow(value, INT16(TICKS_PER_UNIT), &ticks)) return false;
  return SetTicks(ticks);
}

NUMERICOBJ NUMERICOBJ::FromScaled(INT16 value, UINT8 scale)
{
  NUMERICOBJ result;
  INT16 ticks;
  if (fixed128::MulDiv(value, INT16(TICKS_PER_UNIT), scale, &ticks)) result.SetTicks(ticks);
  return result;
}

bool NUMERICOBJ::Set(NUMBER value)
{
  Invalidate();
  if (!std::isfinite(value)) return false;
  // Floating callbacks enter the decimal domain once, at this boundary.
  std::ostringstream text;
  text.imbue(std::locale::classic());
  text.precision(std::numeric_limits<NUMBER>::max_digits10);
  text << value;
  return Set(STRING(text.str().c_str()));
}

bool NUMERICOBJ::Set(const STRING& input)
{
  Invalidate();
  const char *p = input.c_str(), *end = p + input.GetLength();
  while (p != end && isspace((unsigned char)*p)) ++p;
  while (end != p && isspace((unsigned char)end[-1])) --end;
  if (p == end) return false;
  bool negative = false;
  if (*p == '+' || *p == '-') { negative = *p == '-'; ++p; }
  if (p == end) return false;

  if (end-p > 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
    {
      const UINT16 limit = ((UINT16(1) << 127) - (negative ? 0 : 1)) / TICKS_PER_UNIT;
      UINT16 magnitude = 0;
      for (p += 2; p != end; ++p)
        {
          const unsigned digit = *p >= '0' && *p <= '9' ? *p-'0' :
                                 *p >= 'a' && *p <= 'f' ? *p-'a'+10 :
                                 *p >= 'A' && *p <= 'F' ? *p-'A'+10 : 16;
          if (digit == 16 || magnitude > (limit-digit)/16) return false;
          magnitude = magnitude*16+digit;
        }
      INT16 whole;
      return fixed128::Signed(magnitude, negative, &whole) && SetWhole(whole);
    }

  std::string digits;
  size_t fractional = 0;
  bool point = false;
  while (p != end)
    {
      if (*p >= '0' && *p <= '9')
        {
          digits.push_back(*p++);
          if (point) ++fractional;
        }
      else if (*p == '.' && !point) { point = true; ++p; }
      else break;
    }
  long long exponent = 0;
  if (p != end && (*p == 'e' || *p == 'E'))
    {
      ++p;
      bool minus = false;
      if (p != end && (*p == '+' || *p == '-')) { minus = *p == '-'; ++p; }
      const char *first = p;
      const long long bound = (long long)input.GetLength() + 64;
      while (p != end && *p >= '0' && *p <= '9')
        {
          exponent = exponent > (bound - (*p-'0')) / 10 ? bound : exponent * 10 + (*p-'0');
          ++p;
        }
      if (p == first) return false;
      if (minus) exponent = -exponent;
    }
  if (p != end || digits.empty())
    {
      // Preserve the existing dotnumber domain as an unsigned 64-bit code.
      if (!input.IsDotNumber()) return false;
      const bool hex = input.Search(':') != 0;
      UINT8 code = 0;
      const char *part = input.c_str();
      for (const char *q = part; ; ++q)
        if (!*q || *q == '.' || *q == ':')
          {
            if (q == part) return false;
            const STRING token(part, size_t(q-part));
            char *tail;
            const UINT8 value = strtoull(token.c_str(), &tail, hex ? 16 : 10);
            if (*tail || (!hex && value > 255) || (hex && q-part > 4)) return false;
            code = (code << (hex ? 16 : 8)) | value;
            if (!*q) break;
            part = q+1;
          }
      return SetWhole(INT16(code));
    }

  const size_t first = digits.find_first_not_of('0');
  if (first == std::string::npos) return SetTicks(0);
  digits.erase(0, first);
  const long long shift = (long long)FRACTION_DIGITS + exponent - (long long)fractional;
  const long long retained = (long long)digits.size() + (shift < 0 ? shift : 0);
  if (retained + (shift > 0 ? shift : 0) > 39) return false;

  const UINT16 limit = (UINT16(1) << 127) - (negative ? 0 : 1);
  UINT16 magnitude = 0;
  for (long long i = 0; i < retained; ++i)
    {
      const unsigned digit = digits[size_t(i)] - '0';
      if (magnitude > (limit - digit) / 10) return false;
      magnitude = magnitude * 10 + digit;
    }
  for (long long i = 0; i < shift; ++i)
    {
      if (magnitude > limit / 10) return false;
      magnitude *= 10;
    }
  if (shift < 0 && retained >= 0)
    {
      const char next = digits[size_t(retained)];
      const bool sticky = digits.find_first_not_of('0', size_t(retained)+1) != std::string::npos;
      if (next > '5' || (next == '5' && (sticky || (magnitude & 1))))
        {
          if (magnitude == limit) return false;
          ++magnitude;
        }
    }
  INT16 ticks;
  return fixed128::Signed(magnitude, negative, &ticks) && SetTicks(ticks);
}

STRING NUMERICOBJ::ToString() const
{
  if (!Valid) return STRING();
  const INT16 whole = Ticks / INT16(TICKS_PER_UNIT);
  const INT16 remainder = Ticks % INT16(TICKS_PER_UNIT);
  STRING text(whole);
  if (Ticks < 0 && !whole) text = "-0";
  if (remainder)
    {
      char fraction[17];
      snprintf(fraction, sizeof(fraction), "%016llu", (unsigned long long)(remainder < 0 ? -remainder : remainder));
      size_t length = FRACTION_DIGITS;
      while (length && fraction[length-1] == '0') --length;
      fraction[length] = 0;
      text += '.';
      text += fraction;
    }
  return text;
}

NUMERICOBJ& NUMERICOBJ::operator+=(const NUMERICOBJ& value)
{
  INT16 result;
  if (!Valid || !value.Valid || __builtin_add_overflow(Ticks, value.Ticks, &result)) Invalidate();
  else SetTicks(result);
  return *this;
}

NUMERICOBJ& NUMERICOBJ::operator-=(const NUMERICOBJ& value)
{
  INT16 result;
  if (!Valid || !value.Valid || __builtin_sub_overflow(Ticks, value.Ticks, &result)) Invalidate();
  else SetTicks(result);
  return *this;
}

NUMERICOBJ& NUMERICOBJ::operator*=(const NUMERICOBJ& value)
{
  INT16 result;
  if (!Valid || !value.Valid || !fixed128::MulDiv(Ticks, value.Ticks, TICKS_PER_UNIT, &result)) Invalidate();
  else SetTicks(result);
  return *this;
}

NUMERICOBJ& NUMERICOBJ::operator/=(const NUMERICOBJ& value)
{
  INT16 result;
  if (!Valid || !value.Valid || !value.Ticks ||
      !fixed128::MulDiv(Ticks, value.Ticks < 0 ? -INT16(TICKS_PER_UNIT) : INT16(TICKS_PER_UNIT),
                       fixed128::Magnitude(value.Ticks), &result)) Invalidate();
  else SetTicks(result);
  return *this;
}

void NUMERICOBJ::Write(FILE *fp) const
{
  ::Write(UCHR(FRACTION_DIGITS * 2 + (Valid ? 1 : 0)), fp);
  const UINT16 bits = UINT16(Ticks);
  ::Write(UINT16_high_part(bits), fp);
  ::Write(UINT16_low_part(bits), fp);
}

void Write(const NUMERICOBJ& value, FILE *fp) { value.Write(fp); }

bool NUMERICOBJ::Read(FILE *fp)
{
  Invalidate();
  std::array<BYTE, DISK_SIZE> bytes;
  if (!fp || fread(bytes.data(), 1, bytes.size(), fp) != bytes.size() ||
      (bytes[0] >> 1) != FRACTION_DIGITS) return false;
  const UINT16 bits = cons_UINT16(getINT8(bytes.data(), 1), getINT8(bytes.data(), 9));
  const UINT16 sign = UINT16(1) << 127;
  INT16 ticks;
  if (!fixed128::Signed((bits & sign) ? (~bits)+1 : bits, (bits & sign) != 0, &ticks)) return false;
  if (bytes[0] & 1) SetTicks(ticks);
  return true;
}

bool Read(NUMERICOBJ *value, FILE *fp) { return value && value->Read(fp); }

NUMERICOBJ operator+(const NUMERICOBJ& val1, const NUMERICOBJ& val2)
{
  NUMERICOBJ val;

  val = val1;
  val += val2;
  return val;
}
NUMERICOBJ operator-(const NUMERICOBJ& val1, const NUMERICOBJ& val2)
{
  NUMERICOBJ val;

  val = val1;
  val -= val2;
  return val;
}
NUMERICOBJ operator*(const NUMERICOBJ& val1, const NUMERICOBJ& val2)
{
  NUMERICOBJ val;

  val = val1;
  val *= val2;
  return val;
}
NUMERICOBJ operator/(const NUMERICOBJ& val1, const NUMERICOBJ& val2)
{
  NUMERICOBJ val;

  val = val1;
  val /= val2;
  return val;
}


NUMERICALRANGE::NUMERICALRANGE() = default;
NUMERICALRANGE::NUMERICALRANGE(const STRING& text) { SetRange(text); }
NUMERICALRANGE::~NUMERICALRANGE() = default;

bool NUMERICALRANGE::SetRange(const STRING& text)
{
  d_start = NUMERICOBJ();
  d_end = NUMERICOBJ();
  STRING range(text.Strip(STRING::both));
  const size_t length = range.GetLength();
  if (length < 3) return false;
  if ((range[0U] == '[' && range[length-1] == ']') ||
      (range[0U] == '(' && range[length-1] == ')'))
    range = STRING(range.c_str()+1, length-2);
  const char *data = range.c_str();
  for (size_t i = 1; i+1 < range.GetLength(); ++i)
    {
      size_t separator = data[i] == ',' || data[i] == '-' ? 1 :
                         data[i] == '.' && data[i+1] == '.' ? 2 : 0;
      if (!separator) continue;
      const NUMERICOBJ start(STRING(data, i)), end(STRING(data+i+separator));
      if (start.Ok() && end.Ok())
        {
          d_start = start;
          d_end = end;
          return true;
        }
    }
  return false;
}

bool NUMERICALRANGE::Ok() const { return d_start.Ok() && d_end.Ok(); }
bool NUMERICALRANGE::Defined() const { return d_start.Ok() || d_end.Ok(); }

static const INT4 CURRENCY_MODULO = MONETARYOBJ::TICKS_PER_UNIT;


void MONETARYOBJ::Invalidate()
{
  Ticks = 0;
  Valid = false;
  ExplicitFraction = false;
}

//
// Monetary values are stored as signed 128-bit counts of 1/50000-unit ticks.
// String parsing remains decimal and exact up to that resolution.
//
MONETARYOBJ::MONETARYOBJ()
{
  Invalidate();
}

MONETARYOBJ::MONETARYOBJ(const STRING& s)
{
  Invalidate();
  Set(s);
}


#if 1

/*
"123.45"          -> 123.45
"1.230"           -> 1.230
"12.250"          -> 12.250

"123.456.000"     -> 123456000
"1,234,567"       -> 1234567
"12,12,123"       -> 1212123

"123.456,99"      -> 123456.99
"123,456.99"      -> 123456.99

"1 234,56"        -> 1234.56
"1 234,56"        -> 1234.56
"1 234,56"        -> 1234.56
"1'234.56"        -> 1234.56
"1’234.56"        -> 1234.56

"₹1,234"          -> 1234
"$1,234"          -> 1234
"€1,234"          -> 1.234

*/

enum MONEY_FORMAT_HINT {
  MONEY_FORMAT_UNKNOWN,
  MONEY_FORMAT_DOT_DECIMAL
};


static inline bool MoneyDigit(unsigned char c)
{
  return c >= '0' && c <= '9';
}


// Unicode whitespace which can occur as numeric grouping.
// Returns number of UTF-8 bytes consumed.
static size_t MoneySpaceLen(const unsigned char *p, const unsigned char *end)
{
  if (p >= end)
    return 0;

  // ASCII whitespace
  if (*p == ' ' || (*p >= '\t' && *p <= '\r'))
    return 1;

  // Legacy single-byte NBSP.
  if (*p == 0xA0)
    return 1;

  // U+00A0 NO-BREAK SPACE
  if (end - p >= 2 &&
      p[0] == 0xC2 && p[1] == 0xA0)
    return 2;

  if (end - p >= 3)
    {
      // U+2000 .. U+200A
      // includes EN SPACE, EM SPACE, THIN SPACE, HAIR SPACE, etc.
      if (p[0] == 0xE2 && p[1] == 0x80 &&
          p[2] >= 0x80 && p[2] <= 0x8A)
        return 3;

      // U+202F NARROW NO-BREAK SPACE
      if (p[0] == 0xE2 && p[1] == 0x80 && p[2] == 0xAF)
        return 3;

      // U+205F MEDIUM MATHEMATICAL SPACE
      if (p[0] == 0xE2 && p[1] == 0x81 && p[2] == 0x9F)
        return 3;

      // U+3000 IDEOGRAPHIC SPACE
      if (p[0] == 0xE3 && p[1] == 0x80 && p[2] == 0x80)
        return 3;
    }

  return 0;
}


static void SkipMoneySpaces(const unsigned char *&p, const unsigned char *end)
{
  size_t n;

  while ((n = MoneySpaceLen(p, end)) != 0)
    p += n;
}

static void TrimMoneySpaces(const unsigned char *begin, const unsigned char *&end)
{
  for (;;)
    {
      if (end <= begin)
        return;

      // ASCII / legacy single-byte space.
      if (end[-1] == ' ' ||
          (end[-1] >= '\t' && end[-1] <= '\r') ||
          end[-1] == 0xA0)
        {
          --end;
          continue;
        }

      if (end - begin >= 2 &&
          end[-2] == 0xC2 && end[-1] == 0xA0)
        {
          end -= 2;
          continue;
        }

      if (end - begin >= 3)
        {
          if (end[-3] == 0xE2 && end[-2] == 0x80 &&
              end[-1] >= 0x80 && end[-1] <= 0x8A)
            {
              end -= 3;
              continue;
            }

          if (end[-3] == 0xE2 && end[-2] == 0x80 &&
              end[-1] == 0xAF)
            {
              end -= 3;
              continue;
            }

          if (end[-3] == 0xE2 && end[-2] == 0x81 &&
              end[-1] == 0x9F)
            {
              end -= 3;
              continue;
            }

          if (end[-3] == 0xE3 && end[-2] == 0x80 &&
              end[-1] == 0x80)
            {
              end -= 3;
              continue;
            }
        }

      return;
    }
}


// Numeric grouping other than '.' and ','.
static size_t MoneyGroupingLen(const unsigned char *p, const unsigned char *end)
{
  size_t n = MoneySpaceLen(p, end);

  if (n)
    return n;

  if (p >= end)
    return 0;

  // ASCII apostrophe -- common Swiss representation.
  if (*p == '\'')
    return 1;

  // U+2018 / U+2019
  if (end - p >= 3 &&
      p[0] == 0xE2 && p[1] == 0x80 &&
      (p[2] == 0x98 || p[2] == 0x99))
    return 3;

  // U+02BC MODIFIER LETTER APOSTROPHE
  if (end - p >= 2 &&
      p[0] == 0xCA && p[1] == 0xBC)
    return 2;

  // U+FF07 FULLWIDTH APOSTROPHE
  if (end - p >= 3 &&
      p[0] == 0xEF && p[1] == 0xBC && p[2] == 0x87)
    return 3;

  return 0;
}


// Consume one leading currency symbol.
//
// The hint is deliberately weak.  It is only used to resolve a single
// comma such as "$1,234" or "₹1,234".  A single '.' always remains
// decimal, even for these currencies.
static bool ConsumeCurrencySymbol(const unsigned char *&p, const unsigned char *end, MONEY_FORMAT_HINT *hint)
{
  if (p >= end)
    return false;

  switch (*p)
    {
    case '$':
      ++p;
      if (hint) *hint = MONEY_FORMAT_DOT_DECIMAL;
      return true;

    case 163:                       // £ legacy
    case 165:                       // ¥ legacy
      ++p;
      if (hint) *hint = MONEY_FORMAT_DOT_DECIMAL;
      return true;

    case 164:                       // ¤ / € in ISO-8859-15
      ++p;
      return true;
    }

  // UTF-8 £
  if (end - p >= 2 && p[0] == 0xC2 && p[1] == 0xA3)
    {
      p += 2;
      if (hint) *hint = MONEY_FORMAT_DOT_DECIMAL;
      return true;
    }

  // UTF-8 ¤
  if (end - p >= 2 && p[0] == 0xC2 && p[1] == 0xA4)
    {
      p += 2;
      return true;
    }

  // UTF-8 ¥
  if (end - p >= 2 && p[0] == 0xC2 && p[1] == 0xA5)
    {
      p += 2;
      if (hint) *hint = MONEY_FORMAT_DOT_DECIMAL;
      return true;
    }

  // Unicode Currency Symbols U+20A0 .. U+20BF.
  if (end - p >= 3 &&
      p[0] == 0xE2 && p[1] == 0x82 &&
      p[2] >= 0xA0 && p[2] <= 0xBF)
    {
      // U+20A8 RUPEE SIGN, U+20B9 INDIAN RUPEE SIGN
      if (hint && (p[2] == 0xA8 || p[2] == 0xB9))
        *hint = MONEY_FORMAT_DOT_DECIMAL;

      p += 3;
      return true;
    }

  return false;
}


static bool ConsumeCentSuffix(const unsigned char *&end, const unsigned char *begin)
{
  // Legacy ¢.
  if (end > begin && end[-1] == 162)
    {
      --end;
      return true;
    }

  // UTF-8 U+00A2 ¢
  if (end - begin >= 2 && end[-2] == 0xC2 && end[-1] == 0xA2)
    {
      end -= 2;
      return true;
    }

  return false;
}


// Validate grouping in the integer part.
//
// Accepted:
//
//   1,234,567
//   123.456.789
//   12,34,567
//   1'234'567
//   1 234 567
//
// Separators may be mixed.  Messy source data is preferable to data loss.
static bool ValidMoneyGrouping(const unsigned char *p, const unsigned char *end)
{
  if (p == end)
    return true;                    // ".99"

  size_t separators = 0;

  for (const unsigned char *q = p; q < end; )
    {
      if (MoneyDigit(*q))
        {
          ++q;
          continue;
        }

      size_t n = 0;

      if (*q == '.' || *q == ',')
        n = 1;
      else
        n = MoneyGroupingLen(q, end);

      if (!n)
        return false;

      ++separators;
      q += n;
    }

  if (!separators)
    {
      for (const unsigned char *q = p; q < end; ++q)
        if (!MoneyDigit(*q))
          return false;

      return true;
    }

  const size_t groups = separators + 1;
  size_t group = 0;
  size_t digits = 0;

  bool western = true;
  bool indian  = true;

  for (const unsigned char *q = p; ; )
    {
      bool atEnd = q >= end;
      size_t n = 0;

      if (!atEnd)
        {
          if (MoneyDigit(*q))
            {
              ++digits;
              ++q;
              continue;
            }

          if (*q == '.' || *q == ',')
            n = 1;
          else
            n = MoneyGroupingLen(q, end);

          if (!n)
            return false;
        }

      // Empty group: ",123", "123,,456", "123,"
      if (digits == 0)
        return false;

      if (group == 0)
        {
          if (digits < 1 || digits > 3)
            western = false;

          if (digits < 1 || digits > 2)
            indian = false;
        }
      else
        {
          if (digits != 3)
            western = false;

          if (group == groups - 1)
            {
              if (digits != 3)
                indian = false;
            }
          else if (digits != 2)
            indian = false;
        }

      ++group;
      digits = 0;

      if (atEnd)
        break;

      q += n;
    }

  return western || indian;
}


bool MONETARYOBJ::Set(const STRING& s)
{
  Invalidate();

  const unsigned char *p = reinterpret_cast<const unsigned char *>(s.c_str());

  const unsigned char *end = p + s.GetLength();

  SkipMoneySpaces(p, end);
  TrimMoneySpaces(p, end);

  if (p == end)
    return false;

  MONEY_FORMAT_HINT hint = MONEY_FORMAT_UNKNOWN;

  // Optional leading currency symbol.
  ConsumeCurrencySymbol(p, end, &hint);
  SkipMoneySpaces(p, end);

  if (p == end)
    return false;

  bool negative = false;

  if (*p == '-' || *p == '+')
    {
      negative = (*p == '-');
      ++p;
      SkipMoneySpaces(p, end);

      if (p == end)
        return false;
    }

  // Legacy / Unicode cent suffix.
  bool cents = false;

  {
    const unsigned char *e = end;

    if (ConsumeCentSuffix(e, p))
      {
        cents = true;
        end = e;
        TrimMoneySpaces(p, end);

        // A cent expression naturally uses dot-decimal/comma-grouping
        // conventions when we have to disambiguate a single comma.
        hint = MONEY_FORMAT_DOT_DECIMAL;
      }
  }

  if (p == end)
    return false;


  //
  // First pass: classify '.' and ','.
  //
  unsigned dots = 0;
  unsigned commas = 0;

  const unsigned char *lastDot = NULL;
  const unsigned char *lastComma = NULL;

  bool sawDigit = false;

  for (const unsigned char *q = p; q < end; )
    {
      if (MoneyDigit(*q))
        {
          sawDigit = true;
          ++q;
          continue;
        }

      if (*q == '.')
        {
          ++dots;
          lastDot = q++;
          continue;
        }

      if (*q == ',')
        {
          ++commas;
          lastComma = q++;
          continue;
        }

      const size_t n = MoneyGroupingLen(q, end);

      if (!n)
        return false;

      q += n;
    }

  if (!sawDigit)
    return false;


  //
  // Determine decimal separator.
  //
  const unsigned char *decimal = NULL;

  if (dots && commas)
    {
      // 123.456,78
      // 123,456.78
      //
      // Structural evidence wins: rightmost separator is decimal.
      decimal = lastDot > lastComma ? lastDot : lastComma;
    }
  else if (dots == 1)
    {
      // Canonical monetary representation.
      //
      // Never infer grouping merely because three digits follow:
      // 1.230 is a perfectly legitimate unit price.
      decimal = lastDot;
    }
  else if (commas == 1)
    {
      decimal = lastComma;

      // For currencies whose normal convention strongly uses comma
      // grouping, first see whether the whole token is a valid grouped
      // integer:
      //
      //   $1,234  -> 1234
      //   ₹1,234  -> 1234
      //
      // but:
      //
      //   $12,34  -> 12.34
      //
      if (hint == MONEY_FORMAT_DOT_DECIMAL &&
          ValidMoneyGrouping(p, end))
        decimal = NULL;
    }

  // Multiple '.' alone or multiple ',' alone are grouping.
  // Thus "123.456.000" becomes 123456000.


  //
  // Validate the integer/grouping portion.
  //
  const unsigned char *integerEnd = decimal ? decimal : end;

  if (!ValidMoneyGrouping(p, integerEnd))
    return false;


  //
  // Build the whole part exactly.  No floating point involved.
  //
  UINT16 whole = 0;

  for (const unsigned char *q = p; q < integerEnd; )
    {
      if (MoneyDigit(*q))
        {
          const UINT16 digit = *q - '0';

          if (whole > (MAX_UINT16 - digit) / 10)
            return false;

          whole = whole * 10 + digit;
          ++q;
          continue;
        }

      size_t n;

      if (*q == '.' || *q == ',')
        n = 1;
      else
        n = MoneyGroupingLen(q, integerEnd);

      if (!n)
        return false;

      q += n;
    }


  //
  // Convert fractional decimal digits directly to monetary ticks.
  //
  // CURRENCY_MODULO == 50000 == 5 * 10^4.
  //
  // Therefore the first five decimal digits are sufficient to round
  // exactly to the nearest 1/50000:
  //
  //      .12345 * 50000 = 6172.5  -> 6173
  //
  // and all values through four decimal places are exact.
  //
  // For a cent suffix the scale is 500 ticks/cent, so the same
  // observation applies using the first three fractional digits.
  //
  UINT4 fractionScaled = 0;

  if (decimal)
    {
      const unsigned char *q = decimal + 1;

      if (q == end)
        return false;               // "123."

      const unsigned needed = cents ? 3 : 5;

      UINT4 firstDigits = 0;
      unsigned got = 0;

      for (; q < end; ++q)
        {
          if (!MoneyDigit(*q))
            return false;

          if (got < needed)
            {
              firstDigits = firstDigits * 10 + (*q - '0');
              ++got;
            }
        }

      if (got == 0)
        return false;

      while (got < needed)
        {
          firstDigits *= 10;
          ++got;
        }

      // Exact equivalent of floor(x + 0.5) at our fixed resolution.
      fractionScaled = (firstDigits + 1) / 2;
    }


  //
  // Convert to the canonical 1/50000 representation.
  //
  // Ordinary monetary input:
  //       whole * 50000 + fraction
  //
  // Cent input:
  //       cents * 500 + fractional-cent ticks
  //
  const UINT16 scale = (UINT16)(cents ? 500 : CURRENCY_MODULO);
  const UINT16 minMagnitude = (UINT16)MAX_INT16 + 1;
  const UINT16 maxMagnitude =
      negative ? minMagnitude : (UINT16)MAX_INT16;

  if (whole > maxMagnitude / scale)
    return false;

  UINT16 magnitude = whole * scale;

  if ((UINT16)fractionScaled > maxMagnitude - magnitude)
    return false;

  magnitude += (UINT16)fractionScaled;

  if (negative)
    {
      if (magnitude == minMagnitude)
        Ticks = MIN_INT16;
      else
        Ticks = -(INT16)magnitude;
    }
  else
    {
      Ticks = (INT16)magnitude;
    }

  Valid = true;
  ExplicitFraction = (decimal != NULL) || cents;
  return true;
}

#else

static bool
ConsumeCurrencySymbol(const unsigned char *&p, const unsigned char *end)
{
    if (p >= end)
        return false;

    switch (*p) {
    case '$':
    case 163: // £
    case 164: // ¤ / legacy €
    case 165: // ¥
        ++p;
        return true;
    }

    static const unsigned char euro[]   = { 0xE2, 0x82, 0xAC };
    static const unsigned char rupee[]  = { 0xE2, 0x82, 0xB9 };
    static const unsigned char rupee2[] = { 0xE2, 0x82, 0xA8 };
    static const unsigned char won[]    = { 0xE2, 0x82, 0xA9 };
    static const unsigned char shekel[] = { 0xE2, 0x82, 0xAA };
    static const unsigned char dong[]   = { 0xE2, 0x82, 0xAB };
    static const unsigned char peso[]   = { 0xE2, 0x82, 0xB1 };
    static const unsigned char hryvnia[]= { 0xE2, 0x82, 0xB4 };
    static const unsigned char lira[]   = { 0xE2, 0x82, 0xBA };
    static const unsigned char ruble[]  = { 0xE2, 0x82, 0xBD };

    struct CurrencySymbol {
        const unsigned char *bytes;
        size_t length;
    };

    static const CurrencySymbol symbols[] = {
        { euro,    sizeof(euro)    },
        { rupee,   sizeof(rupee)   },
        { rupee2,  sizeof(rupee2)  },
        { won,     sizeof(won)     },
        { shekel,  sizeof(shekel)  },
        { dong,    sizeof(dong)    },
        { peso,    sizeof(peso)    },
        { hryvnia, sizeof(hryvnia) },
        { lira,    sizeof(lira)    },
        { ruble,   sizeof(ruble)   }
    };

    for (const auto& symbol : symbols) {
        if (static_cast<size_t>(end - p) >= symbol.length &&
            memcmp(p, symbol.bytes, symbol.length) == 0) {
            p += symbol.length;
            return true;
        }
    }

    return false;
}

bool MONETARYOBJ::Set(const STRING& s)
{
  Amount = 0;
  Fract  = BAD_CURRENCY_VAL;

  const unsigned char *p =
    reinterpret_cast<const unsigned char *>(s.c_str());
  const unsigned char *end = p + s.GetLength();

  while (p < end && isspace(*p))
    ++p;

  while (end > p && isspace(end[-1]))
    --end;

  if (p == end)
    return false;

  // Optional leading currency sign.
  ConsumeCurrencySymbol(p, end);

  if (p == end)
    return false;

  // Positive amounts only with the current unsigned representation.
  if (*p == '-')
    return false;

  if (*p == '+')
    ++p;

  bool cents = false;

  // Legacy cent suffix: "99¢".
  if (end > p && end[-1] == cent)
    {
      cents = true;
      --end;

      while (end > p && isspace(end[-1]))
        --end;
    }

  NUMBER value = 0;
  NUMBER place = 0.1L;

  bool sawDigit   = false;
  bool sawDecimal = false;
  bool sawFractionDigit = false;

  for (; p < end; ++p)
    {
      if (isdigit(*p))
        {
          sawDigit = true;
          const unsigned digit = *p - '0';

          if (!sawDecimal)
            value = value * 10 + digit;
          else
            {
              sawFractionDigit = true;
              value += digit * place;
              place *= 0.1L;
            }
        }
      else if ((*p == '.' || *p == ',') && !sawDecimal)
        {
          sawDecimal = true;
        }
      else
        {
          // No partial parses: GUIDs, "123abc", "12-34", etc. fail.
          return false;
        }
    }

  if (!sawDigit)
    return false;

  if (sawDecimal && !sawFractionDigit)
    return false;

  if (cents)
    value /= 100.0L;

  return Set(value);
}
#endif


MONETARYOBJ::MONETARYOBJ(const NUMBER x)
{
  Invalidate();
  Set(x);
}

MONETARYOBJ::MONETARYOBJ(const NUMERICOBJ& x)
{
  Invalidate();
  INT16 ticks;
  if (x.Ok() && fixed128::MulDiv(x.GetTicks(), INT16(TICKS_PER_UNIT), NUMERICOBJ::TICKS_PER_UNIT, &ticks))
    SetTicks(ticks);
}


bool MONETARYOBJ::Set(const NUMBER x)
{
  Invalidate();

  const long double input = (long double)x;
  if (!std::isfinite(input))
    return false;

  const long double scaled = input * (long double)CURRENCY_MODULO;
  if (!std::isfinite(scaled))
    return false;

  const long double rounded =
      scaled < 0.0L ? ceill(scaled - 0.5L) : floorl(scaled + 0.5L);

  if (rounded < (long double)MIN_INT16 ||
      rounded > (long double)MAX_INT16)
    return false;

  Ticks = (INT16)rounded;
  Valid = true;

  // NUMBER has already lost lexical precision information. Treat it as
  // an explicitly specified numeric value, never whole-unit shorthand.
  ExplicitFraction = true;
  return true;
}


bool MONETARYOBJ::RoundToNearest100th()
{
  if (!Valid)
    return false;

  const INT16 cent = (INT16)(CURRENCY_MODULO / 100);
  const INT16 half = cent / 2;
  const INT16 remainder = Ticks % cent;
  INT16 rounded = Ticks - remainder;

  if (remainder >= half)
    {
      if (rounded > MAX_INT16 - cent)
        return false;
      rounded += cent;
    }
  else if (remainder <= -half)
    {
      if (rounded < MIN_INT16 + cent)
        return false;
      rounded -= cent;
    }

  Ticks = rounded;
  ExplicitFraction = true;
  return true;
}

STRING MONETARYOBJ::ToString() const
{
  if (!Valid) return STRING();

  const INT16 whole = Ticks / (INT16)TICKS_PER_UNIT;
  const INT16 remainder = Ticks % (INT16)TICKS_PER_UNIT;
  STRING term(whole);
  if (Ticks < 0 && whole == 0) term = "-0";

  // One tick is exactly 0.00002. The remainder is small even at INT128_MIN.
  const unsigned fraction = (unsigned)(remainder < 0 ? -remainder : remainder) * 2;
  char digits[6];
  snprintf(digits, sizeof(digits), "%05u", fraction);
  size_t length = 5;
  while (length > 2 && digits[length-1] == '0') --length;
  digits[length] = '\0';
  term += '.';
  term += digits;
  return term;
}


void MONETARYOBJ::Write(FILE *Fp) const
{
  if (Fp == NULL)
    return;

  const UCHR flags =
      (Valid ? 0x01 : 0x00) |
      (ExplicitFraction ? 0x02 : 0x00);

  const UINT16 bits = (UINT16)Ticks;

  ::Write(flags, Fp);
  ::Write(UINT16_high_part(bits), Fp);
  ::Write(UINT16_low_part(bits), Fp);
}


void Write(const MONETARYOBJ& s, FILE *Fp)
{
  s.Write(Fp);
}


bool MONETARYOBJ::Read(FILE *Fp)
{
  Invalidate();

  if (Fp == NULL)
    return false;

  std::array<BYTE, 17> buf;

  if (fread(buf.data(), 1, buf.size(), Fp) != buf.size())
    return false;

  const UCHR flags = buf[0];
  const UINT16 bits = cons_UINT16(
      getINT8(buf.data(), 1),
      getINT8(buf.data(), 9));
  const UINT16 sign = ((UINT16)1 << 127);

  if ((bits & sign) == 0)
    Ticks = (INT16)bits;
  else
    {
      const UINT16 magnitude = (~bits) + 1;

      if (magnitude == sign)
        Ticks = MIN_INT16;
      else
        Ticks = -(INT16)magnitude;
    }

  Valid = (flags & 0x01) != 0;
  ExplicitFraction = (flags & 0x02) != 0;

  if (!Valid)
    Ticks = 0;

  return Valid;
}


bool Read(MONETARYOBJ *p, FILE *Fp)
{
  return p ? p->Read(Fp) : false;
}


//
// Monetary range syntax mirrors INTEGER ranges, but each endpoint uses the
// monetary parser (currency symbols, grouping, decimal separator, signs).
//
bool MONETARYRANGE::SetRange(const STRING& RangeString)
{
  const STRING range(RangeString.Strip(STRING::both));
  const char *text = range.c_str();
  const size_t length = range.GetLength();

  if (text == NULL || length < 3)
    return false;

  for (size_t i = 1; i + 1 < length; ++i)
    {
      if (text[i] != '-')
        continue;

      const STRING left(text, i);
      const STRING right(text + i + 1);

      const MONETARYOBJ start(left);
      const MONETARYOBJ end(right);

      if (!start.Ok() || !end.Ok())
        continue;

      d_start = start;
      d_end = end;
      return true;
    }

  return false;
}


bool MONETARYRANGE::Contains(const MONETARYOBJ& Test) const
{
  if (!Ok() || !Test.Ok())
    return false;

  if (d_start <= d_end)
    return Test >= d_start && Test <= d_end;

  return Test >= d_end && Test <= d_start;
}


#if 0 

#define BAD_BOOLEAN 0xFF

BOOLEANOBJ::BOLLEANOBJ ()
{
  val = BAD_BOOLEAN; 
}

BOOLEANOBJ::NUMERICOBJ(const STRING& s)
{
  if (s.GetLength())
    val = s.IsNumber() ? s.GetLong() != 0 : s.GetBool();
  else
    val = BAD_BOOLEAN;
}

bool BOOLEANOBJ::Ok() const
{
  return val != BAD_BOOLEAN;
}

void Write(const BOOLEANOBJ& s, FILE *Fp)
{
  Write(s.val, Fp);
}

inline bool Read(BOOLEANOBJ *p, FILE *Fp)
{
  BYTE x;
  if (Read(&x, Fp) == false)
    x = BAD_BOOLEAN;
  if (p) p->val = x;
  return x != BAD_BOOLEAN;
}


#endif


//
// Integer range syntax.
//
// For now the compact query form is:
//
//     10-15
//     -10-15
//     -15--10
//
// INTEGEROBJ remains the authority for endpoint parsing, so the range
// automatically inherits the full signed 128-bit syntax and validation.
//
bool INTEGERRANGE::SetRange(const STRING& RangeString)
{
  const STRING range(RangeString.Strip(STRING::both));
  const char *text = range.c_str();
  const size_t length = range.GetLength();

  if (text == NULL || length < 3)
    return false;

  //
  // A leading '-' belongs to the first endpoint. Every later '-' is a
  // candidate separator; accept the first split for which both sides are
  // valid INTEGEROBJs. This also handles a negative second endpoint.
  //
  for (size_t i = 1; i + 1 < length; ++i)
    {
      if (text[i] != '-')
        continue;

      const STRING left(text, i);
      const STRING right(text + i + 1);

      const INTEGEROBJ start(left);
      const INTEGEROBJ end(right);

      if (!start.Ok() || !end.Ok())
        continue;

      d_start = start;
      d_end   = end;
      return true;
    }

  return false;
}


bool INTEGERRANGE::Contains(const INTEGEROBJ& Test) const
{
  if (!Ok() || !Test.Ok())
    return false;

  if (d_start <= d_end)
    return Test >= d_start && Test <= d_end;

  return Test >= d_end && Test <= d_start;
}


// We now will support 128-bit integers
bool INTEGEROBJ::Set(const STRING& s)
{
  valid = false;
  val   = 0;

  const char *p   = s.c_str();
  const char *end = p + s.GetLength();

  /*
   * Return the byte length of a whitespace character we are willing
   * to treat as presentation/grouping whitespace.
   */
  const auto space_len = [](const char *q, const char *last) -> size_t
  {
    if (q >= last)
      return 0;

    const unsigned char c0 = (unsigned char)q[0];

    // ASCII whitespace
    if (c0 == ' ' || (c0 >= '\t' && c0 <= '\r'))
      return 1;

    // Legacy single-byte NBSP
    if (c0 == 0xA0)
      return 1;

    // U+00A0 NO-BREAK SPACE
    if (last - q >= 2 &&
        c0 == 0xC2 &&
        (unsigned char)q[1] == 0xA0)
      return 2;

    if (last - q >= 3) {
      const unsigned char c1 = (unsigned char)q[1];
      const unsigned char c2 = (unsigned char)q[2];

      // U+2000..U+200A
      if (c0 == 0xE2 && c1 == 0x80 &&
          c2 >= 0x80 && c2 <= 0x8A)
        return 3;

      // U+202F NARROW NO-BREAK SPACE
      if (c0 == 0xE2 && c1 == 0x80 && c2 == 0xAF)
        return 3;

      // U+205F MEDIUM MATHEMATICAL SPACE
      if (c0 == 0xE2 && c1 == 0x81 && c2 == 0x9F)
        return 3;

      // U+3000 IDEOGRAPHIC SPACE
      if (c0 == 0xE3 && c1 == 0x80 && c2 == 0x80)
        return 3;
    }

    return 0;
  };


  /*
   * Punctuation which, for an INTEGER field, may simply be ignored
   * as grouping/presentation noise.
   */
  const auto grouping_len = [&](const char *q, const char *last) -> size_t
  {
    if (q >= last)
      return 0;

    const unsigned char c0 = (unsigned char)q[0];

    if (c0 == '.' || c0 == ',' || c0 == '\'')
      return 1;

    if (const size_t n = space_len(q, last))
      return n;

    // U+02BC MODIFIER LETTER APOSTROPHE
    if (last - q >= 2 &&
        c0 == 0xCA &&
        (unsigned char)q[1] == 0xBC)
      return 2;

    if (last - q >= 3) {
      const unsigned char c1 = (unsigned char)q[1];
      const unsigned char c2 = (unsigned char)q[2];

      // U+2018 / U+2019
      if (c0 == 0xE2 && c1 == 0x80 &&
          (c2 == 0x98 || c2 == 0x99))
        return 3;

      // U+FF07 FULLWIDTH APOSTROPHE
      if (c0 == 0xEF && c1 == 0xBC && c2 == 0x87)
        return 3;
    }

    return 0;
  };


  /*
   * Trim presentation whitespace.
   */
  while (p < end) {
    const size_t n = space_len(p, end);
    if (!n)
      break;
    p += n;
  }

  if (p == end)
    return false;

  /*
   * Find the end excluding trailing Unicode whitespace.
   */
  {
    const char *last_nonspace = p;

    for (const char *q = p; q < end; ) {
      const size_t n = space_len(q, end);

      if (n) {
        q += n;
      } else {
        ++q;
        last_nonspace = q;
      }
    }

    end = last_nonspace;
  }

  if (p == end)
    return false;


  /*
   * std::from_chars does not accept leading '+'.
   *
   * Do not accidentally accept "+-123".
   */
  if (*p == '+') {
    ++p;

    if (p == end || *p == '+' || *p == '-')
      return false;
  }


  /*
   * From here on we interpret the value according to INTEGER semantics.
   */
  bool negative = false;

  if (*p == '-') {
    negative = true;

    if (++p == end)
      return false;
  }


  /*
   * Signed 128-bit magnitude limits.
   *
   * Positive:  2^127 - 1
   * Negative:  2^127
   */
  const UINT16 sign_bit = ((UINT16)1 << 127);
  const UINT16 limit    = negative ? sign_bit : sign_bit - 1;


  /*
   * Install an already validated magnitude.
   */
  const auto commit = [&](UINT16 magnitude) -> bool
  {
    if (magnitude > limit)
      return false;

    if (negative) {
      if (magnitude == sign_bit) {
        // Construct INT128_MIN without overflowing signed arithmetic.
        val = -((INT16)(sign_bit - 1)) - 1;
      } else {
        val = -(INT16)magnitude;
      }
    } else {
      val = (INT16)magnitude;
    }

    valid = true;
    return true;
  };

  // std::from_chars for __int128 is unavailable in strict C++17 on
  // several standard libraries. Keep the ordinary integer path exact.
  {
    UINT16 magnitude = 0;
    const char *q = p;
    for (; q != end && *q >= '0' && *q <= '9'; ++q)
      {
        const unsigned digit = *q-'0';
        if (magnitude > (limit-digit)/10) break;
        magnitude = magnitude*10+digit;
      }
    if (q == end) return commit(magnitude);
  }


  /*
   * Is this scientific notation?
   *
   * Presence of e/E changes the meaning of '.' and ',':
   *
   *     1.2e6
   *     1,2e6
   *
   * both have an unambiguous decimal mantissa.
   */
  const char *epos = nullptr;

  for (const char *q = p; q < end; ++q) {
    if (*q == 'e' || *q == 'E') {
      if (epos != nullptr)
        return false;               // 1e2e3

      epos = q;
    }
  }


  /*
   * ORDINARY INTEGER
   *
   * Once the field is declared INTEGER, punctuation between digits
   * is merely presentation noise:
   *
   *     1,234,567
   *     1.234.567
   *     12,34,567
   *     12'235,678
   *     1.0000
   *
   * We intentionally do not attempt to validate grouping conventions.
   */
  if (epos == nullptr) {
    UINT16 magnitude = 0;
    bool seen_digit  = false;
    bool after_group = false;

    for (const char *q = p; q < end; ) {
      const unsigned char c = (unsigned char)*q;

      if (c >= '0' && c <= '9') {
        const unsigned digit = c - '0';

        if (magnitude > (limit - digit) / 10)
          return false;

        magnitude = magnitude * 10 + digit;

        seen_digit  = true;
        after_group = false;
        ++q;
        continue;
      }

      const size_t n = grouping_len(q, end);

      if (!n || !seen_digit)
        return false;

      /*
       * We even allow mixtures such as:
       *
       *     12'235,678
       *     1, 234
       *
       * but not leading/trailing grouping garbage.
       */
      after_group = true;
      q += n;
    }

    if (!seen_digit || after_group)
      return false;

    return commit(magnitude);
  }


  /*
   * SCIENTIFIC NOTATION
   *
   * Mantissa permits exactly one '.' OR ','.
   *
   *     1.2e6      OK
   *     1,2e6      OK
   *     .5e3       OK
   *     1.e3       OK
   *
   *     1.2,3e7    INVALID
   *     1,2.3e7    INVALID
   *     1.2.3e7    INVALID
   */
  if (epos == p)
    return false;

  bool   decimal_seen     = false;
  bool   mantissa_digit   = false;
  bool   mantissa_nonzero = false;
  size_t total_digits     = 0;
  size_t fractional_digits = 0;

  for (const char *q = p; q < epos; ++q) {
    const unsigned char c = (unsigned char)*q;

    if (c >= '0' && c <= '9') {
      mantissa_digit = true;

      if (c != '0')
        mantissa_nonzero = true;

      ++total_digits;

      if (decimal_seen)
        ++fractional_digits;

      continue;
    }

    if (c == '.' || c == ',') {
      if (decimal_seen)
        return false;

      decimal_seen = true;
      continue;
    }

    return false;
  }

  if (!mantissa_digit)
    return false;


  /*
   * Parse exponent.
   */
  const char *q = epos + 1;

  if (q == end)
    return false;

  bool exponent_negative = false;

  if (*q == '+' || *q == '-') {
    exponent_negative = (*q == '-');

    if (++q == end)
      return false;
  }

  UINT8 exponent = 0;
  bool exponent_overflow = false;

  const UINT8 max_uint8 = (UINT8)-1;

  for (; q < end; ++q) {
    const unsigned char c = (unsigned char)*q;

    if (c < '0' || c > '9')
      return false;

    const unsigned digit = c - '0';

    if (!exponent_overflow) {
      if (exponent > (max_uint8 - digit) / 10) {
        exponent_overflow = true;
      } else {
        exponent = exponent * 10 + digit;
      }
    }
  }


  /*
   * An exponent too large even for UINT8 is necessarily much larger
   * than any physically representable input string.
   */
  if (exponent_overflow) {
    if (!mantissa_nonzero)
      return commit(0);

    if (exponent_negative)
      return commit(0);

    return false;
  }


  /*
   * Accumulate the first N mantissa digits, ignoring the decimal marker.
   *
   * This is important: for a negative effective decimal exponent we
   * don't need to construct the complete coefficient and then divide it.
   * We simply retain the digits which survive truncation toward zero.
   */
  const auto accumulate = [&](size_t keep, UINT16 *result) -> bool
  {
    UINT16 magnitude = 0;
    size_t count = 0;

    for (const char *r = p; r < epos && count < keep; ++r) {
      const unsigned char c = (unsigned char)*r;

      if (c < '0' || c > '9')
        continue;

      const unsigned digit = c - '0';

      if (magnitude > (limit - digit) / 10)
        return false;

      magnitude = magnitude * 10 + digit;
      ++count;
    }

    *result = magnitude;
    return true;
  };


  UINT16 magnitude = 0;


  /*
   * Negative exponent:
   *
   *   coefficient × 10^-(exponent + fractional_digits)
   *
   * Drop the appropriate number of trailing coefficient digits.
   */
  if (exponent_negative) {

    if (exponent >= total_digits)
      return commit(0);

    const size_t remaining =
        total_digits - (size_t)exponent;

    if (fractional_digits >= remaining)
      return commit(0);

    const size_t keep =
        remaining - fractional_digits;

    if (!accumulate(keep, &magnitude))
      return false;

    return commit(magnitude);
  }


  /*
   * Positive exponent.
   */
  if (exponent < fractional_digits) {

    /*
     * Some fractional digits remain and are simply discarded.
     *
     *     1.25e1  -> 12
     */
    const size_t drop =
        fractional_digits - (size_t)exponent;

    if (drop >= total_digits)
      return commit(0);

    const size_t keep =
        total_digits - drop;

    if (!accumulate(keep, &magnitude))
      return false;

    return commit(magnitude);
  }


  /*
   * Decimal point has moved past all existing coefficient digits.
   *
   *     1.25e2 -> 125
   *     1.2e6  -> 1200000
   */
  if (!accumulate(total_digits, &magnitude))
    return false;

  const UINT8 zeros =
      exponent - (UINT8)fractional_digits;

  /*
   * Any nonzero 128-bit integer with more than 38 appended decimal
   * zeros must overflow.
   */
  if (magnitude != 0 && zeros > 38)
    return false;

  for (UINT8 i = 0; i < zeros; ++i) {
    if (magnitude > limit / 10)
      return false;

    magnitude *= 10;
  }

  return commit(magnitude);
}
