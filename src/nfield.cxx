#include "nfield.hxx"
#include "fixed128.hxx"
#include "common.hxx"
#include <array>

void NUMERICFLD::Dump(ostream& os) const { os << *this << endl; }
ostream& operator<<(ostream& os, const NUMERICFLD& field) {
  return os << "start: " << field.GlobalStart << " value: " << field.NumericValue;
}
INT16 NUMERICFLD::DecodeValue(const BYTE *p)
{
  const UINT16 bits = cons_UINT16(getINT8(p, 1+sizeof(GPTYPE)), getINT8(p, 9+sizeof(GPTYPE)));
  const UINT16 sign = UINT16(1) << 127;
  INT16 value = 0;
  fixed128::Signed((bits & sign) ? (~bits)+1 : bits, (bits & sign) != 0, &value);
  return value;
}
bool NUMERICFLD::Write(FILE *fp) const
{
  if (!fp || !NumericValue.Ok()) return false;
  const UINT16 bits = UINT16(NumericValue.GetTicks());
  putObjID(objNUMFLD, fp);
  return ::Write(GlobalStart, fp) == int(sizeof(GPTYPE)) &&
         ::Write(UINT16_high_part(bits), fp) == 8 && ::Write(UINT16_low_part(bits), fp) == 8;
}
bool NUMERICFLD::Write(FILE *fp, GPTYPE offset) const
{
  NUMERICFLD copy(*this); copy.IncrementStart(offset); return copy.Write(fp);
}
bool NUMERICFLD::Read(FILE *fp)
{
  std::array<BYTE, DISK_SIZE> bytes;
  NumericValue = NUMERICOBJ();
  if (!fp || fread(bytes.data(), 1, bytes.size(), fp) != bytes.size() || bytes[0] != objNUMFLD) return false;
  GlobalStart = DecodeGlobalStart(bytes.data());
  NumericValue.SetTicks(DecodeValue(bytes.data()));
  return true;
}
void Write(const NUMERICFLD& field, FILE *fp) { field.Write(fp); }
void Read(NUMERICFLD *field, FILE *fp) { field->Read(fp); }
