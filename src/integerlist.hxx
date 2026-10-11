// integerlist.hxx

#ifndef INTEGERLIST_HXX
#define INTEGERLIST_HXX

#include <algorithm>
#include <vector>

#include "mappedorderedlist.hxx"
#include "integerfield.hxx"
#include "mmap.hxx"

struct INTEGER_INDEX_TRAITS
{
  using value_type = INT16;
  using field_type = INTEGERFLD;
  using span_type  = field_type::span_type;

  struct mapped_type
  {
    const BYTE *value_block = nullptr;
    const BYTE *gp_block    = nullptr;
    size_t      count       = 0;
  };

  static constexpr const char *Name()
  {
    return "INTEGER";
  }

  static bool Less(value_type a, value_type b)
  {
    return a < b;
  }

  static bool Equal(value_type a, value_type b)
  {
    return a == b;
  }

  static value_type Value(const field_type& field)
  {
    return field.GetValue();
  }

  static GPTYPE GlobalStart(const field_type& field)
  {
    return field.GetGlobalStart();
  }

  static value_type MappedValueAt(const BYTE *block, size_t i)
  {
    return INTEGERFLD::DecodeValue(
      block + i * INTEGERFLD::DISK_SIZE);
  }

  static GPTYPE MappedGlobalStartAt(const BYTE *block, size_t i)
  {
    return INTEGERFLD::DecodeGlobalStart(
      block + i * INTEGERFLD::DISK_SIZE);
  }

  static span_type MappedSpanAt(const BYTE *block, size_t i)
  {
    return INTEGERFLD::DecodeSpan(
      block + i * INTEGERFLD::DISK_SIZE);
  }

  static FILE *OpenForAppend(const STRING&);
  static bool LoadRawBlock(const STRING&, std::vector<field_type>*);
  static bool LoadValueBlock(const STRING&, std::vector<field_type>*);

  static bool MapIndexed(const STRING&,
                         MultiMMapSession&,
                         mapped_type *);

  static bool WriteIndex(const STRING&,
      const std::vector<field_type>& valueBlock,
      const std::vector<field_type>& gpBlock);
};


class INTEGERLIST : public MAPPEDORDEREDLIST<INTEGER_INDEX_TRAITS>
{
public:
  using base_type   = MAPPEDORDEREDLIST<INTEGER_INDEX_TRAITS>;
  using value_type  = base_type::value_type;
  using span_type   = base_type::span_type;
  using mapped_type = base_type::mapped_type;
};

#endif
