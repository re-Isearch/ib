#ifndef IB_PEER_SPAN_HXX
#define IB_PEER_SPAN_HXX

#include "opobj.hxx"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

// Keep the decimal argument as well as the historical FLOAT metric: byte
// distances can exceed FLOAT's exact integer range, and explicit zero must
// remain distinguishable from an unbounded PEER.
inline bool ParsePeerByteSpan(const STRING& text, GPTYPE *span)
{
  if (text.IsEmpty()) return false;
  GPTYPE value = 0;
  const GPTYPE limit = std::numeric_limits<GPTYPE>::max();
  for (size_t i = 0; i < text.GetLength(); ++i)
    {
      const unsigned char ch = text.c_str()[i];
      if (ch < '0' || ch > '9') return false;
      const GPTYPE digit = ch - '0';
      if (value > (limit - digit) / 10) return false;
      value = value * 10 + digit;
    }
  if (span) *span = value;
  return true;
}

inline bool HasPeerByteSpan(const OPOBJ& op)
{
  if (!op.GetOperatorString().IsEmpty()) return true;
  // FLOAT is the engine's binary32 metric. Inspect its magnitude bits so a
  // NaN cannot silently select unbounded PEER under -ffast-math.
  static_assert(sizeof(FLOAT) == sizeof(std::uint32_t) &&
      std::numeric_limits<FLOAT>::is_iec559, "PEER metrics require IEEE binary32");
  const FLOAT metric = op.GetOperatorMetric();
  std::uint32_t bits;
  std::memcpy(&bits, &metric, sizeof(bits));
  return (bits & UINT32_C(0x7fffffff)) != 0;
}

inline bool GetPeerByteSpan(const OPOBJ& op, GPTYPE *span)
{
  const STRING text = op.GetOperatorString();
  if (!text.IsEmpty()) return ParsePeerByteSpan(text, span);
  // Also accept programmatically constructed operators using the old metric.
  // Parse the formatted decimal before any integer cast, including non-finite
  // inputs; this stays safe in fast-math builds.
  const double metric = op.GetOperatorMetric();
  if (metric != std::floor(metric)) return false;
  return ParsePeerByteSpan(STRING().form("%.0f", metric), span);
}

#endif
