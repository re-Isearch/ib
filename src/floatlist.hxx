#ifndef FLOATLIST_HXX
#define FLOATLIST_HXX

#include "floatfield.hxx"
#include "mappedorderedlist.hxx"
#include "fc.hxx"

struct FLOAT_INDEX_TRAITS {
  using value_type = DOUBLE;
  using field_type = FLOATFLD;
  using span_type = field_type::span_type;
  struct mapped_type {
    const BYTE *value_block = nullptr, *gp_block = nullptr;
    size_t count = 0;
  };
  static constexpr const char *Name() { return "FLOAT64"; }
  static bool Less(value_type a, value_type b) { return a < b; }
  static bool Equal(value_type a, value_type b) { return a == b; }
  static value_type Value(const field_type& field) { return field.GetValue(); }
  static GPTYPE GlobalStart(const field_type& field) { return field.GetGlobalStart(); }
  static value_type MappedValueAt(const BYTE *block, size_t i) { return field_type::DecodeValue(block+i*field_type::DISK_SIZE); }
  static GPTYPE MappedGlobalStartAt(const BYTE *block, size_t i) { return field_type::DecodeGlobalStart(block+i*field_type::DISK_SIZE); }
  // Full hit coordinates remain in the field coordinate cache.
  static span_type MappedSpanAt(const BYTE *, size_t) { return 0; }
  static FILE *OpenForAppend(const STRING&);
  static bool LoadRawBlock(const STRING&, std::vector<field_type>*);
  static bool LoadValueBlock(const STRING&, std::vector<field_type>*);
  static bool MapIndexed(const STRING&, MultiMMapSession&, mapped_type*);
  static bool WriteIndex(const STRING&, const std::vector<field_type>&, const std::vector<field_type>&);
};

class FLOATLIST : public MAPPEDORDEREDLIST<FLOAT_INDEX_TRAITS> {
public:
  bool LoadGpBlock(const STRING& filename);
  bool ReadValue(const STRING& filename, const FC& field, DOUBLE *value) const;
};

#endif
