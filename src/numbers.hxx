/*
Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
#ifndef NUMBERS_HXX
# define NUMBERS_HXX 1

#include "defs.hxx"
#include <type_traits>

class NUMERICOBJ {
 friend class NUMERICALRANGE;
public:
  static constexpr unsigned FRACTION_DIGITS = 16;
  static constexpr UINT8 TICKS_PER_UNIT = UINT8(10000000000000000ULL);
  static constexpr size_t DISK_SIZE = 17;

  NUMERICOBJ() : Ticks(0), Valid(false) {}
  NUMERICOBJ(const STRING& s) : NUMERICOBJ() { Set(s); }
  NUMERICOBJ(NUMBER x) : NUMERICOBJ() { Set(x); }
  NUMERICOBJ(INT16 x) : NUMERICOBJ() { SetWhole(x); }
  NUMERICOBJ(UINT16 x) : NUMERICOBJ() {
    if (x <= UINT16(MAX_INT16) / TICKS_PER_UNIT) SetTicks(INT16(x * TICKS_PER_UNIT));
  }
  template <class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  NUMERICOBJ(T x) : NUMERICOBJ() { SetWhole(INT16(x)); }
  template <class T, typename std::enable_if<std::is_floating_point<T>::value, int>::type = 0>
  NUMERICOBJ(T x) : NUMERICOBJ() { Set(NUMBER(x)); }

  bool Ok() const { return Valid; }
  bool Set(const STRING& s);
  bool Set(NUMBER x);
  bool SetWhole(INT16 x);
  bool SetTicks(INT16 ticks) { Ticks = ticks; Valid = true; return true; }
  INT16 GetTicks() const { return Ticks; }
  bool IsIntegral() const { return Valid && Ticks % TICKS_PER_UNIT == 0; }
  STRING ToString() const;
  static NUMERICOBJ FromTicks(INT16 ticks) { NUMERICOBJ x; x.SetTicks(ticks); return x; }
  static NUMERICOBJ FromScaled(INT16 ticks, UINT8 scale);

  NUMERICOBJ& operator=(const NUMERICOBJ&) = default;
  NUMERICOBJ& operator+=(const NUMERICOBJ& x);
  NUMERICOBJ& operator-=(const NUMERICOBJ& x);
  NUMERICOBJ& operator*=(const NUMERICOBJ& x);
  NUMERICOBJ& operator/=(const NUMERICOBJ& x);

  friend NUMERICOBJ operator+(const NUMERICOBJ& val1,  const NUMERICOBJ& val2);
  friend NUMERICOBJ operator-(const NUMERICOBJ& val1,  const NUMERICOBJ& val2);
  friend NUMERICOBJ operator*(const NUMERICOBJ& val1,  const NUMERICOBJ& val2);
  friend NUMERICOBJ operator/(const NUMERICOBJ& val1,  const NUMERICOBJ& val2);

  explicit operator long double() const { return Valid ? (long double)Ticks / TICKS_PER_UNIT : 0; }
  explicit operator double() const { return (double)(long double)*this; }
  explicit operator int() const { return Valid ? int(Ticks / TICKS_PER_UNIT) : 0; }
  explicit operator long() const { return Valid ? long(Ticks / TICKS_PER_UNIT) : 0; }
  explicit operator INT16() const { return Valid ? Ticks / TICKS_PER_UNIT : 0; }

  bool operator==(const NUMERICOBJ& x) const { return Valid == x.Valid && Ticks == x.Ticks; }
  bool operator!=(const NUMERICOBJ& x) const { return !(*this == x); }
  bool operator>=(const NUMERICOBJ& x) const { return Valid && x.Valid && Ticks >= x.Ticks; }
  bool operator<=(const NUMERICOBJ& x) const { return Valid && x.Valid && Ticks <= x.Ticks; }
  bool operator>(const NUMERICOBJ& x) const { return Valid && x.Valid && Ticks > x.Ticks; }
  bool operator<(const NUMERICOBJ& x) const { return Valid && x.Valid && Ticks < x.Ticks; }

  void        Write(FILE *Fp) const;
  bool Read(FILE *Fp);

  friend void Write(const NUMERICOBJ& s, FILE *Fp);
  friend bool Read(NUMERICOBJ *p, PFILE Fp);

  // STD IO Streams
  friend ostream& operator <<(ostream& os, const NUMERICOBJ& val) { return os << val.ToString(); }
  friend istream& operator >>(istream& os, NUMERICOBJ& val) {
	STRING value; istream& res = os >> value; val.Set(value); return res; }

private:
  void Invalidate() { Ticks = 0; Valid = false; }
  INT16 Ticks;
  bool Valid;
};

void Write(const NUMERICOBJ& value, FILE *fp);
bool Read(NUMERICOBJ *value, FILE *fp);

#if 0

/* Telephone numbers?

UK:
Min. 10 or 11 digits (the specification indicates 11 digits, but the author knows of some 10 digit numbers)
Valid full UK telephone numbers must start with a 0

Telephone Formats:

  +Code Number
 (+Code) Number
  0Code Number
  00Code Number
  Number

Number Formats:
  (nnn) nnn-nnnn
  nnn-nnn-nnnn

 where - can be space or no-space or -

0 may be prefixed

Extention:
  #nnnnn

*/
class  TELEPHONE_NUM {
public:

