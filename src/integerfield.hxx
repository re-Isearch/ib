

// integerfield.hxx

#ifndef INTEGERFIELD_HXX
#define INTEGERFIELD_HXX

#include <array>

#include "gdt.h"
#include "defs.hxx"

class INTEGERFLD
{
public:
  static constexpr size_t DISK_SIZE = sizeof(GPTYPE) + 16;

  INTEGERFLD() : GlobalStart(0), Value(0) {}

  INTEGERFLD(GPTYPE gp, INT16 value)
    : GlobalStart(gp), Value(value)
  {
  }

  GPTYPE GetGlobalStart() const { return GlobalStart; }
  INT16  GetValue()       const { return Value; }

  void SetGlobalStart(GPTYPE gp) { GlobalStart = gp; }
  void SetValue(INT16 value)     { Value = value; }

  static GPTYPE DecodeGlobalStart(const BYTE *p)
  {
    return getGPTYPE(p, 0);
  }

  static INT16 DecodeValue(const BYTE *p)
  {
    const UINT8 hi = getINT8(p, sizeof(GPTYPE));
    const UINT8 lo = getINT8(p, sizeof(GPTYPE) + 8);

    const UINT16 bits = cons_UINT16(hi, lo);
    const UINT16 sign = ((UINT16)1 << 127);

    if ((bits & sign) == 0)
      return (INT16)bits;

    const UINT16 magnitude = (~bits) + 1;

    if (magnitude == sign)
      return -((INT16)(sign - 1)) - 1;

    return -(INT16)magnitude;
  }

  bool Write(FILE *fp) const
  {
    if (fp == NULL)
      return false;

    const UINT16 bits = (UINT16)Value;

    return ::Write(GlobalStart, fp) == (int)sizeof(GPTYPE) &&
           ::Write(UINT16_high_part(bits), fp) == 8 &&
           ::Write(UINT16_low_part(bits),  fp) == 8;
  }

  bool Write(FILE *fp, GPTYPE Offset) const
  {
    INTEGERFLD tmp(GlobalStart + Offset, Value);
    return tmp.Write(fp);
  }

  bool Read(FILE *fp)
  {
    if (fp == NULL)
      return false;

    std::array<BYTE, DISK_SIZE> buf;

    if (fread(buf.data(), 1, buf.size(), fp) != buf.size())
      return false;

    GlobalStart = DecodeGlobalStart(buf.data());
    Value       = DecodeValue(buf.data());

    return true;
  }

private:
  GPTYPE GlobalStart;
  INT16  Value;
};

#endif
