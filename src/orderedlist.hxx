// orderedlist.hxx

#ifndef ORDEREDLIST_HXX
#define ORDEREDLIST_HXX

#include <algorithm>
#include <cstddef>
#include <vector>

#include "defs.hxx"
#include "string.hxx"


template <class Traits>
class ORDEREDLIST
{
public:
  using value_type = typename Traits::value_type;
  using field_type = typename Traits::field_type;

  //
  // Half-open range [first,last).
  //
  struct range_type
  {
    size_t first = 0;
    size_t last  = 0;

    bool   empty() const { return first >= last; }
    size_t size()  const { return empty() ? 0 : last - first; }

    explicit operator bool() const { return !empty(); }
  };


  ORDEREDLIST() = default;


  void Clear()
  {
    table.clear();
  }


  void Reserve(size_t n)
  {
    table.reserve(n);
  }


  void Add(const field_type& field)
  {
    table.push_back(field);
  }


  void Add(field_type&& field)
  {
    table.emplace_back(std::move(field));
  }


  size_t GetCount() const
  {
    return table.size();
  }


  bool Empty() const
  {
    return table.empty();
  }


  GPTYPE GetGlobalStart(size_t i) const
  {
    return Traits::GlobalStart(table[i]);
  }


  value_type GetValue(size_t i) const
  {
    return Traits::Value(table[i]);
  }


  field_type& operator[](size_t i)
  {
    return table[i];
  }


  const field_type& operator[](size_t i) const
  {
    return table[i];
  }


  typename std::vector<field_type>::iterator begin()
  {
    return table.begin();
  }


  typename std::vector<field_type>::iterator end()
  {
    return table.end();
  }


  typename std::vector<field_type>::const_iterator begin() const
  {
    return table.begin();
  }


  typename std::vector<field_type>::const_iterator end() const
  {
    return table.end();
  }


  //
  // Sort primarily by scalar value.
  //
  // GP is the tie breaker. Apart from making output deterministic, this
  // also keeps equal values in corpus order.
  //
  void Sort()
  {
    std::sort(table.begin(), table.end(),
      [](const field_type& a, const field_type& b)
      {
        const value_type av = Traits::Value(a);
        const value_type bv = Traits::Value(b);

        if (Traits::Less(av, bv))
          return true;

        if (Traits::Less(bv, av))
          return false;

        return Traits::GlobalStart(a) <
               Traits::GlobalStart(b);
      });
  }


  //
  // The normal case should already be GP ordered because indexing and
  // import produce monotonically increasing global addresses.
  //
  // Keep this for validation/recovery, not as part of the normal build.
  //
  bool IsGpOrdered() const
  {
    if (table.size() < 2)
      return true;

    for (size_t i = 1; i < table.size(); ++i)
      {
        if (Traits::GlobalStart(table[i]) <
            Traits::GlobalStart(table[i - 1]))
          return false;
      }

    return true;
  }


  void SortByGP()
  {
    std::sort(table.begin(), table.end(),
      [](const field_type& a, const field_type& b)
      {
        return Traits::GlobalStart(a) <
               Traits::GlobalStart(b);
      });
  }


  //
  // Find the half-open range satisfying Relation.
  //
  // The table MUST be sorted by value.
  //
  // NE deliberately isn't implemented here. INDEX handles
  //
  //       x != key
  //
  // as equality followed by the existing field complement operation.
  //
  range_type Find(value_type key, ZRelation_t Relation) const
  {
    if (table.empty())
      return {};

    const auto lo = std::lower_bound(
      table.begin(), table.end(), key,
      [](const field_type& field, const value_type& value)
      {
        return Traits::Less(Traits::Value(field), value);
      });

    const auto hi = std::upper_bound(
      table.begin(), table.end(), key,
      [](const value_type& value, const field_type& field)
      {
        return Traits::Less(value, Traits::Value(field));
      });

    const size_t lower =
      static_cast<size_t>(lo - table.begin());

    const size_t upper =
      static_cast<size_t>(hi - table.begin());

    const size_t count = table.size();

    switch (Relation)
      {
      case ZRelEQ:
        return { lower, upper };

      case ZRelLT:
        return { 0, lower };

      case ZRelLE:
        return { 0, upper };

      case ZRelGT:
        return { upper, count };

      case ZRelGE:
        return { lower, count };

      case ZRelNE:
        break;

      default:
        break;
      }

    return {};
  }


  //
  // Transitional interface for IntegerSearch().
  //
  // I'd rather use the half-open range directly in new code, but this
  // makes conversion from NumericSearch straightforward if useful.
  //
  bool FindIndexes(value_type key, ZRelation_t Relation, size_t *First, size_t *Last) const
  {
    const range_type range = Find(key, Relation);

    if (range.empty())
      {
        if (First) *First = 0;
        if (Last)  *Last  = 0;
        return false;
      }

    if (First) *First = range.first;
    if (Last)  *Last  = range.last;

    return true;
  }


  //
  // Persistence is deliberately delegated to Traits.
  //
  // That lets INTEGER_INDEX_TRAITS own the canonical INT16 encoding
  // without ORDEREDLIST knowing anything about 128-bit integers,
  // endian conversion, file magic, versions, etc.
  //
  bool LoadValueBlock(const STRING& FileName)
  {
    table.clear();

    if (!Traits::LoadValueBlock(FileName, &table))
      {
        table.clear();
        return false;
      }

    return true;
  }


  bool WriteIndex(const STRING& FileName)
  {
    //
    // Preserve the GP stream before value sorting. Normally it is
    // already monotonically increasing.
    //
    std::vector<field_type> gp_table(table);

    if (!std::is_sorted(gp_table.begin(), gp_table.end(),
          [](const field_type& a, const field_type& b)
          {
            return Traits::GlobalStart(a) <
                   Traits::GlobalStart(b);
          }))
      {
        std::sort(gp_table.begin(), gp_table.end(),
          [](const field_type& a, const field_type& b)
          {
            return Traits::GlobalStart(a) <
                   Traits::GlobalStart(b);
          });
      }

    Sort();

    return Traits::WriteIndex(FileName, table, gp_table);
  }


  template <class Visitor>
  bool VisitMatches(const STRING& FileName, value_type Key, ZRelation_t Relation, Visitor&& visitor)
  {
    if (!LoadValueBlock(FileName))
      return false;

    const range_type range = Find(Key, Relation);

    if (range.empty())
      return false;

    for (size_t i = range.first; i < range.last; ++i)
      visitor(Traits::GlobalStart(table[i]));

    return true;
  }

private:
  std::vector<field_type> table;
};


#endif /* ORDEREDLIST_HXX */
