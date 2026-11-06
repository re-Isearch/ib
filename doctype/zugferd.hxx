/* Copyright (c) 2026 Project re-Isearch and its contributors. Apache-2.0. */
#ifndef IB_ZUGFERD_HXX
#define IB_ZUGFERD_HXX
#include "xml.hxx"
#include <string>

class ZUGFERD : public XML {
public:
  ZUGFERD(PIDBOBJ parent, const STRING& name);
  const char* Description(PSTRLIST list) const override;
  void ParseRecords(const RECORD& record) override;
  // False means this is an ordinary PDF. True means handled or a rejected
  // invoice; an invalid invoice must not silently become PDF text instead.
  bool ParseIfInvoice(const RECORD& record);
  void SourceMIMEContent(PSTRING value) const override;
  void SourceMIMEContent(const RESULT&, PSTRING value) const override;
  bool GetResourcePath(const RESULT& result, PSTRING path) const override;
  void DocPresent(const RESULT& result, const STRING& element, const STRING& syntax,
                  PSTRING output) const override;
  void Present(const RESULT& result, const STRING& element, PSTRING output) const override;
  void Present(const RESULT& result, const STRING& element, const STRING& syntax,
               PSTRING output) const override;
private:
  size_t max_xml_bytes;
  bool StoreInvoice(const RECORD& source, const std::string& xml);
  bool ReadSource(const RESULT& result, PSTRING output) const;
};
#endif
