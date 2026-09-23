#pragma once

#ifndef CGLOMD_HXX
#define CGLOMD_HXX

#include "markdown.hxx"

class CGLOMD : public MARKDOWN {
public:
  CGLOMD(PIDBOBJ DbParent, const STRING& Name)
    : MARKDOWN(DbParent, Name) {}

  const char *Description(PSTRLIST List) const override;
  void ParseRecords(const RECORD& FileRecord) override;
};

#endif
