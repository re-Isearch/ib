# Fixed numerical fields

`NUMERICOBJ` stores a signed 128-bit integer count of `10^-16` units.
The scale is decimal (`10^16` ticks per unit), so values such as `0.1` and
`0.1234567890123456` have exact representations. This is not a binary Q
format. Sixteen fraction digits provide one more decimal digit than a
15-place format; an even digit count does not itself improve performance.

The representable endpoints are:

- Minimum: `-17014118346046923173168.7303715884105728`
- Maximum: `17014118346046923173168.7303715884105727`

Decimal and scientific notation are parsed directly into ticks. Additional
fraction digits are rounded to nearest, with ties to even. Zero is valid;
missing values, malformed input, nonfinite values and overflow are invalid.
Multiplication and division use a 256-bit intermediate and the same
rounding rule. Arithmetic overflow and division by zero produce an invalid
object. Integer arguments remain exact, including mixed arithmetic.

Numerical indexing, equality and inequality searches, numerical range
endpoints and scan terms use ticks throughout. Query terms enter the
parser as strings instead of passing through `double`. Numerical search
continues to retrieve full field coordinates from the coordinate cache.
Scans emit exact decimal strings, trim trailing fractional zeroes, and
count duplicate values. The integer and monetary columns retain their
own formats and scan methods.

`GetFieldData` accepts a `NUMERICOBJ*` for exact retrieval. Existing
floating callback and `DOUBLE*` interfaces convert explicitly at their
API boundaries; their floating-point precision is not recoverable.

The numerical column header identifies the fixed format, version, decimal
scale, row size and record count. Indexed columns contain a value-sorted
block and a GP-sorted block. Appending restores the GP block to a raw
stream before reindexing. Interval rows also identify their fixed format
and store both endpoint scales.

**Reindex existing floating numerical and numerical-range columns.** Their
old on-disk representation is rejected, including when opening for append.
This also applies to computed, hash, phonetic and boolean fields stored
through the numerical column. There is no floating-column compatibility
decoder. Rebuild applications and document-type plugins against the new
headers because the numeric object and range parsing interfaces changed.

The focused `fixed_numerical` CTest target is enabled with
`IB_BUILD_SCORING_TESTS=ON`. It checks decimal limits, ties, scientific
notation, arithmetic, persistence, mapped search relations, exact scan
terms and counts, append/import order, interval indexing and rejection of
old formats or incompatible scales.
