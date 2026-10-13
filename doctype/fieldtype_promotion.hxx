#ifndef FIELD_TYPE_PROMOTION_HXX
#define FIELD_TYPE_PROMOTION_HXX

#include "attrlist.hxx"

namespace field_type_inference {

// Probe only the types reachable from the current type. Check metadata only
// after the content supplies a wider candidate.
template <class Value, class IsFrozen>
inline FIELDTYPE GuessNumericPromotion(const FIELDTYPE& current,
                                      const Value& contents, IsFrozen isFrozen)
{
  switch ((INT)current)
    {
      case FIELDTYPE::integer:
        if (contents.IsIntegralNumber()) return current;
        break;
      case FIELDTYPE::numerical:
        break;
      default:
        return current;
    }

  FIELDTYPE candidate(current);
  if (contents.IsNumber())
    candidate = FIELDTYPE::numerical;
  else if (contents.IsCurrency())
    candidate = FIELDTYPE::currency;

  return candidate != current && !isFrozen() ? candidate : current;
}

} // namespace field_type_inference

#endif
