# Vector evidence: matches, scores, weights, and spans

## Why this needs an explicit contract

Lexical retrieval offers a useful binary distinction: a specified term occurs in a record or it does not. Lexical ranking may still be continuous, but **term occurrence itself is discrete**.

Vector retrieval is different. A nearest-neighbor search can return a best candidate even when that candidate is not relevant to the user's information need. Similarity is continuous; ranking first means *nearest among candidates*, not *relevant*. There are, figuratively, **99 shades of gray**.

This matters when a document contains several vector spans, when a query searches several vector fields, and when vector terms participate in a lexical/vector expression.

**Key principle:** The searcher (human or agent) supplies the relevance policy through the query: field selection, terms, explicit weights, and operators. The index must not silently substitute a field-importance policy of its own.

## Four different concepts

1. **Match location:** The field and source span that produced a candidate vector hit.
2. **Native similarity:** The embedding model/index's score for that query and span. A top-ranked hit is not automatically relevant.
3. **Calibrated evidence:** The contribution obtained by mapping native similarity through the field/model's calibration function. Weak similarity can contribute zero.
4. **Query weight and algebra:** The explicit importance assigned to that query operand, and the operators that determine how operands combine.

These quantities must not be conflated. In particular, a high query weight does not turn noise into relevant evidence.

## Order of operations

For a vector query term against field `f`, the intended conceptual order is:

```text
retrieved spans with native similarities
    -> field/model-specific evidence calibration
    -> consolidate spans belonging to the same record and same vector operand
    -> apply that operand's explicit query weight
    -> combine distinct operands according to query operators
```

Equivalently, with a native similarity `s`, field-aware calibration `C_f`, and an explicit query weight `w`:

```text
contribution = w * C_f(s)
```

**Calibrate before weighting.** If `C_f(s) = 0`, even a large positive `w` leaves the contribution at zero. A field weight expresses the searcher's *importance*, not a declaration that every hit in that field is relevant.

Calibration is not an oracle: a statistically unusual similarity may still match something irrelevant to the overall information need. The existing HybridEnergy documentation explicitly distinguishes background rarity from relevance probability.

## Multiple spans from *one* vector query term

One HNSW search can return several passages that map to the same MDT record. They are **not** several independently specified query operands, and they must not appear as duplicate documents.

**Proposed first policy (not yet implemented):**

- Preserve all returned span locations in the record's hit/evidence information.
- Use the **largest calibrated evidence** among those spans as the record's score contribution for that one vector operand.
- Do not sum passage scores: overlapping windows and long documents could otherwise inflate scores merely by yielding more hits.
- Do not boost the document for extra spans until measurements show that doing so is beneficial.

If all spans calibrate to zero, the record may still be a nearest-neighbor candidate with useful coordinates. **Zero evidence is not identical to no candidate.** A pure-vector search should not silently report "nothing found" solely because calibration rejected every contribution. Presentation/ranking of zero-evidence candidates remains a separate policy question.

### Why this is a result-set issue, not an HNSW duplication issue

The current Schmate `EmbeddingIndexer::search()` translates each returned span into an `IRESULT` and calls `FastAddEntry()`. That fast insertion path does not generally coalesce nonadjacent entries with the same record ID. The lexical search code already contains a `FastAddEntry()` followed by `MergeEntries(true)` pattern.

**However:** blindly calling `MergeEntries(true)` is not the complete proposed fix. Its existing score-aggregation behavior is not the desired *maximum calibrated passage evidence* policy. Deduplicating identities, retaining hit locations, and selecting the contribution must be considered together.

## Different vector query terms are different operands

Suppose the searcher explicitly asks for two vector searches, possibly in different fields and possibly with different query text.

Those are **separate operands of the query**, not duplicate passages from one operand. The searcher's weights and operators govern their combination. Do **not** replace their combination with an implicit MAX simply because both operands happen to be vector-valued.

This mirrors the familiar lexical pattern:

```text
TITLE/quantum:2 quantum OR
```

The searcher asks for `quantum` generally while boosting occurrences in `TITLE`. The engine should execute that stated preference rather than deciding for itself that `TITLE` is always important.

### A subtle example: similarity versus contribution

| Operand | Native score | Explicit weight |
| --- | ---: | ---: |
| Field 1 / query 1 | 0.40 | 3.0 |
| Field 2 / query 2 | 0.80 | 0.5 |

Comparing `3 * 0.40` to `0.5 * 0.80` is **not** a valid substitute for calibration. On illustrative anchors where `0.40` is below the noise floor, its calibrated evidence is zero and its weighted contribution remains zero. The `0.80` match may provide nonzero evidence despite the smaller weight.

Nor should the engine compare uncalibrated scores across fields/queries as if they necessarily had the same distribution. The actual calibration depends on the embedding model, field, and scoring scale; query difficulty can also vary.

## Field semantics belong to the query

Consider two documents:

| Field | Document A | Document B |
| --- | ---: | ---: |
| Abstract | weak | strong |
| Claims | weak | weak |
| Bibliography | strong | weak |

An unweighted highest-similarity rule might favor Document A, although a scientific-claim searcher might prefer Document B. A bibliography-oriented searcher might prefer Document A. **Neither preference is universally correct.**

The searcher must be able to express the preference with field selection, operand weights, and query operators. Calibration makes an evidence contribution interpretable on its intended scale; it does **not** supply the user's semantic intent.

## Implementation boundaries and open questions

This document describes the **intended semantics**, not a claim that the entire policy is implemented today.

- **Schmate:** Return native vector hits, scores, field identity, and span coordinates without manufacturing a relevance judgment.
- **HybridEnergy:** Map vector similarity to calibrated evidence, with available global and per-field anchors or background CDFs. Preserve its explicit warning: rarity is not relevance.
- **CoreQuarry result sets:** Coalesce repeated spans from the **same vector operand and same document**, retain their locations, and select the strongest calibrated contribution for that operand.
- **CoreQuarry query algebra:** Apply explicit weights and combine **distinct** operands as requested by the searcher.
- **Pure-vector zero-evidence results:** Decide how to display/order candidates without silently turning low confidence into an empty index result or promoting them into positive relevance evidence.

Before changing code, test at least: nonadjacent duplicate document hits; overlapping and nonoverlapping spans; two different vector operands matching the same record; different per-field calibrations; weighted noise-floor behavior; and pure-vector searches whose best candidates all receive zero calibrated evidence.

## Related code and documentation

- [HybridEnergy: stable vector evidence](HybridEnergy.md)
- [Schmate `EmbeddingIndexer::search()`](https://github.com/re-Isearch/Schmate/blob/fix/search-only-hnsw-right-size/src/EmbeddingIndexer.cpp)
- [CoreQuarry `atomicIRSET::AddEntry` / `FastAddEntry` / `MergeEntries`](../src/irset.cxx)
