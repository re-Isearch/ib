# Source spans, fields, and types in CoreQuarry (IB)

A field is a searchable interpretation of a span of source, **not an
exclusive owner of the text in that span**.

That simple distinction explains much of IB's design: structured search,
overlapping annotations, typed field indexes, and vector search can all refer
to the same original material without making the material itself belong to
only one indexing scheme.

## Three independent concerns

1. **Source** — the original record and its byte coordinates. The source is
   not rewritten into a separate universe for each search method.
2. **Field** — a named interval (or set of intervals) over that source.
   Fields may nest, coincide, or overlap. A path such as
   `DOC\SECTION\PARA` describes a structural location, not ownership of
   its bytes.
3. **Field type** — the interpretation and indexing/search machinery bound
   to a field, such as text, date, numeric, or HNSW.

Field definitions combine a name and a type; see `DFD` and `FIELDOBJ`.
The type controls a field-restricted query. It does not prevent the same bytes
from participating in other fields or in the ordinary lexical index.

A useful shorthand is:

> The source determines *where*; the field determines *which span*; the
> field type determines *how that field is searched*.

## One source, multiple interpretations

Consider:

```xml
<A>
  some text
  <HNSW>some vector text</HNSW>
  more text
</A>
```

The words inside `HNSW` are still source text. They remain in the lexical
index, including under the enclosing `A` context. With no field
restriction, a search for `vector` is an ordinary lexical search.
A field restriction to `A` can also locate that word lexically.

But a query restricted to the **HNSW-typed field** dispatches to its vector
index, using the field's declared type. For example, if the example's HNSW
field is indexed under that name:

```text
HNSW/"some vector text"
```

No special vector-search operator is required. This is the same principle
used for numeric and date fields: the field type and the supplied query value
jointly determine the applicable search behavior. A query such as `Feb`
against a date field can fall back to lexical interpretation when it is not
a date value the date machinery can use. A vector field accepts ordinary
text as model input, so that particular input-validation fallback is not
needed.

If a use case needs lexical **field-restricted** search over exactly the
HNSW span, an overlapping field—for example `HNSW_CONTENT`—can be defined
with the same source coordinates and a text type:

```text
HNSW/"some vector text"       # HNSW interpretation
HNSW_CONTENT/vector           # lexical interpretation of the same bytes
```

The second field could instead have another suitable type. Nothing about
the source span itself enforces one exclusive interpretation.

## Overlapping and nested fields are deliberate

Fields need not partition a document. They can nest as XML elements do,
or describe the same interval from independent points of view. A span
representing an annotation can overlap a paragraph, a named entity, or a
typed value without duplicating the source.

This is why formats such as BRAT fit the model naturally: annotations are
labelled source spans, and their coordinates can coexist with structural
fields and text search. The substantial benefit is not merely importing
annotations; it is being able to search their spans alongside the existing
field and positional machinery.

Structure still matters. For example:

```xml
<identity>
  <name>Edward</name>
  <organization>
    <name>Nonmonotonic</name>
    <address>...</address>
  </organization>
</identity>
```

The two `name` elements are close in serialized bytes but have different
structural roles. The address is inside the organization; proximity alone
does not establish that it is the person's address. XML byte order is not
a substitute for hierarchy or a claim of semantic association.

Queries and evidence displays should therefore preserve structural identity,
rather than treating all nearby words as a single relationship.

## Implications for evidence

A result already carries hits, spans, scores, and other metadata. Evidence
display interprets that existing data; it is not another retrieval or
ranking stage.

Two pieces of evidence can refer to identical or overlapping source
coordinates while having different provenances—for example, a lexical hit
and a vector hit in the same paragraph. Presenting the text twice costs
attention and, for agents, tokens without adding source information.

The display can describe their coordinate relationship:

- **Contains / within** — one evidence interval encloses the other.
- **Overlaps** — the intervals intersect without containment.
- **Near** — the intervals are disjoint but separated by no more than a
  configurable byte gap.
- **Separate** — the intervals are farther apart.

These are claims about **byte coordinates**, not semantic alignment. For
`near`, the display can additionally show the narrowest *verified common
containing field instance* using IB's existing structural machinery. It
should not infer a common parent merely from equal path prefixes: distinct
instances of the same field path may refer to different entities. The
common-container annotation is useful specifically when spans are nearby
but disjoint; it need not clutter the other relationships.

The purpose differs by audience but not by algorithm: an agent avoids
spending tokens rereading identical source, while a human gets a compact
guide to which passages might merit inspection. Nothing in these
annotations changes what was found, how it scored, or how it was ranked.

See [Evidence.md](Evidence.md) for the existing hit, evidence-cover,
container, and display-extent machinery.

## A note on the `@` suffix

`@` is **not** the switch for vector search. It is reserved for a proposed
cross-field vector relevance-feedback mechanism: roughly, using a vector
associated with one field to search another vector-indexed field. Its exact
syntax and semantics have not been settled, and that mechanism is not yet
implemented.

For normal HNSW queries, use ordinary fielded query syntax, as for every
other declared field type. Introducing a special operator just to select
vector search would break the uniform field-type dispatch model.

## Design principle

**Keep source coordinates, field identity, and field-type interpretation
distinct.** Their independence makes heterogeneous indexing possible
without sacrificing ordinary lexical access, structural context, or
faithful evidence display.
