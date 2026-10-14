# Binary64 floating fields

Declare a field as `float64`, `double`, or `binary64` to use IEEE 754
binary64 floating point. `floating`, `xs:double` and `xs:float` also select
this type. Existing field-type IDs remain unchanged. The sidecar suffix
is `f64`.

This type complements `integer` (signed 128-bit integers), `numerical`
(signed 128-bit fixed point with 16 decimal fraction digits), and
`currency` (signed 128-bit ticks of 1/50000 unit). Decimal128 is not needed
to use binary64. Automatic inference/promotion retains its existing
rules; explicitly declared floating fields use their own parser.

Field values and query terms are parsed directly into `double`, including
scientific notation and subnormals. Neither fixed-point conversion nor a
locale-dependent decimal separator is involved. The index accepts finite
values. Invalid syntax, NaN, infinity, overflow and inputs below the
representable range are rejected.

The value occupies eight bytes on disk in network-order IEEE encoding,
alongside its GP. A distinct, versioned column header identifies the
format and row size. Value-sorted and GP-sorted blocks support the shared
traits-based mapped search and scan algorithms. Hit coordinates continue
to come from the field coordinate cache.

`Float64Search` supports the six scalar relations. `Float64Scan` has
`SCANLIST` and `STRLIST` overloads; `NumericalScan` dispatches to it for a
floating field. Scan terms are shortest decimals that round-trip to the
stored binary64 value. Equal values are grouped numerically; positive
and negative zero share the scan term `0`, while stored negative-zero
bits are preserved.

The binary64 storage/query sources and their regression test disable
fast-math, including in optimized builds, to preserve these IEEE rules.

The new format requires creating/reindexing explicitly floating fields.
Existing fixed-point columns retain their format. Older floating-column
files are not decoded as the new format. Rebuild applications and
doctype plugins against the updated headers.

Enable `IB_BUILD_SCORING_TESTS` to run the `float64` CTest regression.
