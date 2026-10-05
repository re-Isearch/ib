
// integerfield.hxx

#ifndef INTEGERFIELD_HXX
#define INTEGERFIELD_HXX

#include <array>
#include <limits>

#include "gdt.h"
#include "defs.hxx"
#include "fc.hxx"

class INTEGERFLD
{
public:
  //
  // On-disk field geometry. Keep this alias as the single width knob.
  // A UINT2 span represents field lengths through 65536 bytes because
  // FC::Span() is end-start, not byte count.
  //
  using span_type = UINT2;

  static constexpr size_t VALUE_SIZE  = 16;
  static constexpr size_t SPAN_OFFSET = sizeof(GPTYPE) + VALUE_SIZE;
  static constexpr size_t DISK_SIZE   = SPAN_OFFSET + sizeof(span_type);

  INTEGERFLD()
    : Value(0), GlobalStart(0), Span(0), SpanValid(true)
  {
  }

  //
  // Retained for source compatibility. New index writers should pass FC so
  // the row can materialize its own hit. A GP-only row describes one byte.
  //
  INTEGERFLD(GPTYPE gp, INT16 value)
    : Value(value), GlobalStart(gp), Span(0), SpanValid(true)
  {
  }

  INTEGERFLD(const FC& fc, INT16 value)
    : Value(value),
      GlobalStart(fc.GetFieldStart()),
      Span(0),
      SpanValid(false)
  {
    const size_t span = fc.Span();

    if (!fc.IsEmpty() && span <= MaxSpan())
    {
      Span = static_cast<span_type>(span);
      SpanValid = true;
    }
  }

  GPTYPE    GetGlobalStart() const { return GlobalStart; }
  INT16     GetValue()       const { return Value; }
  span_type GetSpan()        const { return Span; }

  bool HasValidSpan() const { return SpanValid; }

  static constexpr size_t MaxSpan()
  {
    return static_cast<size_t>(std::numeric_limits<span_type>::max());
  }

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

  static span_type DecodeSpan(const BYTE *p)
  {
    span_type value = 0;

    for (size_t i = 0; i < sizeof(span_type); ++i)
      value = static_cast<span_type>(
          (value << 8) | static_cast<span_type>(p[SPAN_OFFSET + i]));

    return value;
  }

  bool Write(FILE *fp) const
  {
    if (fp == NULL || !SpanValid)
      return false;

    const UINT16 bits = (UINT16)Value;

    return ::Write(GlobalStart, fp) == (int)sizeof(GPTYPE) &&
           ::Write(UINT16_high_part(bits), fp) == 8 &&
           ::Write(UINT16_low_part(bits),  fp) == 8 &&
           ::Write(Span, fp) == (int)sizeof(span_type);
  }

  bool Write(FILE *fp, GPTYPE Offset) const
  {
    INTEGERFLD tmp(*this);
    tmp.GlobalStart += Offset;
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
    Span        = DecodeSpan(buf.data());
    SpanValid   = true;

    return true;
  }

private:
  //
  // Member order deliberately keeps the in-memory object compact when INT16
  // has 16-byte alignment. It does not define the on-disk layout.
  //
  INT16     Value;
  GPTYPE    GlobalStart;
  span_type Span;
  bool      SpanValid;
};

#endif
