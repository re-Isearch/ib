# Byte proximity searches

`A B PROX:n` finds nonoverlapping hits in the same record with at most `n`
bytes between them. It does not require a shared field instance. For example:

```sh
Isearch -d PATENTS -rpn is a PROX:10
Isearch -d PATENTS -rpn is a PROX:50
```

For inclusive hit intervals with A before B, the byte gap is:

```text
gap = B.start - A.end - 1
```

Adjacent hits have gap zero. Ordinary PROX does not pair overlapping intervals
or a hit with itself. This differs from bounded PEER, which explicitly permits
overlapping operands and also verifies the structural container relationship.

An absolute negative metric selects gaps at least its magnitude. Nonzero
metrics between -1 and 1 use the existing document-span calculation and
rounding. `PROX:0` retains its existing PEER meaning. The metric is still stored
as FLOAT; this change does not alter query serialization or metric precision.
Coordinates and gap comparisons use GPTYPE throughout.

## Missing later hits in sorted tables

The previous implementation could exit its inner loop on the first distant
right-hand hit, even if a later right-hand hit was close. For example:

| Operand | Hit intervals |
| --- | --- |
| `is` | `[100,101]` |
| `a` | `[0,0]`, `[103,103]` |

The first `a` is distant; the second has gap 1 and must match `PROX:10`.
The old sorted-hit early exit could discard the record before reaching it.
The old code also narrowed coordinate differences to int and subtracted
interval endpoints before checking their order.

Separately, the short `PROX:n` spelling read its argument at the character
offset for the longer `PROXIMITY:n` spelling. This could read beyond the intended
argument and use zero or unrelated data as the metric. Both spellings now read
the numeric argument after their own colon, including percentage arguments.
Named query composition also retains the parsed metric and field arguments
instead of resetting non-PEER operator parameters when adding the glue operator.

## Execution

The corrected join intersects record IDs, sorts right-hand hits by start/end,
then locates qualifying before and after ranges for each left-hand hit.
Full-width endpoint comparisons precede subtraction, and boundary arithmetic
is checked near the GPTYPE limits.

When ends remain ordered, each side's matches form a contiguous range. Pair
counts and evidence boundary events avoid enumerating the Cartesian product.
Nested intervals with crossing end order use exact end checks for the before
range. After ranges remain contiguous by start coordinate.

The hit table retains participating original hits without adding them again
for every pair. The record hit count counts all qualifying pairs. Existing
left-score and auxiliary-count behavior is retained. Cached aggregate hit
counts are invalidated after the join.

An operand whose records are sorted differently is copied and sorted by record
ID, then passed to the same join. This removes the duplicate implementation
that had the same early-exit problem and inconsistent fractional calculations.

This is a search-time correction. Existing indexes do not need rebuilding.

## Regression test

With `IB_BUILD_SCORING_TESTS=ON`, build `proximity_test` and run
`ctest -R '^proximity$' --output-on-failure`.

The test includes the missing-later-hit example, both operand orders, sorted
and unsorted hits and records, an independent pair oracle, directional and
negative metrics, fractional metrics, source identifiers, large coordinates,
boundary arithmetic, INDEX dispatch, and dense 30,000-by-30,000 joins with
linear evidence storage.
