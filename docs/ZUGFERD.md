# ZUGFeRD / Factur-X invoices

`ZUGFERD` is an XML-derived doctype for hybrid PDF invoices. It indexes the
embedded invoice XML and retains the PDF as the original resource. `FACTUR-X`
is an alias. With the attachment backend enabled, the existing `PDF` doctype
automatically hands recognized invoices to `ZUGFERD` before running its PDF
text filter.

No PDF page rendering, OCR, Poppler, or external extraction process is involved
in the invoice path. Ordinary PDFs continue to use the configured PDF filter.

## Build

Install the development packages for **qpdf 11.0 or newer** and **Expat**, then
configure the normal IB build with:

```sh
cmake -S . -B build -DIB_ZUGFERD=ON
cmake --build build
```

For example, Homebrew supplies these as `qpdf` and `expat`. A nonstandard
installation can be located with `QPDF_ROOT` or `CMAKE_PREFIX_PATH`.

| CMake option | Behavior |
| --- | --- |
| `IB_ZUGFERD=AUTO` (default) | Enable when both dependencies are available; otherwise keep the normal PDF path. |
| `IB_ZUGFERD=ON` | Require the dependencies; fail configuration if unavailable. |
| `IB_ZUGFERD=OFF` | Build without invoice attachment support. Explicit ZUGFERD input reports that support is unavailable. |
| `IB_BUILD_ZUGFERD_TESTS=ON` | Build the focused extraction and doctype regression test. Requires the backend. |

## Index and search

For a collection known to contain invoice PDFs:

```sh
Iindex -d invoices -locale de.UTF-8 -t ZUGFERD invoice.pdf
```

For a mixed collection, select `PDF`; recognized invoice PDFs are indexed as
`ZUGFERD` and the remaining PDFs follow the existing text-filter path:

```sh
Iindex -d documents -locale de.UTF-8 -t PDF invoice.pdf ordinary.pdf
```

Invoice XML uses UTF-8. Select a UTF-8 database locale (as above), including
when appending to an existing UTF-8 index. The database locale controls
lexical collation; it must agree with the input encoding.

The extracted XML is parsed using the normal XML field and span machinery.
Element names and namespace prefixes remain those of the source XML. Invoice
identifiers, parties, line items, totals, and other XML content can therefore
be searched and retrieved through the existing XML interfaces. This doctype
does not assign invoice-specific numeric/date field types automatically;
configure those using the normal XML field configuration when required.

For example, search the invoice text and show field-based evidence with:

```sh
Isearch -d invoices -max 10 -show Lieferant
```

The decoded XML limit is **33,554,432 bytes (32 MiB)** per invoice. Override it
with the doctype option `MAXXMLBYTES`, for example:

```sh
Iindex -d invoices -locale de.UTF-8 -t ZUGFERD -o MAXXMLBYTES=67108864 invoice.pdf
```

It can also be set in the database INI file:

```ini
[ZUGFERD]
MAXXMLBYTES=67108864
```

The limit applies while the embedded stream is decoded, including compressed
streams. It must be positive and at most `INT_MAX`.

## Recognition and extraction

The reader resolves the PDF catalog's associated files (`/AF`) and document
attachment name tree (`/Names/EmbeddedFiles`). qpdf handles indirect objects,
cross-reference streams, object streams, and attachment stream filters.
The reader honors an inclusive `RECORD` start/end range, so an invoice PDF can
also be a bounded record within a concatenated source file.

Recognized attachment filenames (case insensitive) are:

- `factur-x.xml`
- `zugferd-invoice.xml` (including `ZUGFeRD-invoice.xml`)
- `xrechnung.xml`

A recognized name must contain well-formed UTF-8 XML with one of these roots,
regardless of the namespace prefix used:

| Root element | Namespace |
| --- | --- |
| `CrossIndustryInvoice` | `urn:un:unece:uncefact:data:standard:CrossIndustryInvoice:100` |
| `CrossIndustryDocument` | `urn:ferd:CrossIndustryDocument:invoice:1p0` |

An unrelated XML attachment does not make a PDF an invoice. The same stream
referenced by both attachment mechanisms is read once. Multiple distinct
invoice streams, malformed invoice XML, DTDs, and oversized XML are rejected
with a diagnostic. A recognized but invalid invoice does not silently fall
back to indexing the visual PDF text. Encrypted PDFs are unsupported by this
invoice reader.

This is format recognition and extraction, **not PDF/A, EN 16931, profile,
Schematron, signature, or business-rule validation**. It does not prove that a
document meets an invoicing regulation. Standalone invoice XML should use the
existing XML doctype; this reader handles hybrid PDF/CII invoices, not UBL.

## Original resource and presentation

The extracted XML is stored unchanged under the database's `.cat` catalog
directory and indexed from byte zero. The original path is retained using
the FILTER resource mechanism: a `.path` sidecar stores the original source
path, PDF MIME type, and source record range. `RECORD::OrigPathname` also
retains the source path. Relative-path mode is supported. Cache filenames are
generated from the source inode/range, never from an attachment name or
caller-supplied key.

Keep the original PDF (or source container) available for original-resource
retrieval; the XML cache is not a second copy of the PDF.

| Element set | SUTRS | HTML transport |
| --- | --- | --- |
| `PDF` | Original PDF bytes | Original PDF with `application/pdf` header |
| `XML` | Exact extracted XML bytes | Extracted XML with `application/xml` header |
| `SOURCE` (`R_`) | Original PDF | Extracted XML, following FILTER presentation conventions |
| `FULLTEXT` (`F_`) | Extracted XML | Original PDF, following FILTER presentation conventions |
| `$Path` | Original PDF/container path | Original PDF/container path |
| `BRIEF` (`B_`) | Original source basename | Original source basename |
| Other XML fields | Inherited XML presentation | Inherited XML presentation |

Use the explicit `PDF` and `XML` element sets when a caller needs the same
payload regardless of record syntax.

## Regression test

```sh
cmake -S . -B build -DIB_ZUGFERD=ON -DIB_BUILD_ZUGFERD_TESTS=ON
cmake --build build --target zugferd_test
ctest --test-dir build -R '^zugferd$' --output-on-failure
```

The test generates its own PDF fixtures. It exercises compressed and
uncompressed attachments, object streams, AF-only/name-tree-only containers,
Unicode filenames, ambiguous and invalid attachments, XML limits, PDF hand-off
without a filter, inherited XML fields, exact XML/PDF retrieval, relative
paths, bounded source ranges, and ordinary-PDF fallback.
It also builds and reopens a real UTF-8 IB index through `ZUGFERD`, `FACTUR-X`,
and automatic `PDF` detection, checks every posting against its source, and
searches an invoice value inside an XML field.

## Format references

- [FeRD ZUGFeRD FAQ](https://www.ferd-net.de/standards/zugferd-faq)
- [PDFlib: ZUGFeRD and Factur-X attachment structure](https://www.pdflib.com/pdf-knowledge-base/zugferd-and-factur-x/)
- [qpdf library documentation](https://qpdf.readthedocs.io/en/stable/library.html)
