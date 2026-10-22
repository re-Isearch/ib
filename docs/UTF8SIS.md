# UTF-8 SIS ordering

UTF-8 token keys are already folded before indexing. Their unsigned byte
order is shared by the token sorter, SIS writing, lookup and index collapse.
Applying an eight-bit charset table to individual UTF-8 bytes can change
their ordering, including their position relative to ASCII words.

Collapse compares each key using its recorded byte length. Padding and the
following GP are excluded. Prefix limits remain byte counts; a key window
may end partway through an encoded character, so comparison must not decode
past that window. Both input indexes must use the same charset and SIS
prefix width. Different widths require rebuilding with a common SIS limit.

The UTF-8 charset selects its own comparison and SIS lookup callbacks.
Exact lookup uses recorded term lengths to reject longer words, including
matches that stop inside a UTF-8 sequence. Prefix lookup remains available.

When merge keys are reconstructed from source text, preparation folds whole
UTF-8 sequences once and uses Unicode-aware term classification to count
bytes. The merge reader retains enough source context to complete a character
at the prefix limit. SIS writing pads short strings rather than reading
beyond their allocation. Single-byte charset comparisons retain their
existing tables.

`INDEX::Dump()` reconstructs a folded comparison key separately from its
original-case display text. It uses each SIS header's charset and byte prefix
width, folds complete source characters before comparing that prefix, and
checks both term order and posting order within an equal key. Display text
ends on a complete UTF-8 character; the caller's charset is restored afterward.

Source buffer cleaning and `GetIndirectTerm()` walk whole UTF-8 sequences,
including silent normalization bytes and contextual dot-in-word characters.
Disabling lowercasing still requires Unicode-aware classification. Invalid or
incomplete sequences at a byte limit end the recovered term. Indirect reads
include the final byte of the record's inclusive range.

The `utf8_sis` CTest regression (`IB_BUILD_SCORING_TESTS=ON`) covers charset
dispatch, ASCII/Unicode ordering, exact and prefix lookups, byte limits,
merge-key preparation and real three-partition SIS/posting collapse. It also
checks windows splitting two-, three- and four-byte sequences, real source
recovery through prefix scans, original-case dump output, record-end boundaries,
case variants, Unicode separators, malformed ordering and single-byte charsets.

Reindex from source if an older collapse has already reordered or combined
UTF-8 term buckets incorrectly. Updating the comparator cannot reconstruct
the original dictionary/posting associations from a damaged merged index.
