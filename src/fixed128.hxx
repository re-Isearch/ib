#ifndef FIXED128_HXX
#define FIXED128_HXX

#include "defs.hxx"

namespace fixed128 {

inline UINT16 Magnitude(INT16 value)
{
  const UINT16 bits = (UINT16)value;
  return value < 0 ? (~bits) + 1 : bits;
}

inline bool Signed(UINT16 magnitude, bool negative, INT16 *result)
{
  const UINT16 sign = UINT16(1) << 127;
  if (magnitude > (negative ? sign : sign - 1)) return false;
  *result = negative && magnitude == sign ? -INT16(sign - 1) - 1 :
            negative ? -INT16(magnitude) : INT16(magnitude);
  return true;
}

struct Wide { UINT16 high, low; };

// Four 64-bit limbs provide the full product without signed overflow.
inline Wide Multiply(UINT16 a, UINT16 b)
{
  const UINT8 av[] = {UINT8(a), UINT8(a >> 64)};
  const UINT8 bv[] = {UINT8(b), UINT8(b >> 64)};
  UINT8 limbs[4] = {};
  for (unsigned i = 0; i < 2; ++i)
    {
      UINT16 carry = 0;
      for (unsigned j = 0; j < 2; ++j)
        {
          const UINT16 sum = UINT16(av[i]) * bv[j] + limbs[i+j] + carry;
          limbs[i+j] = UINT8(sum);
          carry = sum >> 64;
        }
      limbs[i+2] = UINT8(carry);
    }
  return {cons_UINT16(limbs[3], limbs[2]), cons_UINT16(limbs[1], limbs[0])};
}

inline bool Divide(Wide value, UINT16 divisor, UINT16 *quotient, UINT16 *remainder)
{
  if (!divisor || value.high >= divisor) return false;
  if (!value.high)
    {
      *quotient = value.low / divisor;
      *remainder = value.low % divisor;
      return true;
    }
  UINT16 q = 0, r = value.high;
  for (int bit = 127; bit >= 0; --bit)
    {
      const bool carry = (r >> 127) != 0;
      r = (r << 1) | ((value.low >> bit) & 1);
      if (carry || r >= divisor)
        {
          r -= divisor;
          q |= UINT16(1) << bit;
        }
    }
  *quotient = q;
  *remainder = r;
  return true;
}

inline bool MulDiv(INT16 a, INT16 b, UINT16 divisor, INT16 *result)
{
  UINT16 quotient, remainder;
  if (!Divide(Multiply(Magnitude(a), Magnitude(b)), divisor, &quotient, &remainder))
    return false;
  const UINT16 half = divisor / 2;
  if (remainder > half || (!(divisor & 1) && remainder == half && (quotient & 1)))
    {
      if (quotient == ~UINT16(0)) return false;
      ++quotient;
    }
  return Signed(quotient, (a < 0) != (b < 0), result);
}

} // namespace fixed128

#endif
