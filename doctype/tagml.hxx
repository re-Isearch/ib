#pragma once
/*-
File:           tagml.hxx
Version:        1.00
Description:    Class TAGML - Text-As-Graph Markup Language
Copyright:      Copyright (c) 2026 re-Isearch Project
                Licensed under the Apache 2.0 license
@@@-*/

#ifndef TAGML_HXX
#define TAGML_HXX

#include "doctype.hxx"

class TAGML : public DOCTYPE {
public:
  TAGML(PIDBOBJ DbParent, const STRING& Name);

  const char *Description(PSTRLIST List) const;

  void ParseRecords(const RECORD& FileRecord);
  void ParseFields(PRECORD NewRecord);

  GPTYPE ParseWords(UCHR *DataBuffer, GPTYPE DataLength,
                    GPTYPE DataOffset, GPTYPE *GpBuffer,
                    GPTYPE GpLength);

  INT GetTerm(const STRING& Filename, CHR *Buffer,
              off_t Offset, size_t Length);

  void Present(const RESULT& ResultRecord, const STRING& ElementSet,
               PSTRING StringBufferPtr) const;

  void SourceMIMEContent(PSTRING StringPtr) const;

  ~TAGML();

private:
  void AddField(PRECORD Record, const STRING& FieldName,
                GPTYPE Start, GPTYPE End);
  void AddField(PRECORD Record, const STRING& FieldName,
                const FCT& Fct);

  static void BlotSyntax(UCHR *Buffer, GPTYPE Length);

  STRING CommentField;
};

typedef TAGML* PTAGML;

#endif /* TAGML_HXX */
