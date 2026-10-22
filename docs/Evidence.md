# Evidence covers and evidence display in IB

IB derives an excerpt from the match locations in a search result. It first
ranks spans of evidence, then identifies a containing field, then chooses the
bytes to display. The caller can retain the evidence span and its structural
identity separately from the rendered excerpt.

This guide describes the active implementation in `re-Isearch/ib`, reviewed at
revision `183abca623e8eb46af47ec0534cfd7af0989c12b`. The examples assume the
standard build, with hit source identity enabled.

## Quick start

Show evidence for each returned record with a target of 130 source bytes:

```sh
../bin/Isearch -d /path/to/database -top 5 -show -advice 130 -rpn latin bavarian AND
```

| Option | Meaning |
| --- | --- |
| `-show` | Display the selected evidence excerpt for each result that has hits. |
| `-advice N` | Set the source-byte attention budget. The default is `130`. Use a nonnegative integer. |
| `-advice 0` | Select the strongest evidence cover without a length penalty or context expansion. Structural identity is still discovered. |
| `-top 5` | Limit the number of returned records; this does not limit the number of evidence covers evaluated within a record. |

The normal `-show` path calls `Context()` and uses VT100 markers: lexical hits
are bold and nonlexical evidence is underlined. The field/container name is
printed beside `Evidence:`. The `-XML -show` path uses the separate
`XMLContext()` implementation; it does not use the selection described here.

## 1. Hits, covers, containers, and excerpts

A search result has a hit table. Each hit has an inclusive byte interval, `FC`,
and normally a source identifier, `SourceId`.

| Item | Meaning |
| --- | --- |
| Lexical hit | A hit with a nonzero `SourceId`, identifying lexical provenance supplied by the search engine. |
| Anonymous/nonlexical hit | A hit with `SourceId == 0`, such as a date, numerical, or vector match with a field-coordinate extent. |
| Evidence cover | A continuous interval enclosing a candidate combination of hits. It carries coverage and compactness measurements. |
| Structural container | A known indexed field or ancestor containing the selected evidence, such as `LINE`, `SPEECH`, or `TEXT`. |
| Display extent | The interval actually read to produce the excerpt. It may include context around the evidence. |

`SourceId` is the identity used by this algorithm. The code does not compare the
spelling of displayed terms to decide whether two hits are independent. Repeated
occurrences with the same source ID provide alternative locations; they do not
increase the number of evidence dimensions within a cover.

Coordinates returned by `RESULT` are zero-based and relative to the beginning
of the record. Ends are inclusive:

```text
width = end - start + 1
source-file offset = RESULT::GetRecordStart() + local start
```

These are byte coordinates, not character, word, or LLM-token counts. A cover is
one continuous span, including any intervening text; it is not a concatenation
of several disconnected excerpts.

## 2. Building evidence covers

`RESULT::GetEvidenceCovers(Max)` builds and ranks candidate spans from the hit
table. It returns an empty vector when there are no candidates.

The implementation:

1. Separates lexical hits from anonymous hits and sorts both groups by position.
2. Counts the distinct nonzero lexical source IDs, `L`.
3. Determines the number of possible evidence dimensions, `D`, from `AuxCount`
   and the available source identities.
4. Uses a sliding window to find compact lexical windows for each distinct-source
   count `K`, from one through `L`.
5. Combines each lexical window with each anonymous hit as an alternative.
6. Adds anonymous-only candidates, collapses identical extents, and ranks them.

The denominator is calculated as follows:

```text
D = max(RESULT::GetAuxCount(), L)
if anonymous hits exist and D <= L:
    D = L + 1
D = max(D, 1)
```

`AuxCount` is result metadata supplied by query evaluation. Consequently, `D`
reflects the evidence represented by this result and its hit provenance; it is
not necessarily the number of words typed in the original query.

For a multi-evidence result, lexical-only windows are retained when they contain
at least two distinct sources. One-source lexical windows are retained on their
own when `D == 1`; otherwise they are used internally for combinations with
anonymous evidence. The display selector also considers individual hits, as
explained below.

All anonymous hits currently share one anonymous evidence dimension. A lexical
window can therefore gain **one** evidence item by combining with an anonymous
hit. Different date, numeric, or vector extents are alternatives; this code does
not accumulate several independent anonymous dimensions into a single cover.

Candidates with identical start and end coordinates are collapsed to the one
with the most participating evidence. Distinct extents can overlap and remain
in the returned list.

