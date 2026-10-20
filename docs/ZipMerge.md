# Buffered SIS zipper

`INDEX::MergeIndexFiles()` now merges a snapshot of the numbered SIS/INX
shards with `MERGE::Zip()`. For up to 64 inputs this is one streaming pass,
rather than repeatedly collapsing the last two shards into a growing index.
`INDEX::CollapseIndexFiles()` retains its existing two-shard implementation.

## Selection and copying

Each `MERGEUNIT` owns sequential readers for one dictionary and posting file.
A tournament tree selects the smallest current dictionary key and replays only
the changed input's path to the root. Equal keys are consumed in input order;
their posting blocks are concatenated under one output SIS entry. Selection is
per SIS entry, never per posting. Source documents are not read or tokenized.

Comparison uses the input charset's bounded `CompareSIS()`. UTF-8 prefixes
are compared as stored bytes, including prefixes ending within a UTF-8 sequence.
Original term lengths are bounded by the stored prefix width for comparisons.
The prefix width, charset and posting magic must match across inputs. Native
posting bytes are copied without conversion; SIS endpoints use the existing
portable encoding. No on-disk format change is required.

Each input has a 128 KiB posting buffer and a 64 KiB dictionary buffer.
A pass also uses a 1 MiB copying buffer and 320 KiB of output buffering.
Fan-in is limited to 64 and reduced for low descriptor limits. Larger input sets
are merged in bounded groups into private intermediate files, followed by the
next pass. Input order is retained across groups. Intermediates are cleaned on
return; optimizer recovery also removes intermediates owned by a dead optimizer.

For M input SIS entries and P posting bytes, a pass takes O(M log k) key
selection work and O(P) posting copying. The number of complete passes depends
on the available fan-in. Wall-clock speed depends on storage and key/list sizes.

## Publication and existing options

The output pair is written and closed before installation. Malformed dictionaries,
invalid posting spans, read/write/close errors and incompatible headers cause
the merge to fail. Numbered inputs are retired only after both output files
are installed and the MDT shard count is successfully changed to zero.
A changed shard count aborts publication.

This reuses the current optimizer locking and numbered-to-unnumbered handoff.
It does not add a background scheduler or a new concurrent-writer protocol.

* `-fast` continues to publish separate runs.
* `-optimize` and `-append` use the zipper for full consolidation.
* `-merge` still invokes full consolidation after each indexing flush. Batching
  similar-sized runs is a separate scheduling improvement.
* `-collapse` and the current `-incr` policy continue to use two-shard collapse.

## Verification

Enable `IB_BUILD_SCORING_TESTS` and run:

```sh
ctest --test-dir build -R '(zip_merge|utf8_sis)' --output-on-failure
```

`zip_merge` checks stable equal-key posting order, large posting lists, original
lengths exceeding prefix windows, UTF-8 prefixes, single-byte charsets, empty
runs, 130 shards, bounded fan-in, malformed inputs, capacity limits, temporary
cleanup, optimizer error propagation, publication and an already-open reader.
`utf8_sis` retains coverage of the existing two-shard collapse path.
