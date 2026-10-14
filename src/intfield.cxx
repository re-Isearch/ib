/*
Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
#pragma ident  "%Z%%Y%%M%  %I% %G% %U% BSN"
/*@@@
File:		intfield.cxx
Version:	$Revision: 1.1 $
Description:	Class INTFIELD - Numeric interval data object
Author:		Archie Warnock (warnock@clark.net), A/WWW Enterprises
@@@*/


#include "intfield.hxx"
#include <array>

INTERVALFLD::INTERVALFLD()
{
  GlobalStart = (GPTYPE)-1;
}


// Copy Constructor
INTERVALFLD::INTERVALFLD(const INTERVALFLD& OtherField)
{
  GlobalStart = OtherField.GlobalStart;
  StartValue  = OtherField.StartValue;
  EndValue    = OtherField.EndValue;
}


INTERVALFLD INTERVALFLD::operator=(const INTERVALFLD& OtherField)
{
  GlobalStart = OtherField.GlobalStart;
  StartValue  = OtherField.StartValue;
  EndValue    = OtherField.EndValue;

  return *this;
}


void INTERVALFLD::Dump(ostream& os) const
{
  os << *this << endl;
}

ostream& operator<<(ostream& os, const INTERVALFLD& Fld)
{
 return  os << "ptr: " << Fld.GlobalStart << " [" << Fld.StartValue << "," << Fld.EndValue << "]";
}

INTERVALFLD::~INTERVALFLD()
{
}


void INTERVALFLD::Write(PFILE Fp) const
{
  putObjID(objFIXEDINTERVALFLD, Fp);
  ::Write(GlobalStart, Fp);
  ::Write(StartValue, Fp);
  ::Write(EndValue, Fp);
}

void INTERVALFLD::Write(PFILE Fp, GPTYPE Offset) const
{
  putObjID(objFIXEDINTERVALFLD, Fp);
  ::Write(GlobalStart+Offset, Fp);
  ::Write(StartValue, Fp);
  ::Write(EndValue, Fp);
}


void Write(const INTERVALFLD& s, PFILE Fp)
{
  s.Write(Fp);
}


bool INTERVALFLD::Read(FILE *Fp)
{
  StartValue = EndValue = NUMERICOBJ();
  std::array<BYTE, sizeof(GPTYPE)> gp;
  if (!Fp || getObjID(Fp) != objFIXEDINTERVALFLD ||
      fread(gp.data(), 1, gp.size(), Fp) != gp.size() ||
      !::Read(&StartValue, Fp) || !::Read(&EndValue, Fp) ||
      !StartValue.Ok() || !EndValue.Ok())
    {
      StartValue = EndValue = NUMERICOBJ();
      return false;
    }
  GlobalStart = getGPTYPE(gp.data(), 0);
  return true;
}

bool Read(INTERVALFLD *s, FILE *Fp)
{
  return s && s->Read(Fp);
} 


