# Adaptive integer storage

An `integer` field retains its signed 128-bit value domain. At index
finalization its traits select the smallest supported encoding for the
entire column:

| Values in the column | Stored value |
| --- | --- |
| All between -2147483648 and 2147483647 | Signed 32-bit |
| All between 0 and 4294967295 | Unsigned 32-bit |
| Otherwise | Signed 128-bit |

The signed encoding takes precedence when both 32-bit encodings fit.
Existing `xs:int` and `xs:unsignedInt` field declarations also benefit
from this automatic storage selection.
An empty index also uses signed 32-bit storage. Values occupy four or
sixteen bytes per row, alongside the unchanged GP and field span. With
64-bit GPs, each complete row is 14 or 26 bytes; the index has both a
value-sorted block and a GP-sorted block plus a 16-byte header.

The column header identifies its width and signedness. Both versions
are read by `INTEGER_INDEX_TRAITS`; mapped searches and scans use the
same shared algorithms. Query keys and range endpoints keep the full
128-bit domain, including when the indexed values occupy four bytes.
Negative keys never wrap into unsigned values, and out-of-range keys
produce the appropriate empty or complete field-domain result.

Ingestion writes full-width raw rows. Reopening an indexed column for
append first expands its GP-sorted block into the raw representation,
using a temporary file and rename. Finalization selects the width again.
A value outside the current 32-bit encoding therefore widens storage
without changing field metadata or the field's frozen state. Mixing a
negative value with an unsigned value above 2147483647 requires 128 bits.

Import reads either indexed representation in GP order, applies the
destination GP offset, and writes full-width raw rows. Small and wide
integer columns can be merged in either direction; finalization chooses
the encoding from all destination values. No automatic downgrade to
floating point or fixed-point numerical values occurs.

The existing integer field ID and `i` sidecar suffix remain the same.
Full-width indexed columns retain version 2; compact indexed columns
use version 3 with an explicit signedness flag and checked row size.
Both readers reject incomplete rows, inconsistent lengths, unsupported
flags and impossible counts before appending or mapping a column.
Older applications must be rebuilt to read newly compacted columns.

This storage change applies to native integer fields. Existing hash,
phonetic and boolean field encodings retain their current behavior.
In particular `hash` and `casehash` use CRC64 plus a fractional length
discriminator. A dedicated boolean posting/bitmap representation can
be introduced independently of numeric column storage.

Enable `IB_BUILD_SCORING_TESTS` for the `integer_storage` regression.
