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


class INTEGERLIST : public ORDEREDLIST<INTEGER_INDEX_TRAITS>
{
public:
  using base_type   = ORDEREDLIST<INTEGER_INDEX_TRAITS>;
  using value_type  = INTEGER_INDEX_TRAITS::value_type;
  using span_type   = INTEGER_INDEX_TRAITS::span_type;
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
    return VisitMappedMatches(
        FileName, Key, Relation, Sessions,
        []() { return true; },
        visitor);
  }

  template <class Poll, class Visitor>
  bool VisitMappedMatches(const STRING& FileName,
                          value_type Key,
                          ZRelation_t Relation,
                          MultiMMapSession& Sessions,
                          Poll&& poll,
                          Visitor&& visitor)
  {
    static constexpr size_t CPU_CHECK_INTERVAL = 64U * 1024U;
    size_t until_check = CPU_CHECK_INTERVAL;

    const auto checkpoint = [&]() -> bool
    {
      if (--until_check != 0)
        return true;

      until_check = CPU_CHECK_INTERVAL;
      return poll();
    };
    mapped_type view;

    if (!INTEGER_INDEX_TRAITS::MapIndexed(FileName, Sessions, &view))
      return false;

    if (view.count == 0)
      return true;

    //
    // != is naturally a GP-order field-domain scan. Test every INTEGER
    // occurrence in the already GP-sorted block and emit only occurrences
    // whose value differs from Key. Records without the field never enter.
    //
    if (Relation == ZRelNE)
      {
        for (size_t i = 0; i < view.count; ++i)
          {
            if (!checkpoint())
              return false;

            if (!INTEGER_INDEX_TRAITS::Equal(
                  INTEGER_INDEX_TRAITS::MappedValueAt(view.gp_block, i),
                  Key))
              visitor(
                  INTEGER_INDEX_TRAITS::MappedGlobalStartAt(view.gp_block, i),
                  INTEGER_INDEX_TRAITS::MappedSpanAt(view.gp_block, i));
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
          {
            if (!checkpoint())
              return false;

            visitor(
                INTEGER_INDEX_TRAITS::MappedGlobalStartAt(view.value_block, i),
                INTEGER_INDEX_TRAITS::MappedSpanAt(view.value_block, i));
          }

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

    return VisitMappedSlicesInGpOrder(
        view,
        first, last,
        0, 0,
        [Key, Relation](value_type value)
        {
          return Relation == ZRelLT ? value < Key :
                 Relation == ZRelLE ? value <= Key :
                 Relation == ZRelGT ? value > Key : value >= Key;
        },
        checkpoint,
        poll,
        visitor);
  }


  //
  // Range relation semantics:
  //
  //   = a-b   a <= value <= b
  //  >= a-b   a <= value <= b
  //   > a-b   a <  value <  b
  //   < a-b   value < a || value > b
  //  <= a-b   value <= a || value >= b
  //  != a-b   value < a || value > b
  //
  // Endpoints are normalized here rather than in INTEGERRANGE so the value
  // object preserves what was parsed.
  //
  template <class Visitor>
  bool VisitMappedRange(const STRING& FileName,
                        value_type Low,
                        value_type High,
                        ZRelation_t Relation,
                        MultiMMapSession& Sessions,
                        Visitor&& visitor)
  {
    return VisitMappedRange(
        FileName, Low, High, Relation, Sessions,
        []() { return true; },
        visitor);
  }

  template <class Poll, class Visitor>
  bool VisitMappedRange(const STRING& FileName,
                        value_type Low,
                        value_type High,
                        ZRelation_t Relation,
                        MultiMMapSession& Sessions,
                        Poll&& poll,
                        Visitor&& visitor)
  {
    static constexpr size_t CPU_CHECK_INTERVAL = 64U * 1024U;
    size_t until_check = CPU_CHECK_INTERVAL;

    const auto checkpoint = [&]() -> bool
    {
      if (--until_check != 0)
        return true;

      until_check = CPU_CHECK_INTERVAL;
      return poll();
    };
    mapped_type view;

    if (!INTEGER_INDEX_TRAITS::MapIndexed(FileName, Sessions, &view))
      return false;

    if (view.count == 0)
      return true;

    if (INTEGER_INDEX_TRAITS::Less(High, Low))
      std::swap(Low, High);

    const size_t lower_low  = LowerBound(view, Low);
    const size_t upper_low  = UpperBound(view, Low);
    const size_t lower_high = LowerBound(view, High);
    const size_t upper_high = UpperBound(view, High);

    if ((Relation == ZRelEQ || Relation == ZRelGE) &&
        INTEGER_INDEX_TRAITS::Equal(Low, High))
      {
        for (size_t i = lower_low; i < upper_low; ++i)
          {
            if (!checkpoint())
              return false;

            visitor(
                INTEGER_INDEX_TRAITS::MappedGlobalStartAt(view.value_block, i),
                INTEGER_INDEX_TRAITS::MappedSpanAt(view.value_block, i));
          }

        return true;
      }

    //
    // <= x-x means value <= x OR value >= x, i.e. every occurrence.
    // Walk the GP block once rather than materializing two overlapping slices.
    //
    if (Relation == ZRelLE &&
        INTEGER_INDEX_TRAITS::Equal(Low, High))
      {
        for (size_t i = 0; i < view.count; ++i)
          {
            if (!checkpoint())
              return false;

            visitor(
                INTEGER_INDEX_TRAITS::MappedGlobalStartAt(view.gp_block, i),
                INTEGER_INDEX_TRAITS::MappedSpanAt(view.gp_block, i));
          }

        return true;
      }

    switch (Relation)
      {
      case ZRelEQ:
      case ZRelGE:
        return VisitMappedSlicesInGpOrder(
            view,
            lower_low, upper_high,
            0, 0,
            [Low, High](value_type value)
            {
              return value >= Low && value <= High;
            },
            checkpoint,
            poll,
            visitor);

      case ZRelGT:
        return VisitMappedSlicesInGpOrder(
            view,
            upper_low, lower_high,
            0, 0,
            [Low, High](value_type value)
            {
              return value > Low && value < High;
            },
            checkpoint,
            poll,
            visitor);

      case ZRelLT:
      case ZRelNE:
        return VisitMappedSlicesInGpOrder(
            view,
            0, lower_low,
            upper_high, view.count,
            [Low, High](value_type value)
            {
              return value < Low || value > High;
            },
            checkpoint,
            poll,
            visitor);

      case ZRelLE:
        return VisitMappedSlicesInGpOrder(
            view,
            0, upper_low,
            lower_high, view.count,
            [Low, High](value_type value)
            {
              return value <= Low || value >= High;
            },
            checkpoint,
            poll,
            visitor);

      default:
        return false;
      }
  }

private:
  struct HITPOS
  {
    GPTYPE    gp;
    span_type span;
  };

  //
  // Materialize one or two slices of the value-sorted block and emit them in
  // GP order. For broad selections, avoid allocating/sorting most of the
  // column and instead stream the GP-sorted block through Predicate.
  //
  template <class Predicate, class Checkpoint, class Poll, class Visitor>
  static bool VisitMappedSlicesInGpOrder(const mapped_type& view,
                                         size_t first1,
                                         size_t last1,
                                         size_t first2,
                                         size_t last2,
                                         Predicate&& PredicateFn,
                                         Checkpoint&& checkpoint,
                                         Poll&& poll,
                                         Visitor&& visitor)
  {
    const size_t selected =
        (last1 > first1 ? last1 - first1 : 0) +
        (last2 > first2 ? last2 - first2 : 0);

    if (selected == 0)
      return true;

    const size_t broad_range =
        view.count / 4 + (view.count % 4 != 0);

    if (selected >= broad_range)
      {
        for (size_t i = 0; i < view.count; ++i)
          {
            if (!checkpoint())
              return false;

            const value_type value =
                INTEGER_INDEX_TRAITS::MappedValueAt(view.gp_block, i);

            if (PredicateFn(value))
              visitor(
                  INTEGER_INDEX_TRAITS::MappedGlobalStartAt(view.gp_block, i),
                  INTEGER_INDEX_TRAITS::MappedSpanAt(view.gp_block, i));
          }

        return true;
      }

    std::vector<HITPOS> hits;
    hits.reserve(selected);

    const auto collect =
        [&](size_t first, size_t last)
        {
          for (size_t i = first; i < last; ++i)
            {
              if (!checkpoint())
                return false;

              hits.push_back({
                  INTEGER_INDEX_TRAITS::MappedGlobalStartAt(
                      view.value_block, i),
                  INTEGER_INDEX_TRAITS::MappedSpanAt(
                      view.value_block, i)
              });
            }

          return true;
        };

    if (!collect(first1, last1) || !collect(first2, last2))
      return false;

    // std::sort itself is not interruptible; avoid entering it if the leaf
    // budget has already expired after materializing the selected slice.
    if (!poll())
      return false;

    std::sort(hits.begin(), hits.end(),
      [](const HITPOS& a, const HITPOS& b)
      {
        return a.gp < b.gp;
      });

    if (!poll())
      return false;

    for (const HITPOS& hit : hits)
      {
        if (!checkpoint())
          return false;

        visitor(hit.gp, hit.span);
      }

    return true;
  }

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