  TELEPHONE_NUM ()

private:
  UINT4 CountryCode;
  UINT4 AreaCode;
  UINT8 Number;
  UINT4 Extension;
};

/*
CARD TYPE 	Prefix 	Length 	Check digit algorithm
MASTERCARD	51-55	16 	mod 10
VISA	4	13, 16 	mod 10
AMEX	34
37	15 	mod 10
Diners Club/
Carte Blanche	300-305
36
38 	14	mod 10
Discover	6011	16 	mod 10
enRoute	2014
2149 	15	any
JCB	3	16 	mod 10
JCB	2131
1800	15 	mod 10
*/

class CREDITCARD_NUM {


}


/* IBAN: LLPZIIDBAN

2-place, alphabetical country code (LL)
2-place, numberical checksum (PZ) over the entire IBAN 
Maximal 30-place Basic Bank Account Number (BBAN):
	Bank Identification (IID) Account id (BAN)

Name                                                                Example 
Country Code	alpha constant                                        DE
Checksun        PZ        2-place numeric, Modulus 97-10 (ISO 7064)   21
Bank Id         IID  BLZ  Konstant 8-place alphanumeric               30120400
Account Id      BAN  KTO  Konstant 10-stellig (with leading zeros)    15228


*/


#endif


class NUMERICALRANGE
{
public:
  NUMERICALRANGE();
  NUMERICALRANGE(const NUMERICOBJ& Start, const NUMERICOBJ& End) {
    d_start = Start;
    d_end   = End;
  }
  NUMERICALRANGE(const NUMERICOBJ& NewPoint) {
    *this = NewPoint;
  }
  NUMERICALRANGE(const STRING& RangeString);

  // Setting
  bool SetRange(const STRING& RangeString);
  bool SetRange(const NUMERICOBJ& Start, const NUMERICOBJ& End) {
    d_start = Start;
    d_end   = End;
    return Ok();
  }

  NUMERICALRANGE& operator  =(const NUMERICALRANGE& Range) {
    d_start = Range.d_start;
    d_end   = Range.d_end;
    return *this;
  }
  // Only set when the string makes sense!
  NUMERICALRANGE& operator  =(const STRING& RangeString) {
    const NUMERICALRANGE Range(RangeString);
    if (Range.Ok()) *this = Range; // Set only if OK
    return *this;
  }
  // Arithmetic
  NUMERICALRANGE& operator +=(const NUMERICALRANGE& Range) {
   d_start += Range.d_start;
   d_end   += Range.d_end;
   return *this;
  }
  NUMERICALRANGE& operator -=(const NUMERICALRANGE& Range) {
    d_start -= Range.d_start;
    d_end   -= Range.d_end;
    return *this;
  }
  NUMERICALRANGE& operator ++() {return *this += NUMERICOBJ(1); }
  NUMERICALRANGE& operator --() {return *this -= NUMERICOBJ(1); }

  // typecasts
  operator    STRING () const;

  friend ostream &operator << (ostream &os, const NUMERICALRANGE &dt);
  friend STRING  &operator << (STRING &String, const NUMERICALRANGE &dt); 
 
