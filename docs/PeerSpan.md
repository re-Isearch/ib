# Bounded PEER searches

`PEER:n` requires two hits to satisfy the existing PEER container relationship
and have at most `n` bytes between them. Both conditions apply to the same pair
of hits. Plain `PEER` continues to have no distance bound.

| RPN operator | Relationship |
| --- | --- |
| `A B PEER:n` | A and B are peers, in either order, with byte gap at most n. |
| `A B PEERb:n` | A is before B under the existing PEERb ordering, with byte gap at most n. |
| `A B PEERa:n` | A is after B under the existing PEERa ordering, with byte gap at most n. |

`n` is a nonnegative decimal integer within the database's `GPTYPE` range. Signs,
fractions, percentages, exponents, and overflowing values are not valid byte
bounds. The exact integer is retained when queries are copied, rewritten, or
printed, including values beyond the precision of the historical FLOAT metric.

## Examples

```sh
Isearch -d PATENTS -rpn risc processor PEER:250
Isearch -d PATENTS -rpn risc processor PEERb:250
Isearch -d PATENTS -rpn processor risc PEERa:250
```

Infix queries accept the same operators, for example `risc PEERb:250 processor`.
Unparenthesized chains bind left to right. Use parentheses or explicit RPN to
select a different grouping: bounded PEER is a binary relation and is not
reassociated by the query rewriter.

## What the distance measures

Hit coordinates are inclusive byte intervals. For A ending before B starts:

```text
gap = B.start - A.end - 1
```

For the reverse order, exchange A and B. Overlapping intervals have gap zero.
Thus `[10,12]` and `[20,22]` have gap 7; `[10,12]` and `[13,15]` have gap 0.
`PEER:0` accepts adjacency or overlap and still checks the container. It is
different from unbounded `PEER`.

The unit is bytes in the indexed coordinate system, not words or Unicode
characters. Compound operands use their retained hit intervals. Ordered forms
keep the existing PEERa/PEERb start-and-end ordering, including its treatment of
overlapping intervals. If a database has no structural fields, the relationship
is bounded proximity within the same record.

## Execution and performance

The engine first intersects records and finds distance/order candidates among
their positional hits. It resolves field containers only for hits with such
candidates, then verifies containment within the positional window.

For ordinary tokens and intervals whose ends remain ordered after sorting by
start, matching right-hand hits form a contiguous range. The engine counts pairs
and collects their evidence through range boundaries, without enumerating every
matching pair. Broad or nested compound intervals with crossing end order use
explicit candidate checks to preserve correctness. Result hit counts still
count qualifying pairs; the hit table retains the participating source hits.

This operator asks a narrower question than unbounded PEER. It can reduce field
lookups and pair work for common terms, but its benefit depends on the chosen
bound and the hit distribution. It does not change unbounded PEER into an
approximate search or automatically choose a frequency threshold or distance.
It uses the existing positional and field indexes.

In particular, `A B PROXIMITY:n SIBLING` is not a substitute for this check:
the current SIBLING rewrite treats a completed proximity operand as opaque.

## Diagnosing slow joins

Add `-debug` to print one `Bounded PEER profile` line per bounded operator:

```sh
Isearch -debug -d PATENTS -show -max 1 risc processor PEER:130
```

The counters describe the full bounded join, before the displayed result limit:

| Counter | Meaning |
| --- | --- |
| `records` | Records present in both input sets. |
| `left_hits`, `right_hits` | Input hit counts across those shared records. |
| `positional_hits` | Left hits with at least one right hit satisfying the byte gap and ordering, before checking containers. |
| `container_lookups` | Actual calls to the container resolver after the positional filter. Zero for a database without structural fields. |
| `matched_records`, `pairs` | Records and hit pairs passing the complete bounded PEER relationship. |
| `elapsed_ms` | Wall time inside this bounded join, including copies, sorting and hit-table assembly. |
| `container_ms` | Wall time spent inside container resolver calls. |
| `other_ms` | The rest of the bounded join's elapsed time. |

These timings exclude the earlier term searches and later result presentation.
They include the overhead of the diagnostic clock reads; ordinary searches do
not read the diagnostic clock or update these counters. A high `container_ms`
points to structural lookup work; a high `other_ms` calls for investigating the
remaining join work. The counters also show how much the positional filter
actually reduces the number of hits needing container resolution.

## Regression test

With `IB_BUILD_SCORING_TESTS=ON`, build `peer_span_test` and run
`ctest -R '^peer_span$' --output-on-failure`. The test compares the join against
an independent pair oracle, exercises parser and INDEX dispatch, covers zero and
large bounds, nested intervals, ordering, structural boundaries and provenance,
and checks a dense join without materializing its Cartesian product.
