# Stable vector evidence for hybrid records

`HybridNormalization` now calibrates each native vector score independently of
the other retrieved candidates. It no longer divides by the query's maximum or
assigns every query a full-score winner. Lexical and typed-object scoring are
unchanged; this is the vector entry point into the existing record algebra.

For fixed anchors, the computation is:

```
x = clamp((native_score - VectorNoise) / (VectorStrong - VectorNoise), 0, 1)
evidence = VectorFloor + (1 - VectorFloor) * x*x*(3 - 2*x)
record_contribution = TermWeight * evidence
```

Defaults deliberately use identifiable points on Schmate's score scale:

```ini
[Hybrid]
VectorNoise=0.5
VectorStrong=1.0
VectorFloor=0.0
```

Schmate reports `(1 + similarity) / 2` for cosine/IP. Thus `0.5` is zero
interaction and `1.0` is unit interaction. These are starting anchors, **not
learned relevance thresholds**. Tune them for each embedding model and field.
For example, if raw similarity `0.2` is your noise boundary, set `VectorNoise`
to `0.6` on this source-score scale. If a caller boosts a source score before
normalization, choose anchors on that boosted scale. IRSET term weights apply
after calibration.

Use `[Hybrid:ABSTRACT]`, `[Hybrid:TITLE]`, etc. to override settings for the field
being searched. Each setting falls back to `[Hybrid]` individually. Database
profiles let different databases/models use different calibrations. The original
one-argument API uses the global section; the query evaluator passes the field
to the new overload.

`VectorFloor=0.4` restores the historical `[0.4,1]` envelope while retaining
absolute calibration. The default floor of zero lets weak vector matches
contribute no semantic evidence. Such records remain in the result set, with
their match locations intact. Lexical `MaxNormalization` still has its existing
floor; numerical compatibility does not imply that the modalities are fully
calibrated to relevance yet.

The existing `MaxNormalization` state marker prevents subsequent lexical
normalization from replacing vector scores with hit-count-derived values.
Term weights apply once. Min/max metadata describes the actual resulting scores,
including empty sets and negative weights. Non-finite source scores contribute
zero. Invalid settings log a warning and use defaults or the fixed-anchor path.

## Optional background information calibration

A small uniform empirical CDF can change the calibration curve from score
spacing to background surprisal spacing. It is fixed for a model/field, rather
than fitted to the top-K list of the current query.

```ini
[Hybrid:ABSTRACT]
VectorNoise=0.6
VectorStrong=0.95
VectorCDFMin=0
VectorCDFMax=1
VectorCDF=0 50 90 99 100
```

The example is intentionally tiny. In practice use 256 or 512 bins. There are
`bins + 1` cumulative boundary counts: the first is zero, subsequent counts are
nondecreasing integers, and the last is the sample count `N`. Values outside the
CDF domain use the nearest endpoint. Counts must be nonnegative, and the table
must distinguish the selected noise and strong anchors.

At each boundary, the table precomputes:

```
tail = (N - cumulative_count + 1) / (N + 1)
information = -log(tail)
```

The pseudocount keeps unseen tails finite. Lookup interpolates adjacent
information entries in O(1). The same smoothstep then maps information between
`I(VectorNoise)` and `I(VectorStrong)` to bounded evidence. Table parsing and
logarithms happen once per result set, not once per candidate. Invalid or
unresolved tables fall back to fixed score anchors.

Generate the settings from one native score per line:

```sh
python3 utils/vector_background.py --field ABSTRACT background-scores.txt
```

Schmate's `utils/vector_background.cpp` samples query-energy-normalized INT4
PASS cosine scores from a float32 embedding export and reports the energy
distribution. Its output can be piped to this script. Sample each model/field
separately, using representative query/candidate populations. Random corpus
pairs approximate a background; they are not guaranteed to be unrelated and
may differ from live query-to-corpus relationships.

Background rarity is not a relevance probability. ANN selects extreme scores
from many candidates, so rare random-pair scores alone do not establish
relevance. Sample count limits tail resolution; validate anchors and fusion
weights on representative searches and judged records. This branch provides
the inexpensive calibration mechanism, not a fitted model or corpus-specific
thresholds.

## Regression checks

Configure with `-DIB_BUILD_SCORING_TESTS=ON`, build
`hybrid_normalization_test`, and run `ctest -R hybrid_normalization
--output-on-failure` in the build directory. Tests exercise the real IRSET
method, field overrides, CDF validation, candidate independence, actual score
metadata, invalid inputs, legacy floor, and repeated normalization guards.
