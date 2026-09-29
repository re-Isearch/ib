// integerlist.hxx

#ifndef INTEGERLIST_HXX
#define INTEGERLIST_HXX

#include "orderedlist.hxx"
#include "integerfield.hxx"

struct INTEGER_INDEX_TRAITS
{
  using value_type = INT16;
  using field_type = INTEGERFLD;

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

  static bool LoadValueBlock( const STRING&, std::vector<field_type>*);

  static bool WriteIndex( const STRING&,
      const std::vector<field_type>& valueBlock,
      const std::vector<field_type>& gpBlock);
};

using INTEGERLIST = ORDEREDLIST<INTEGER_INDEX_TRAITS>;

#endif
