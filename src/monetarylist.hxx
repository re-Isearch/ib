// monetarylist.hxx

#ifndef MONETARYLIST_HXX
#define MONETARYLIST_HXX

#include "integerlist.hxx"

// The physical row is identical to INTEGER: GP + signed INT128 scalar + span.
// For MONETARY the scalar is the exact count of 1/50000-unit ticks.
using MONETARYFLD = INTEGERFLD;

struct MONETARY_INDEX_TRAITS : public INTEGER_INDEX_TRAITS
{
  using field_type = MONETARYFLD;
  struct mapped_type
  {
    const BYTE *value_block = nullptr;
    const BYTE *gp_block = nullptr;
    size_t count = 0;
  };

  static constexpr const char *Name()
  {
    return "MONETARY";
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

class MONETARYLIST : public MAPPEDORDEREDLIST<MONETARY_INDEX_TRAITS>
{
public:
  using base_type   = MAPPEDORDEREDLIST<MONETARY_INDEX_TRAITS>;
  using value_type  = typename base_type::value_type;
  using span_type   = typename base_type::span_type;
  using mapped_type = typename base_type::mapped_type;
};

#endif
