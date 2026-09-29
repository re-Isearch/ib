// integerfield.hxx

#ifndef INTEGERFIELD_HXX
#define INTEGERFIELD_HXX

#include "gdt.h"
#include "defs.hxx"

class INTEGERFLD
{
public:
  INTEGERFLD() : GlobalStart(0), Value(0) {}

  INTEGERFLD(GPTYPE gp, INT16 value)
    : GlobalStart(gp), Value(value)
  {
  }

  GPTYPE GetGlobalStart() const { return GlobalStart; }
  INT16  GetValue()       const { return Value; }

  void SetGlobalStart(GPTYPE gp) { GlobalStart = gp; }
  void SetValue(INT16 value)     { Value = value; }

private:
  GPTYPE GlobalStart;
  INT16  Value;
};

#endif
