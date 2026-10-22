#ifndef TYPED_SCAN_HXX
#define TYPED_SCAN_HXX

#include "attrlist.hxx"
#include "mappedorderedlist.hxx"
#include "numbers.hxx"
#include "floatfield.hxx"
#include "scan.hxx"

namespace typed_scan {

inline void Append(STRLIST *output, const STRING& term, size_t)
{
  output->AddEntry(term);
}

inline void Append(SCANLIST *output, const STRING& term, size_t frequency)
{
  output->FastAddEntry(term, frequency);
}

// The datatype owns its decoder and formatter; no floating-point conversion
// is needed to group values, count occurrences or create scan terms.
template <class Traits, class Db, class Output, class Format>
size_t ScanField(Db *parent, Output *output, const STRING& field,
                 const FIELDTYPE& expected, size_t start, INT requested,
                 MultiMMapSession& sessions, Format&& format)
{
  if (!parent || !output || requested == 0 || parent->GetFieldType(field) != expected)
    return 0;

  STRING filename;
  if (!parent->DfdtGetFileName(field, expected, &filename) || filename.IsEmpty())
    {
      parent->SetErrorCode(123); // Unsupported attribute combination.
      return 0;
    }

  size_t total = 0;
  MAPPEDORDEREDLIST<Traits> values;
  if (!values.VisitMappedValues(filename, start, requested, sessions,
      [&](typename Traits::value_type value, size_t frequency)
      {
        Append(output, format(value), frequency);
        ++total;
      }))
    parent->SetErrorCode(123);
  return total;
}

struct NumericTerm {
  STRING operator()(INT16 ticks) const { return NUMERICOBJ::FromTicks(ticks).ToString(); }
};

struct Float64Term {
  STRING operator()(DOUBLE value) const { return float64::ToString(value); }
};

struct IntegerTerm {
  STRING operator()(INT16 value) const { return STRING(value); }
};

struct CurrencyTerm {
  STRING operator()(INT16 ticks) const {
    MONETARYOBJ value;
    value.SetTicks(ticks);
    return value.ToString();
  }
};

} // namespace typed_scan

#endif
