/*-
File:           brat.hxx
Version:        1.00
Description:    Class BRAT - brat stand-off text annotations
Copyright:      Copyright (c) 2026 re-Isearch Project
                Licensed under the Apache 2.0 license
@@@-*/

#ifndef BRAT_HXX
#define BRAT_HXX

#include "doctype.hxx"

class BRAT : public DOCTYPE {
public:
  BRAT(PIDBOBJ DbParent, const STRING& Name);

  const char *Description(PSTRLIST List) const;

  void ParseRecords(const RECORD& FileRecord);
  void ParseFields(PRECORD NewRecord);

  ~BRAT();

private:
  void AddField(PRECORD Record, const STRING& FieldName,
                const FCT& Fct);

  STRING AnnotationExt;
  bool   ByteOffsets;
};

typedef BRAT* PBRAT;

#endif /* BRAT_HXX */
