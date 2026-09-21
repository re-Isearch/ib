/*
Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
#include <stdlib.h>
#include "common.hxx"
#include "numbers.hxx"

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


bool MONETARYOBJ::Set(const STRING& s)
{
  Amount = 0;
  Fract  = BAD_CURRENCY_VAL;

  const unsigned char *p =
      (const unsigned char *)s.c_str();
  const unsigned char *end = p + s.GetLength();

  while (p < end && isspace(*p))
    ++p;

  while (end > p && isspace(end[-1]))
    --end;

  if (p == end)
    return false;

  const unsigned char money = 164; // currency sign / Euro in ISO-8859-15
  const unsigned char yen   = 165;
  const unsigned char pound = 163;
  const unsigned char cent  = 162;

  // Optional leading currency sign.
  if (*p == '$' || *p == money || *p == yen || *p == pound)
    {
      ++p;
      while (p < end && isspace(*p))
        ++p;
    }

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

#else

bool MONETARYOBJ::Set(const STRING& s)
{
  const char *ptr = s.c_str();

  while (isspace(*ptr)) ptr++;

  const size_t len  = strlen(ptr);

  // Largest number: 18446744073709551615
  if (len < 2 || len >= 64) return false;

  char dup[64];

  memcpy(dup, ptr, len+1);

  char *tcp = dup + len; // End of field
  const char  money = (char)164; // Also EURO in 8859-15
  const char  yen   = (char)165;
  const char  pound = (char)163;
  const char  cent  = (char)162;
  size_t cents = 0;

  ptr = dup;
  if (*ptr == '$' || *ptr == yen || *ptr == pound || *ptr == money) ptr++;
  while (isspace(*ptr)) ptr++;

  for ( ; *tcp != '.' && *tcp != ',' && tcp >= ptr ; tcp--)
    {
      if (*tcp == cent)
	{
	  cents++;
          break; // value is in cents
	}
    }
  {long double rest;
  if ((*tcp == '.' || *tcp == ',') && *(tcp+1) && (rest = (long double)atof (tcp + 1)) < 100.0)
    {
      long double r = rest*CURRENCY_MODULO;
      // 50000 = 100 cents -> 500 = 1 cent
      // 5000*x = 0.1
      Fract = (UINT2)r;
      *tcp = '\0';
    }
  else
    Fract = 0;
  }

  if ((Amount = atol (ptr)) == 0)
    {
       if (!_ib_isdigit(*ptr))
	Fract = BAD_CURRENCY_VAL;
    }
  else if (Fract == 0 && cents)
    {
      Set( Amount/100.0 );
    }
  return Ok();
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

