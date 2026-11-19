/* Copyright (c) 2026 Project re-Isearch and its contributors.
   Licensed under the Apache 2.0 license: see LICENSE. */
#ifndef IB_VECTOR_EVIDENCE_HXX
#define IB_VECTOR_EVIDENCE_HXX

#ifdef VECTOR_INDEX
#include "fc.hxx"
#include <cstddef>
#include <vector>

// Query-local provenance: global source coordinates, original vector score,
// and ordinal among candidates returned for this vector search/field.
// This is not a lexical token weight or a record's combined ranking score.
struct VECTOR_EVIDENCE {
  FC Extent;
  DOUBLE Score = 0;
  size_t Rank = 0;
  size_t Returned = 0;

  VECTOR_EVIDENCE() = default;
  VECTOR_EVIDENCE(const FC& extent, DOUBLE score, size_t rank, size_t returned)
    : Extent(extent), Score(score), Rank(rank), Returned(returned) {}
};

using VECTOR_EVIDENCES = std::vector<VECTOR_EVIDENCE>;
#endif // VECTOR_INDEX
#endif // IB_VECTOR_EVIDENCE_HXX
