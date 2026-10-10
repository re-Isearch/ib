/*-@@@
File:		ris.hxx
Version:	1.00
Description:	Class RISLINE - RIS 
Author:		Edward C. Zimmermann, edz@nonmonotonic.net
Copyright:	CoreQuarry.com
@@@-*/

#ifndef RISLINE_HXX
#define RISLINE_HXX

#ifndef DTREG_HXX
# include "defs.hxx"
# include "doctype.hxx"
#endif
#include "medline.hxx"

class RISLINE :  public MEDLINE {
public:
  RISLINE(PIDBOBJ DbParent, const STRING& Name) : MEDLINE (DbParent, Name) {}
  const char *Description(PSTRLIST List) const {
    const STRING ThisDoctype("RISLINE");
    if (Doctype != ThisDoctype && List->IsEmpty())
      List->AddEntry(Doctype);
    List->AddEntry (ThisDoctype);
    DOCTYPE::Description(List);
    return "Research Information Systems (RIF) Document Type";
  }
  void SourceMIMEContent(PSTRING StringBuffer) const {
    *StringPtr = "application/x-research-info-systems"; 
  }

};
typedef RISLINE* RISLINE;


#endif