### Coverage, dispersion, and rank

For a candidate with width `W`, participating evidence count `n`, and denominator
`D`:

```text
Energy      = n / D
Dispersion  = W / n
compactness = 1 / (1 + Dispersion / DispersionDecay)
rank        = Energy^EvidenceWeight * compactness^DispersionWeight
```

`Energy` measures coverage of the represented evidence dimensions. `Dispersion`
is bytes of span per participating dimension. A smaller dispersion means the
evidence is more concentrated.

The width includes the hit intervals themselves as well as the space between
them. A broad nonlexical hit covering an entire paragraph therefore has a larger
dispersion than a narrow field hit, even if both contain the same lexical hits.
A single hit also has nonzero dispersion: its width.

This rank selects passages **within one result**. It does not read the result's
retrieval score, term weights, or vector similarity. `Energy` here is a coverage
ratio, not a relevance probability or the calibrated vector evidence described
in [HybridEnergy.md](https://github.com/re-Isearch/ib/blob/183abca623e8eb46af47ec0534cfd7af0989c12b/docs/HybridEnergy.md).

Covers are ordered by descending rank. Ties prefer higher energy, then smaller
dispersion, then an earlier starting position.

### Cover-ranking parameters

These are currently `static const` values in `src/result.cxx`. They are not
runtime API arguments, command-line options, or database-profile settings.
Changing them requires changing the source and rebuilding.

| Parameter | Current value | Effect |
| --- | --- | --- |
| `EvidenceWeight` | `1.5` | Exponent on coverage. Increasing it penalizes partial coverage more strongly relative to complete coverage. |
| `DispersionWeight` | `1.0` | Exponent on compactness. Increasing it penalizes dispersed spans more strongly. |
| `DispersionDecay` | `32.0` bytes per evidence dimension | Sets the compactness scale. At dispersion `32`, compactness is `0.5`. Increasing this value makes wider spans less costly. |

The `[E2]` settings `CoverageFloor` and `ProximityGain` belong to result scoring.
They do not configure these cover-ranking constants.

## 3. Selecting display evidence

`RESULT::GetDisplayEvidence(MaxBytesAdvice, DoctypePtr)` returns a
`DISPLAY_EVIDENCE` structure. It first chooses the evidence, then discovers its
structural identity, then determines the presentation envelope.

### Unbounded selection: `MaxBytesAdvice == 0`

The selector uses the highest-ranked cover from `GetEvidenceCovers(0)`. If no
cover was produced but hits exist, it falls back to the first hit. It still looks
up the containing field when a suitable doctype/database is available.

`DisplayExtent` remains exactly the chosen evidence extent. Zero does not request
the whole record or expansion to the whole containing field. The strongest cover
can itself represent partial evidence; zero does not force complete coverage.

### Bounded selection: `MaxBytesAdvice > 0`

Full covers, partial covers, and each individual hit compete. Each individual
hit is scored as one evidence item with the same denominator `D` used above.
This allows a useful local match to win when a joint cover is too expensive to
show.

For candidate width `W` and byte advice `B`, the selection utility is:

```text
overage = max(0, (W - B) / B)
cost    = DisplayOverageWeight * overage^DisplayOverageExponent
utility = cover rank - cost
```

There is no overage cost at or below the advice. A candidate can exceed the
advice and still win if its evidence rank justifies the extra bytes. Utility can
be negative; the selector chooses the best available candidate rather than
requiring a positive value.

Ties prefer higher energy, smaller dispersion, shorter extent, and then an
earlier starting position.

| Parameter | Current value | Effect |
| --- | --- | --- |
| `MaxBytesAdvice` / `B` | Supplied by caller; `Context()` defaults to `130` | Source-byte attention budget. Zero disables length cost and presentation expansion. |
| `DisplayOverageWeight` | `0.25` | Multiplies the over-budget cost. Increasing it makes oversized evidence less attractive. |
| `DisplayOverageExponent` | `2.0` | Sets the cost curve. The current quadratic curve makes large overruns increasingly expensive. |
| `DisplayHardLimitFactor` | `4.0` | Candidates wider than `4 * B` receive sentinel utility `-MAXFLOAT`. Ordinary candidates outrank them. |
| `DisplayStructureBonus` | `0.05`, currently inactive | The helper supports a structural bonus, but active evidence selection always calls it with `structural == false`. Containers are considered afterward. |

The overage and hard-limit constants are also source constants, not profile or
CLI settings. The hard limit is a selection guardrail, not a guaranteed output
cap: if every candidate receives sentinel utility, the current selector can
still choose one. The selected evidence is not cut in half to enforce a limit.

### Worked example

Suppose a result represents two lexical dimensions (`D = 2`) and one individual
hit is five bytes wide. With the current weights:

```text
single-hit energy     = 1 / 2 = 0.5
single-hit dispersion = 5 / 1 = 5
single-hit rank        = 0.5^1.5 / (1 + 5/32) = approximately 0.3058
```

With `B = 130`, compare that single hit with a cover containing both dimensions:

| Two-source cover width | Energy | Dispersion | Cover rank | Overage cost | Utility | Winner against the five-byte single |
| --- | --- | --- | --- | --- | --- | --- |
| `60` bytes | `1.0` | `30` | `0.5161` | `0` | `0.5161` | Two-source cover |
| `140` bytes | `1.0` | `70` | `0.3137` | `0.0015` | `0.3122` | Two-source cover, despite exceeding advice |
| `300` bytes | `1.0` | `150` | `0.1758` | `0.4275` | `-0.2517` | Single hit, whose utility remains `0.3058` |

These are alternative scenarios, assuming no other candidate has higher utility.
The table describes evidence selection before context expansion.

## 4. Discovering the structural container

After choosing the evidence, the selector asks where it lives. With a doctype
connected to its database and usable record metadata, it:

1. Chooses an anchor inside the evidence, preferring a contained lexical hit.
2. Converts the anchor to global index coordinates and calls `GetPeerFc()` to
   discover a containing field.
3. Uses that field if it contains the entire evidence span.
4. Otherwise tries exact indexed ancestors along the discovered field path.
5. Saves the accepted container in record-relative coordinates.

Ancestor lookup follows defined field names. `GetFieldPath()` recognizes
backslash, slash, vertical bar, and colon as path separators. The final root
member is deliberately excluded from the ancestor lookup. A field can therefore
have no accepted container when the evidence crosses its boundaries. A dotted
name alone does not establish ancestry in this lookup.

Structural discovery uses the indexed field coordinates and available field
paths; it does not parse a new document tree during display. The returned
container is the smallest one found by this discovery path, rather than a
promise to search every overlapping annotation or possible hierarchy.

`ContainerName` and `ContainerExtent` describe the evidence even when the
container is too large to display. They can identify a speech, paragraph, or
field for a caller that wants to retrieve more context separately.

### Choosing the presentation envelope

For positive byte advice:

- If the accepted container fits within the advice, display the whole container.
- Otherwise, expand around the selected evidence toward the advised width,
  staying inside the accepted container if one is known, or inside the record.
- Allocate roughly half the extra space to the left and the remainder to the
  right. Reuse space on the other side when a boundary prevents that allocation.
- If the evidence already reaches or exceeds the advice, keep it intact.

For example, with a 130-byte advice and a 20-byte evidence span, a containing
90-byte `LINE` is displayed in full. A containing 400-byte `SPEECH` is retained
as the structural identity, while the display normally expands to a 130-byte
window within that speech. Near a record or field boundary, less context may be
available.

Structure does not rerank the evidence during this process. Discovering a large
container does not cause the selector to switch to another match.

## 5. Rendering and markers

`PresentDisplay()` reads `DisplayExtent` from the record's source file through
`GetRecordData()`. When supplied, the doctype provides its record-reading
behavior, including format-specific normalization such as entity handling.
Without a doctype, the generic reader reads source bytes directly.

The renderer then marks **all fully contained hits** from the result's hit
table, including hits that did not determine the winning cover. A hit crossing
the display boundary is not partially marked. Marker insertion is ordered by
byte boundary so that an enclosing nonlexical extent can contain lexical marks.

`DISPLAY_MARKER` has four literal delimiter strings:

| Member | Meaning |
| --- | --- |
| `LexicalStart` | Insert before a fully contained hit with nonzero `SourceId`. |
| `LexicalEnd` | Insert after that lexical hit. |
| `EvidenceStart` | Insert before a fully contained anonymous/nonlexical hit. |
| `EvidenceEnd` | Insert after that anonymous/nonlexical hit. |

The evidence delimiters mark individual anonymous hit intervals. They do not
wrap the whole selected cover merely because it is a cover.

Built-in marker styles are available through `RESULT::GetDisplayMarkers()`:

| Style | Enum value | Lexical delimiters | Nonlexical delimiters |
| --- | --- | --- | --- |
| `DisplayMarkerNone` | `0` | Empty | Empty |
| `DisplayMarkerText` | `1` | `\|\|` and `\|\|` | `[` and `]` |
| `DisplayMarkerVT100` | `2` | `ESC[1m` and `ESC[22m` (bold) | `ESC[4m` and `ESC[24m` (underline) |
| `DisplayMarkerMarkDown` | `3` | `**` and `**` | `_` and `_` |

For example, text markers can produce `[||Latin|| ... ||Bavarian||]` when a
nonlexical hit encloses both lexical hits. With lexical hits alone, there are
no enclosing evidence brackets.

`DisplayMarkerNone` disables markers while retaining doctype reading behavior.
Selecting Markdown changes the inserted delimiters; it does not convert or
escape the source text into Markdown.

## 6. API reference

### `GetEvidenceCovers(size_t Max) const`

Returns ranked `EVIDENCE_COVER` values. `Max == 0` returns all covers; a positive
value limits the number returned. This limit is applied after candidate
construction and sorting, so it is not a work or memory budget.

Each cover contains:

| Member | Meaning |
| --- | --- |
| `extent` | Inclusive record-relative byte interval enclosing the evidence. |
| `Energy` | Participating evidence count divided by `D`. |
| `Dispersion` | Inclusive span width divided by the participating evidence count. |

`GetBestContextHit()` returns the extent of the first ranked cover, or an empty
`FC` if no cover was produced. It does not perform display-budget selection or
structural expansion.

### `GetDisplayEvidence(size_t MaxBytesAdvice, DOCTYPE *DoctypePtr) const`

Returns the chosen evidence and presentation metadata without reading the
excerpt text.

| Parameter | Meaning |
| --- | --- |
| `MaxBytesAdvice` | Zero for selection without a length penalty or expansion; positive for budget-aware selection and context expansion. |
| `DoctypePtr` | The record's doctype. Its database supplies metadata and field coordinates for structural discovery. `NULL` leaves that discovery unavailable. |

The overload `GetDisplayEvidence(DoctypePtr)` uses zero advice. There is no
implicit 130-byte advice in this selector; the 130-byte default comes from
higher-level presentation wrappers such as `Context()`.

| Returned member | Meaning |
| --- | --- |
| `Cover` | Winning `EVIDENCE_COVER`; its metrics describe the evidence span. |
| `DisplayExtent` | Bytes to read for the excerpt, including any chosen context. |
| `ContainerExtent` | Discovered containing field interval, possibly much larger than the excerpt. |
| `ContainerName` | Discovered field or path name; empty if none was accepted. |
| `HasContainer()` | True when `ContainerName` is nonempty. |

Check `DisplayExtent.IsEmpty()` before using a selection or its cover metrics.
An empty hit table or an invalid record interval produces no usable selection.
`HasContainer()` is not a test for whether evidence exists.

### `PresentBestDisplayEvidence(...) const`

The marker-based overload combines selection and rendering:

```cpp
bool PresentBestDisplayEvidence(
    size_t MaxBytesAdvice,
    const DISPLAY_MARKER& Marker,
    STRING* StringBuffer,
    DOCTYPE* DoctypePtr = NULL,
    STRING* TagPtr = NULL) const;
```

| Parameter | Meaning |
| --- | --- |
| `MaxBytesAdvice` | Advice passed to the display selector. |
| `Marker` | Lexical and anonymous-hit delimiters. |
| `StringBuffer` | Required output buffer for the rendered excerpt. |
| `DoctypePtr` | The record's doctype, used for structural lookup and source reading. |
| `TagPtr` | Optional output for the container name. When supplied, the excerpt has no automatic field-name prefix. |

When `TagPtr` is omitted, this overload prefixes successful output with
`[ContainerName] `. The current implementation also inserts `[] ` when no
container name was found. Pass a tag buffer to keep the label separate.

The return value reports presentation success. Invalid/empty display ranges,
a null output buffer, or failure to read source data cause presentation to fail.

### `PresentDisplay(...) const`

Use the following overload to render a previously selected interval:

```cpp
bool PresentDisplay(
    const FC& Range,
    STRING* StringBuffer,
    const DISPLAY_MARKER& Marker,
    DOCTYPE* DoctypePtr = NULL) const;
```

`Range` is a nonempty record-relative interval contained in the record.
`PresentDisplay()` does not select evidence, expand context, or add a container
label. An overload accepts `DISPLAY_MARKER_STYLE` instead of a marker structure.
Use an explicit style to make the desired delimiters clear.

### `IDB::Context()` and `VIDB::Context()`

These wrappers obtain the appropriate doctype and call the marker-based
presentation method. `VIDB` first routes the result to its physical database.

```cpp
bool Context(
    const RESULT& ResultRecord,
    STRING* Line,
    const DISPLAY_MARKER& Marker,
    STRING* TagName = NULL,
    size_t MaxAdvice = 130);
```

`Line` receives the excerpt and `TagName`, when supplied, receives the container
name separately. `MaxAdvice` has the same meaning as `MaxBytesAdvice`.

## 7. C++ examples

The fragments below assume a result already retrieved from an `IDB` named `db`.
Include `common.hxx` before the database headers, followed by `idb.hxx` and
`result.hxx`, as in the existing IB clients.

```cpp
STRING excerpt;
STRING field;
const DISPLAY_MARKER& markers =
    RESULT::GetDisplayMarkers(DisplayMarkerMarkDown);

bool ok = db.Context(result, &excerpt, markers, &field, 240);
```

This selects and renders with a 240-byte advice, keeping the field label
separate from the excerpt.

To retain evidence coordinates and structural metadata as well as display text:

```cpp
DOCTYPE* doctype = db.GetDocTypePtr(result.GetDocumentType());
DISPLAY_EVIDENCE selected = result.GetDisplayEvidence(240, doctype);

if (!selected.DisplayExtent.IsEmpty()) {
    STRING excerpt;
    bool ok = result.PresentDisplay(
        selected.DisplayExtent, &excerpt, DisplayMarkerMarkDown, doctype);

    // selected.Cover.extent: the evidence that won
    // selected.DisplayExtent: the excerpt interval
    // selected.ContainerName / ContainerExtent: its structural identity
}
```

To inspect candidate covers without rendering:

```cpp
EVIDENCE_COVERS bestThree = result.GetEvidenceCovers(3);
EVIDENCE_COVERS allCovers = result.GetEvidenceCovers(0);
```

## 8. Byte-budget and provenance limits

Advice is measured before marker strings or a container label are inserted.
The rendered string can therefore be longer than the advice even when the
selected source interval fits it. Doctype processing can also affect the
relationship between source bytes and visible text.

The positional context envelope is calculated in bytes. The current selector
does not align its arbitrary context edges to word, sentence, or UTF-8 character
boundaries. Natural field boundaries are used when the entire accepted field
fits; arbitrary padded excerpts do not carry that boundary guarantee.

Nonlexical coverage depends on actual hit extents: a vector or typed match with
no coordinates cannot supply a passage location. Anonymous provenance is
currently grouped into one dimension, as described above. The returned cover is
a heuristic explanation of the result's available hits, not a proof of every
Boolean, positional, or semantic condition in the original query.

## Implementation references

The links below pin the implementation described by this guide:

- [RESULT structures and public methods](https://github.com/re-Isearch/ib/blob/183abca623e8eb46af47ec0534cfd7af0989c12b/src/result.hxx)
- [Cover construction, display selection, markers, and rendering](https://github.com/re-Isearch/ib/blob/183abca623e8eb46af47ec0534cfd7af0989c12b/src/result.cxx)
- [Hit coordinates and source identity](https://github.com/re-Isearch/ib/blob/183abca623e8eb46af47ec0534cfd7af0989c12b/src/fchits.hxx)
- [Result conversion to record-relative coordinates](https://github.com/re-Isearch/ib/blob/183abca623e8eb46af47ec0534cfd7af0989c12b/src/irset.cxx)
- [IDB context wrapper and peer/container lookup](https://github.com/re-Isearch/ib/blob/183abca623e8eb46af47ec0534cfd7af0989c12b/src/idb.cxx)
- [Indexed ancestor paths](https://github.com/re-Isearch/ib/blob/183abca623e8eb46af47ec0534cfd7af0989c12b/src/dfdt.cxx)
- [Isearch options and evidence output](https://github.com/re-Isearch/ib/blob/183abca623e8eb46af47ec0534cfd7af0989c12b/src/isearch_main.cxx)