  NUMERICOBJ  GetStart() const { return d_start; }
  NUMERICOBJ  GetEnd()   const { return d_end; }

  void  SetStart(const NUMERICOBJ& NewStart) { d_start = NewStart; }
  void  SetEnd(const NUMERICOBJ& NewEnd)  { d_end = NewEnd; }

  bool Ok() const;
  bool Defined() const;
  bool Contains(const NUMERICOBJ& Test) const;
  bool Contains(const NUMERICALRANGE& OtherRange) const;

  void        Clear()  { d_start = NUMERICOBJ(); d_end = NUMERICOBJ(); }

  // Comparison operators
  bool operator ==(const NUMERICALRANGE& OtherRange) const;
  bool operator !=(const NUMERICALRANGE& OtherRange) const;
  bool operator  >(const NUMERICALRANGE& OtherRange) const;
  bool operator  <(const NUMERICALRANGE& OtherRange) const;
  bool operator >=(const NUMERICALRANGE& OtherRange) const;
  bool operator <=(const NUMERICALRANGE& OtherRange) const;

  // I/O
  bool Read(PFILE Fp);
  void Write(PFILE Fp) const;
  
  ~NUMERICALRANGE();
  
private:
  NUMERICOBJ d_start;
  NUMERICOBJ d_end;
};
  
  
// Common functions....
inline void Write(const NUMERICALRANGE& Range, FILE *Fp)  { Range.Write(Fp);         }
inline bool Read(NUMERICALRANGE *Range, FILE *Fp)  { return Range->Read(Fp);  }


/*
                    +----------------+
STRING ------------>|                |
NUMERICOBJ -------->| Set(NUMBER)    |----> valid positive MONETARYOBJ
NUMBER ------------>|                |
computed callback ->|                |
                    +----------------+
                           ^
                           |
                   Set(STRING) parses
                   syntax then calls it
*/


class MONETARYOBJ {
public:
  enum { TICKS_PER_UNIT = 50000 };

  MONETARYOBJ();
  MONETARYOBJ(const STRING& s);
  MONETARYOBJ(const NUMBER x);
  MONETARYOBJ(const NUMERICOBJ& x);

  bool Ok() const { return Valid; }

  bool Set(const NUMBER x);
  bool Set(const STRING& s);
  bool SetTicks(INT16 ticks, bool fractionSpecified = true) {
    Ticks = ticks;
    Valid = true;
    ExplicitFraction = fractionSpecified;
    return true;
  }

  INT16 GetTicks() const { return Ticks; }
  INT16 GetWholePart() const {
    return Valid ? Ticks / (INT16)TICKS_PER_UNIT : 0;
  }
  bool HasExplicitFraction() const {
    return Valid && ExplicitFraction;
  }

  MONETARYOBJ& operator=(const MONETARYOBJ& other) {
    Ticks = other.Ticks;
    Valid = other.Valid;
    ExplicitFraction = other.ExplicitFraction;
    return *this;
  }
  MONETARYOBJ& operator=(const NUMBER x) {
    Set(x);
    return *this;
  }

  operator long double() const {
    return Valid ? (long double)Ticks / (long double)TICKS_PER_UNIT : 0.0L;
  }
  operator NUMERICOBJ() const {
    return Valid ? NUMERICOBJ::FromScaled(Ticks, TICKS_PER_UNIT) : NUMERICOBJ();
  }
  operator double() const { return (double)((long double)*this); }
  operator int() const { return (int)GetWholePart(); }
  operator long() const { return (long)GetWholePart(); }

  bool RoundToNearest100th();
  // Exact decimal with 2-5 fraction digits; whole amounts retain ".00" so
  // scan terms select exact values rather than whole-unit search buckets.
  STRING ToString() const;

  bool operator==(const MONETARYOBJ& other) const { return Ticks == other.Ticks; }
  bool operator!=(const MONETARYOBJ& other) const { return Ticks != other.Ticks; }
  bool operator>=(const MONETARYOBJ& other) const { return Ticks >= other.Ticks; }
  bool operator<=(const MONETARYOBJ& other) const { return Ticks <= other.Ticks; }
  bool operator> (const MONETARYOBJ& other) const { return Ticks >  other.Ticks; }
  bool operator< (const MONETARYOBJ& other) const { return Ticks <  other.Ticks; }

