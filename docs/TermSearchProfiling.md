# Profiling literal phrase source verification

Set `IB_PROFILE_TERMSEARCH=1` to emit per-anchor and per-query diagnostics
from `INDEX::TermSearch()`. The flag is checked once per process; without it
the counters and timers are inactive. No profile data is stored in the index,
and no reindex is required.

```sh
IB_PROFILE_TERMSEARCH=1 ../bin/Isearch -d /tmp/BART -show -max 1 "to be or not to be"

# Same corpus, positional operator: escape #< from the shell.
IB_PROFILE_TERMSEARCH=1 ../bin/Isearch -d /tmp/BART -show -max 1 \
  to be '#<' or '#<' not '#<' to '#<' be '#<'
```

Look for `TERM_PROFILE` in the diagnostic stream.

Each `TERM_PROFILE anchor` line gives the partition (`shard`), selected SIS
anchor term and its byte offset in the query, the number of candidate postings
actually read (after any clipping), and the SIS overflow flag.

The `TERM_PROFILE query` line gives:

| Field | Meaning |
| --- | --- |
| `shards` | Partitions whose INX file was opened for lookup |
| `candidates` | Candidate posting entries actually read from INX |
| `visited` | Postings reached by the candidate verification loop |
| `field_rejected` | Postings skipped by the field filter |
| `source_checks` | Candidates reaching a source-verification branch |
| `accepted` | Hits appended after source and boundary checks |
| `source_reads` | `GetIndirectBuffer()` calls made to verify postings |
| `source_bytes` | Positive bytes returned by those calls |
| `source_ms` | Cumulative wall time inside those calls |
| `probe_reads` | `GetIndirectBuffer()` calls in the old INX binary-search fallback |
| `probe_bytes` | Positive bytes returned by fallback probes |
| `probe_ms` | Cumulative wall time spent in fallback source reads |
| `compares` | Calls to the selected source comparison routine |
| `compare_ms` | Cumulative wall time inside those comparisons, including UTF-8 source folding when applicable |
| `elapsed_ms` | Elapsed wall time inside the profiled `TermSearch()` call |
| `delegated` | Search was delegated to another phrase/query path (e.g. common-word proximity fallback) |

The source-read counters include doctype `GetTerm()` work, which can involve
file positioning, entity-normalization lookbehind, entity processing, and
copying. They are not counts of actual physical disk reads or OS-level opens:
the file-pointer table normally reuses cached handles.

Times are wall-clock measurements, not the CPU time printed by Isearch.
The times overlap with `elapsed_ms` and should not be summed with it.
When the profile flag is enabled, the per-call timers and logging themselves
add overhead. Compare multiple runs with identical settings, preferably with
warm and cold caches measured separately. `-show` runs after the search and
does not contribute to `TermSearch()` timing.

A cached query may bypass `TermSearch()`, in which case no corresponding
diagnostic appears. The `#<` query executes multiple independent term
searches; expect a separate summary for each term.
