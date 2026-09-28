/*
Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
#include <stdlib.h>
#include "common.hxx"
#include "numbers.hxx"

#include <charconv>

static DOUBLE BAD_NUMBER = 1.17549435e-38F;

NUMERICOBJ::NUMERICOBJ ()
{
  val = BAD_NUMBER;
}

NUMERICOBJ::NUMERICOBJ(const STRING& s)
{
  if (s.IsNumber())
    {
      val = (NUMBER)s;
    }
  else if (s.IsDotNumber())
    {
      UINT8     x = 0;
      unsigned  i = 0;
      char      buf[20];
      for (const char *ptr = s.c_str(); *ptr && i < sizeof(buf)-1 ; ptr++)
	{
	  if ((buf[i] = *ptr) == '.' || *ptr == ':')
	    {
	      int base = *ptr == ':' ? 16 : 10;
	      buf[i] = '\0';
	      if (2*(i/2) != i )
		i++; // Add i for odd number
	      x <<= (base == 10 ? 8 : i*4);
	      x += (int)strtol(buf, (char **)NULL, base);
	      i = 0;
	    }
	  else i++;
	} 
      val = x;
    }
  else // Handle Floating Point & Scientific Notation
    {
      const char* start = s.c_str();
      char* endPtr = nullptr;
      
      // std::strtod natively parses "6.053984e-5" out of the box
      double parsedVal = std::strtod(start, &endPtr);
      
      // If endPtr advanced to the end of the string, it's a valid floating-point match!
      if (endPtr != start && *endPtr == '\0')
        {
          val = (NUMBER)parsedVal;
        }
      else
        {
          val = BAD_NUMBER;
        }
    }
}

bool NUMERICOBJ::Ok() const
{
  return val != BAD_NUMBER;
}

void Write(const NUMERICOBJ& s, FILE *Fp)
{
  ::Write(s.val, Fp); 
}

bool NUMERICOBJ::Read(FILE *Fp)
{
  if (::Read(&val, Fp))
    return true;
  val = BAD_NUMBER;
  return false;
}

bool Read(NUMERICOBJ *p, FILE *Fp) {
  if (p) return p->Read(Fp);
  return false;
}



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


NUMERICOBJ operator+(const NUMERICOBJ& val1, const DOUBLE val2)
{
  NUMERICOBJ val;

  val = val1;
  val += val2;
  return val;
}
NUMERICOBJ operator-(const NUMERICOBJ& val1, const DOUBLE val2)
{
  NUMERICOBJ val;

  val = val1;
  val -= val2;
  return val;
}
NUMERICOBJ operator*(const NUMERICOBJ& val1, const DOUBLE val2)
{
  NUMERICOBJ val;

  val = val1;
  val *= val2;
  return val;
}
NUMERICOBJ operator/(const NUMERICOBJ& val1, const DOUBLE val2)
{
  NUMERICOBJ val;

  val = val1;
  val /= val2;
  return val;
}


NUMERICALRANGE::NUMERICALRANGE()
{
  d_start = BAD_NUMBER;
  d_end   = BAD_NUMBER;
}

NUMERICALRANGE::NUMERICALRANGE(const STRING& RangeString)
{
  d_start = BAD_NUMBER;
  d_end   = BAD_NUMBER;
  SetRange(RangeString);
}

NUMERICALRANGE::~NUMERICALRANGE()
{
  ;
}

// Formats:
//   (x,y) [x,y] x..y x-y x:y x/y
// where x,y are floating point numbers
//

bool NUMERICALRANGE::SetRange(const STRING& RangeString)
{
#if 1
    double start = BAD_NUMBER;
    double end   = BAD_NUMBER;
    char tail[2]; 
    int fields = 0;

    const STRING range ( RangeString.Strip(STRING::both) );
    const char* tcp = range.c_str();

    if (!tcp  || *tcp == '\0') return false;
    // 1. PRE-SCREEN: Reject if we see Time/Date indicators like ':' or 'T' or 'Z'
    // This immediately kills cases like "12.12:0-30" or "2023-10-1T12:00"
    if (strpbrk(tcp, ":TZtz")) {
        return false;
    }

    switch(*tcp) {
        case '[': 
            fields = sscanf(tcp, "[%lf,%lf]%1s", &start, &end, tail);
            break;

        case '(': 
            fields = sscanf(tcp, "(%lf,%lf)%1s", &start, &end, tail);
            break;

        default:
            // 2. Try the ".." separator (Preferred)
            fields = sscanf(tcp, "%lf..%lf%1s", &start, &end, tail);
            
            // 3. Try the "-" separator (The tricky one)
            if (fields != 2) {
                // To support negative numbers, we use sscanf logic carefully.
                // This will match "10-20", "-10-20", or "10--20"
                fields = sscanf(tcp, "%lf-%lf%1s", &start, &end, tail);
            }

            // 4. Fallback: Try a single float
            if (fields != 2) {
                fields = sscanf(tcp, "%lf%1s", &start, tail);
                if (fields == 1) end = start;
            }
            break;
    }

    // 5. FINAL VALIDATION
    // fields == 1 means sscanf found one double and nothing else.
    // fields == 2 means sscanf found two doubles and nothing else.
    // If fields is 3, tail captured junk (like "123-456ABC"), so we reject.
    if (fields == 2) {
        d_start = start;
        d_end = end;
        return true;
    }

    return false;
#else
  int         fieldcount = 0;
  const char *tcp        = RangeString.c_str();
  DOUBLE      start      = BAD_NUMBER;
  DOUBLE      end        = BAD_NUMBER;
  char        tail[2];

  while (isspace(*tcp)) tcp++;  // Skip blanks

  switch(*tcp) {
    case '\0': /* Empty String */; break;
    case '[': fieldcount = sscanf(tcp,"[%lf,%lf]", &start, &end); break;
    case '(': fieldcount = sscanf(tcp,"(%lf,%lf)", &start, &end); break;
    default:
    if ((fieldcount = sscanf(tcp,"%lf..%lf%1s", &start, &end, tail)) == 1)
      {
	if (start == (double)(int)(start))
	  {
	    long istart, iend;
	    if ((fieldcount = sscanf(tcp,"%ld..%ld%1s", &istart, &iend, tail)) >= 2)
	      {
		if (fieldcount == 3) return false;
		end = iend;
		break;
	      }
	    else if ((fieldcount = sscanf(tcp,"%ld--%ld%1s", &istart, &iend, tail)) >= 2)
	     {
		if (fieldcount == 3) return false;
		end = iend;
		break;
	     }
	  }
	if (!isdigit(*tcp) && (*tcp != '.'))
	  break; // Not a number
	while(isdigit(*tcp)) tcp++;

	if (*tcp == 'T' || *tcp == 'Z') // Its a date!
	   return false;
	if (*tcp == '.') tcp++;
	while (isdigit(*tcp)) tcp++;
	// Now do we have another number?
	if (*tcp == '-' && (isdigit(tcp[1]) || (tcp[1] == '.' && isdigit(tcp[3]) )))
	  {
	    // Format:  Num-Num2
	    end = atof(tcp+1);
	    fieldcount = 2;
	    break;
	  }
	while (isspace(*tcp)) tcp++;
	if (*tcp == ','  || *tcp == ':' || *tcp == '/')
	  {
	    tcp++;
	    while (isspace(*tcp)) tcp++;
	    if ((isdigit(*tcp) || (*tcp == '.' && isdigit(tcp[1])))
			|| (*tcp == '-' && isdigit(tcp[1])))
	      {
		// Looking at number
		end = atof(tcp);
		fieldcount = 2;
		break;
	      }
	  }
      }
  }
  d_start = start;
  d_end   = end;
  return fieldcount == 2;
