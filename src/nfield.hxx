#ifndef NUMERICFLD_HXX
#define NUMERICFLD_HXX

#include "defs.hxx"
#include "numbers.hxx"
#include "magic.hxx"

class NUMERICFLD {
public:
  using span_type = UINT2;
  static constexpr size_t DISK_SIZE = 1 + sizeof(GPTYPE) + 16;
  NUMERICFLD() : GlobalStart(0) {}
  NUMERICFLD(GPTYPE gp, const NUMERICOBJ& value) : GlobalStart(gp), NumericValue(value) {}
  operator STRING() const { return NumericValue.ToString(); }
  NUMERICFLD& operator+=(GPTYPE offset) { IncrementStart(offset); return *this; }
  void IncrementStart(GPTYPE offset) { GlobalStart += offset; }
  GPTYPE GetGlobalStart() const { return GlobalStart; }
  void SetGlobalStart(GPTYPE gp) { GlobalStart = gp; }
  NUMERICOBJ GetNumericValue() const { return NumericValue; }
  void SetNumericValue(const NUMERICOBJ& value) { NumericValue = value; }
  size_t Sizeof() const { return DISK_SIZE; }
  void Dump(ostream& os = cout) const;
  bool Write(FILE *fp) const;
  bool Write(FILE *fp, GPTYPE offset) const;
  bool Read(FILE *fp);
  static GPTYPE DecodeGlobalStart(const BYTE *p) { return getGPTYPE(p, 1); }
  static INT16 DecodeValue(const BYTE *p);
  friend ostream& operator<<(ostream& os, const NUMERICFLD& field);
private:
  GPTYPE GlobalStart;
  NUMERICOBJ NumericValue;
};

typedef NUMERICFLD* PNUMERICFLD;
void Write(const NUMERICFLD& field, FILE *fp);
void Read(NUMERICFLD *field, FILE *fp);
#endif
