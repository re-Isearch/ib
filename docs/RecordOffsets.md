# Large source files and bounded records

In a 64-bit build, `RECORD` stores its absolute source-file start and
inclusive end as `GPTYPE`. It can describe a whole input file before
parsing, including a range such as `[0, 100000000000000]`. An end of zero
retains the existing to-EOF convention. A document/container range is
independent of the configured maximum size of an individual indexed
record.

`MDTREC` describes a bounded indexed record. It stores a `GPTYPE` source
start and a `UINT4` span equal to `end - start`. `GetLocalRecordEnd()`
reconstructs the inclusive end. The existing maximum-record-size setting
still applies during ingestion; concatenating small records into a
large file does not increase any individual record's span.

`SetLocalRecordEnd()` and `SetLocalRecordRange()` return false for a
reversed range or a span larger than `UINT4`. Invalid ranges cannot be
written or inserted into an MDT. Moving a start with
`SetLocalRecordStart()` preserves its span; builders setting both
endpoints should use `SetLocalRecordRange()`. Source-start-plus-span
overflow is checked independently of the span's width.

The small MDT fields are arranged to avoid padding away the saving. On
the tested 64-bit build, `MDTREC` remains 168 bytes, compared with 176
bytes for two wide endpoints. This is a storage/layout result, not a
claim of measured search-speed improvement.

`RESULT` also retains wide absolute endpoints, so retrieval does not
truncate the source position after indexing. Queue validation and
hit-coordinate calculations use wide positions; record-length checks
operate on the difference without a narrowing cast.

RECORD and RESULT streams have distinct object IDs and an explicit
`GPTYPE` width tag; their collection formats also have distinct IDs.
MDT headers use the `MD3` format marker, and lookup
caches have a new version. An incompatible MDT is rejected before
mapping its rows or rebuilding caches, even if its file length happens
to match the new row size. Applications and doctype plugins must be
rebuilt together.

Rebuild affected indexes and recreate old indexing queues. Already
truncated offsets cannot be recovered from the old metadata; the
original source files remain the input for reindexing. The regression
test uses small payloads at offsets beyond 4 GiB and near 16 GiB in a
sparse file, along with wide range serialization, MDT reopening,
retrieval, span-overflow rejection and incompatible-format checks.

Enable `IB_BUILD_SCORING_TESTS` to run the `record_offsets` CTest test.
