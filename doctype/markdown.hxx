/*-
File:           markdown.hxx
Version:        1.00
Description:    Class MARKDOWN - Markdown Document Type
Copyright:      Copyright (c) 2026 re-Isearch Project
                Licensed under the Apache 2.0 license

Notes:
  This is intentionally an indexing-oriented Markdown parser, not a renderer.
  It recognizes ATX headings (H1..H6) as nested section fields and otherwise
  lets the normal DOCTYPE word parser handle the document text.
@@@-*/

#ifndef MARKDOWN_HXX
#define MARKDOWN_HXX

#include "doctype.hxx"
#include "HTMLEntities.hxx"

class MARKDOWN : public DOCTYPE {
public:
  MARKDOWN(PIDBOBJ DbParent, const STRING& Name);

  const char *Description(PSTRLIST List) const;

  void ParseFields(PRECORD NewRecord);

  GPTYPE ParseWords(UCHR *DataBuffer, GPTYPE DataLength,
                    GPTYPE DataOffset, GPTYPE *GpBuffer,
                    GPTYPE GpLength);

  INT GetTerm(const STRING& Filename, CHR *Buffer,
              off_t Offset, size_t Length);

  void SourceMIMEContent(PSTRING StringPtr) const;

  ~MARKDOWN();

private:
  void AddField(PRECORD Record, const STRING& Name, GPTYPE Start, GPTYPE End);
  void AddSection(PRECORD Record, int Level, GPTYPE Start, GPTYPE End);

  // Zap obvious raw HTML markup in-place without changing byte offsets.
  // Angle-bracket email and URI autolinks are deliberately preserved.
  static void ZapHtmlTags(UCHR *Buffer, GPTYPE Length);

  HTMLEntities Entities;
  STRING HeadingSeparator;
  bool NormalizeEntities;
  bool IgnoreHTMLTags;
};

typedef MARKDOWN* PMARKDOWN;

#endif /* MARKDOWN_HXX */
