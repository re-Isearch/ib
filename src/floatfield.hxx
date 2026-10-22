#ifndef FLOATFIELD_HXX
#define FLOATFIELD_HXX

#include "defs.hxx"
#include "magic.hxx"
#include <cmath>
#include <limits>

static_assert(sizeof(DOUBLE) == 8 && std::numeric_limits<DOUBLE>::is_iec559 &&
              std::numeric_limits<DOUBLE>::digits == 53 &&
              std::numeric_limits<DOUBLE>::max_exponent == 1024,
              "FLOAT64 columns require IEEE 754 binary64 doubles");

namespace float64 {
bool Parse(const STRING& text, DOUBLE *value);
// Shortest round-trip decimal; numerically equal signed zeros share "0".
STRING ToString(DOUBLE value);
}

class FLOATFLD {
public:
  using span_type = UINT2;
  static constexpr size_t DISK_SIZE = 1 + sizeof(GPTYPE) + 8;
  FLOATFLD() : GlobalStart(0), Value(std::numeric_limits<DOUBLE>::quiet_NaN()) {}
  FLOATFLD(GPTYPE gp, DOUBLE value) : GlobalStart(gp), Value(value) {}
  GPTYPE GetGlobalStart() const { return GlobalStart; }
  DOUBLE GetValue() const { return Value; }
  void IncrementStart(GPTYPE offset) { GlobalStart += offset; }
  bool Write(FILE *fp) const;
  bool Write(FILE *fp, GPTYPE offset) const;
  bool Read(FILE *fp);
  static GPTYPE DecodeGlobalStart(const BYTE *p) { return getGPTYPE(p, 1); }
  static DOUBLE DecodeValue(const BYTE *p);
private:
  GPTYPE GlobalStart;
  DOUBLE Value;
};

#endif