  void Write(FILE *Fp) const;
  bool Read(FILE *Fp);

  friend void Write(const MONETARYOBJ& s, FILE *Fp);
  friend bool Read(MONETARYOBJ *p, PFILE Fp);

  friend ostream& operator<<(ostream& os, const MONETARYOBJ& val) {
    return os << (long double)val;
  }
  friend istream& operator>>(istream& os, MONETARYOBJ& val) {
    STRING tmp;
    istream& out = os >> tmp;
    val = MONETARYOBJ(tmp);
    return out;
  }

private:
  void Invalidate();

  INT16 Ticks;
  bool  Valid;
  bool  ExplicitFraction;
};


class MONETARYRANGE
{
public:
  MONETARYRANGE() = default;
  MONETARYRANGE(const MONETARYOBJ& Start, const MONETARYOBJ& End)
    : d_start(Start), d_end(End) {}
  explicit MONETARYRANGE(const STRING& RangeString) { SetRange(RangeString); }

  bool SetRange(const STRING& RangeString);
  bool SetRange(const MONETARYOBJ& Start, const MONETARYOBJ& End) {
    if (!Start.Ok() || !End.Ok()) return false;
    d_start = Start;
    d_end = End;
    return true;
  }

  MONETARYOBJ GetStart() const { return d_start; }
  MONETARYOBJ GetEnd() const { return d_end; }
  void SetStart(const MONETARYOBJ& Start) { d_start = Start; }
  void SetEnd(const MONETARYOBJ& End) { d_end = End; }

  bool Ok() const { return d_start.Ok() && d_end.Ok(); }
  bool Defined() const { return d_start.Ok() || d_end.Ok(); }
  bool Contains(const MONETARYOBJ& Test) const;
  void Clear() { d_start = MONETARYOBJ(); d_end = MONETARYOBJ(); }

private:
  MONETARYOBJ d_start;
  MONETARYOBJ d_end;
};


class INTEGEROBJ {
public:
  INTEGEROBJ() : val(0), valid(false) {}
  INTEGEROBJ(INT16 x) : val(x), valid(true) {}
  INTEGEROBJ(const STRING& s) : val(0), valid(false) { Set(s); }

  bool Set(const STRING& s);
  bool Set(INT16 x) {
    val = x;
    valid = true;
    return true;
  }

  bool Ok() const { return valid; }

  operator INT16() const { return val; }

  bool operator==(const INTEGEROBJ& x) const { return val == x.val; }
  bool operator!=(const INTEGEROBJ& x) const { return val != x.val; }
  bool operator< (const INTEGEROBJ& x) const { return val <  x.val; }
  bool operator<=(const INTEGEROBJ& x) const { return val <= x.val; }
  bool operator> (const INTEGEROBJ& x) const { return val >  x.val; }
  bool operator>=(const INTEGEROBJ& x) const { return val >= x.val; }

private:
  INT16 val;
  bool valid;
};


class INTEGERRANGE
{
public:
  INTEGERRANGE() = default;

  INTEGERRANGE(const INTEGEROBJ& Start, const INTEGEROBJ& End)
    : d_start(Start), d_end(End)
  {
  }

  explicit INTEGERRANGE(const STRING& RangeString)
  {
    SetRange(RangeString);
  }

  bool SetRange(const STRING& RangeString);

  bool SetRange(const INTEGEROBJ& Start, const INTEGEROBJ& End)
  {
    if (!Start.Ok() || !End.Ok())
      return false;

    d_start = Start;
    d_end   = End;
    return true;
  }

  INTEGEROBJ GetStart() const { return d_start; }
  INTEGEROBJ GetEnd()   const { return d_end; }

  void SetStart(const INTEGEROBJ& Start) { d_start = Start; }
  void SetEnd(const INTEGEROBJ& End)     { d_end = End; }

  bool Ok() const
  {
    return d_start.Ok() && d_end.Ok();
  }

  bool Defined() const
  {
    return d_start.Ok() || d_end.Ok();
  }

  bool Contains(const INTEGEROBJ& Test) const;

  void Clear()
  {
    d_start = INTEGEROBJ();
    d_end   = INTEGEROBJ();
  }

private:
  INTEGEROBJ d_start;
  INTEGEROBJ d_end;
};

#endif