#endif
}


bool NUMERICALRANGE::Ok() const
{
  return d_start.val != BAD_NUMBER && d_end.val != BAD_NUMBER;
}

bool NUMERICALRANGE::Defined() const
{
  return d_start.val != BAD_NUMBER || d_end.val != BAD_NUMBER;
}

static const UINT4 CURRENCY_MODULO  = 50000;
static const UINT2 BAD_CURRENCY_VAL = 50001;


void MONETARYOBJ::Invalidate()
{
  Amount = 0;
  Fract  = BAD_CURRENCY_VAL;
}

//
// This is the class that handles monetary objects like prices and valuta.
// 
MONETARYOBJ::MONETARYOBJ ()
{
  Invalidate();
}

MONETARYOBJ::MONETARYOBJ (const STRING& s)
{
  Amount = 0;
  Fract  = BAD_CURRENCY_VAL;
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

  // Current representation is unsigned.
  if (*p == '-')
    return false;

  if (*p == '+')
    {
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
  UINT8 whole = 0;

  for (const unsigned char *q = p; q < integerEnd; )
    {
      if (MoneyDigit(*q))
        {
          const UINT8 digit = *q - '0';
          const UINT8 maxValue = ~(UINT8)0;

          if (whole > (maxValue - digit) / 10)
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
  const UINT8 scale = cents ? 500 : CURRENCY_MODULO;

  const UINT8 maxAmount = (UINT4)~(UINT4)0;
  const UINT8 maxTicks = maxAmount * (UINT8)CURRENCY_MODULO + (CURRENCY_MODULO - 1);

  if (whole > maxTicks / scale)
    return false;

  UINT8 ticks = whole * scale;

  if ((UINT8)fractionScaled > maxTicks - ticks)
    return false;

  ticks += fractionScaled;

  Amount = (UINT4)(ticks / CURRENCY_MODULO);
  Fract  = (UINT2)(ticks % CURRENCY_MODULO);

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


MONETARYOBJ::MONETARYOBJ (const NUMBER x)
{
  Set(x);
}

MONETARYOBJ::MONETARYOBJ (const NUMERICOBJ& x)
{
  Set((NUMBER)x);
}


#if 1

bool MONETARYOBJ::Set(const NUMBER x)
{
  Invalidate();

  // Current representation is unsigned: don't silently wrap debt/negative
  // amounts. Supporting negatives would require a representation change.
  if (x < 0) return false;

  // NaN/infinity should also never become prices.
  if (!std::isfinite((long double)x)) return false;

  const NUMBER whole = floorl(x);
  // MaxAmount >
  if (whole > (NUMBER)((UINT4)~(UINT4)0))
    return false;


  NUMBER f = (x - whole) * (double)CURRENCY_MODULO;
  UINT4 fract = (UINT4)floorl(f + 0.5L);

  UINT4 amount = (UINT4)whole;

  // Round to the nearest representable 1/50000.
  if (fract >= CURRENCY_MODULO)
    {
      if (amount == (UINT4)~(UINT4)0)
        return false;
      // 0.99999... may round to the next whole unit.
      ++amount;
      fract = 0;
    }

  Amount = amount;
  Fract  = (UINT2)fract;
  return true;
}

#else

bool MONETARYOBJ::Set (const NUMBER x)
{
  const long crowns = (long)x;

  Amount = (UINT4)crowns;
  Fract  = (UINT2)((x - crowns)*CURRENCY_MODULO);
  if (Amount == crowns) // It fits
    return true;
  Fract += BAD_CURRENCY_VAL;
  return false; // Does not fit
}
#endif

void MONETARYOBJ::Write(FILE *Fp) const
{
  ::Write(Amount, Fp);
  ::Write(Fract, Fp);
}

void Write(const MONETARYOBJ& s, FILE *Fp)
{
  s.Write(Fp);
}

#if 1
bool MONETARYOBJ::Read(FILE *Fp)
{
  Invalidate();

  UINT4 amount;
  UINT2 fract;

  ::Read(&amount, Fp);
  ::Read(&fract, Fp);

  if (Ok()) 
    {
      Amount = amount;
      Fract  = fract;
      return true;
    }
  return false;
}

#else
bool MONETARYOBJ::Read(FILE *Fp)
{
  Fract = BAD_CURRENCY_VAL;
  ::Read(&Amount, Fp);
  ::Read(&Fract, Fp);
  return Ok();
}
#endif
  

inline bool Read(MONETARYOBJ *p, FILE *Fp)
{
  if (p) return p->Read(Fp);
  return false;
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


bool INTEGEROBJ::Set(const STRING& s)
{
  valid = false;
  val = 0;

  const char *p   = s.c_str();
  const char *end = p + s.GetLength();

  while (p < end &&
         (*p == ' ' || (*p >= '\t' && *p <= '\r')))
    ++p;

  while (end > p &&
         (end[-1] == ' ' ||
          (end[-1] >= '\t' && end[-1] <= '\r')))
    --end;

  if (p == end)
    return false;

  // std::from_chars does not accept leading '+'.
  if (*p == '+') {
    if (++p == end)
      return false;
  }

  INT8 value;
  const auto result = std::from_chars(p, end, value, 10);

  if (result.ec != std::errc() || result.ptr != end)
    return false;

  val = value;
  valid = true;
  return true;
}
