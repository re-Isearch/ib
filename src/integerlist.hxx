// integerlist.hxx

#ifndef INTEGERLIST_HXX
#define INTEGERLIST_HXX

#include <algorithm>
#include <vector>

#include "orderedlist.hxx"
#include "integerfield.hxx"
#include "mmap.hxx"

struct INTEGER_INDEX_TRAITS
{
  using value_type = INT16;
  using field_type = INTEGERFLD;

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


class INTEGERLIST : public ORDEREDLIST<INTEGER_INDEX_TRAITS>
{
public:
  using base_type   = ORDEREDLIST<INTEGER_INDEX_TRAITS>;
  using value_type  = INTEGER_INDEX_TRAITS::value_type;
  using mapped_type = INTEGER_INDEX_TRAITS::mapped_type;

  using base_type::OpenForAppend;
  using base_type::WriteIndex;

  template <class Visitor>
  bool VisitMappedMatches(const STRING& FileName,
                          value_type Key,
                          ZRelation_t Relation,
                          MultiMMapSession& Sessions,
                          Visitor&& visitor)
  {
    mapped_type view;

    if (!INTEGER_INDEX_TRAITS::MapIndexed(FileName, Sessions, &view))
      return false;

    if (view.count == 0)
      return true;

    //
    // != is naturally a GP-order field-domain scan.  Test every INTEGER
    // occurrence in the already GP-sorted block and emit only occurrences
    // whose value differs from Key.  Records without the field never enter.
    //
    if (Relation == ZRelNE)
      {
        for (size_t i = 0; i < view.count; ++i)
          {
            if (!INTEGER_INDEX_TRAITS::Equal(
                  INTEGER_INDEX_TRAITS::MappedValueAt(view.gp_block, i),
                  Key))
              visitor(INTEGER_INDEX_TRAITS::MappedGlobalStartAt(
                  view.gp_block, i));
          }

        return true;
      }

    const size_t lower = LowerBound(view, Key);
    const size_t upper = UpperBound(view, Key);

    //
    // Equal values are GP ordered already because GP is the tie breaker in
    // the value-sorted block.
    //
    if (Relation == ZRelEQ)
      {
        for (size_t i = lower; i < upper; ++i)
          visitor(INTEGER_INDEX_TRAITS::MappedGlobalStartAt(
              view.value_block, i));

        return true;
      }

    size_t first = 0;
    size_t last  = 0;

    switch (Relation)
      {
      case ZRelLT:
        first = 0;
        last  = lower;
        break;

      case ZRelLE:
        first = 0;
        last  = upper;
        break;

      case ZRelGT:
        first = upper;
        last  = view.count;
        break;

      case ZRelGE:
        first = lower;
        last  = view.count;
        break;

      default:
        return false;
      }

    if (first >= last)
      return true;

    std::vector<GPTYPE> gps;
    gps.reserve(last - first);

    for (size_t i = first; i < last; ++i)
      gps.push_back(INTEGER_INDEX_TRAITS::MappedGlobalStartAt(
          view.value_block, i));

    std::sort(gps.begin(), gps.end());

    for (GPTYPE gp : gps)
      visitor(gp);

    return true;
  }

private:
  static size_t LowerBound(const mapped_type& view, value_type key)
  {
    size_t lo = 0;
    size_t hi = view.count;

    while (lo < hi)
      {
        const size_t mid = lo + (hi - lo) / 2;

        if (INTEGER_INDEX_TRAITS::Less(
              INTEGER_INDEX_TRAITS::MappedValueAt(view.value_block, mid),
              key))
          lo = mid + 1;
        else
          hi = mid;
      }

    return lo;
  }

  static size_t UpperBound(const mapped_type& view, value_type key)
  {
    size_t lo = 0;
    size_t hi = view.count;

    while (lo < hi)
      {
        const size_t mid = lo + (hi - lo) / 2;

        if (!INTEGER_INDEX_TRAITS::Less(
              key,
              INTEGER_INDEX_TRAITS::MappedValueAt(view.value_block, mid)))
          lo = mid + 1;
        else
          hi = mid;
      }

    return lo;
  }
};

#endif
