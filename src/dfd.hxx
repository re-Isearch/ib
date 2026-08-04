/* Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE */
/*@@@
File:		dfd.hxx
Description:	Class DFD - Data Field Definition
@@@*/

#ifndef DFD_HXX
#define DFD_HXX

#include "attrlist.hxx"


// Describes how coordinates within one physical field index are arranged.
//
// Example:
//   <ROOT><A>one<A>two<A>three</A></A></A></ROOT>
//
// The unqualified A field contains nested coordinate ranges.
// The fully qualified ROOT/A/A/A field contains disjoint ranges.
//
// Used to select optimized field/proximity algorithms.
enum class DFD_FC_LAYOUT : uint8_t { Unknown     = 0, Disjoint    = 1, Nested      = 2, Overlapping = 3 };


class DFD {
public:
  DFD();
  DFD(const DFD& OtherDfd);

  DFD& operator=(const DFD& OtherDfd);

#if 1
  void    SetFieldObj(const FIELDOBJ& newFieldObj) {
    Attributes.AttrSetFieldObj(newFieldObj);
  }
  FIELDOBJ GetFieldObj() const {
    return Attributes.AttrGetFieldObj();
  }
  FIELDTYPE  GetFieldType() const {
    return Attributes.AttrGetFieldObj().GetFieldType();
  }
  bool GetFieldType(STRING *Typename) const {
    FIELDTYPE t = Attributes.AttrGetFieldObj().GetFieldType();
    if (t.Ok())
      *Typename = (STRING)t.c_str();
    else
      return false;
    return true;
  }


  bool        checkFieldName(const STRING& Fieldname) const;

  void        SetFieldName(const STRING& fieldName) {
#if 1
    Attributes.AttrSetFieldName(fieldName);
#else
    SetFieldObj (FIELDOBJ(fieldName, FIELDTYPE(FIELDTYPE::text)) );
#endif
  }
  STRING      GetFieldName() const {
    return Attributes.AttrGetFieldObj().GetFieldName();
  }
  bool GetFieldName(STRING *StringBuffer) const {
    return !(*StringBuffer = GetFieldName()).IsEmpty();
  }

  void        SetFieldType(const FIELDTYPE& NewFieldType) {
    Attributes.AttrSetFieldType(NewFieldType);
  }

#else
  void        SetFieldName(const STRING& NewFieldName) {
    Attributes.AttrSetFieldName(NewFieldName);
  }
  bool GetFieldName(STRING *StringBuffer) const;
  STRING      GetFieldName() const {
    return Attributes.AttrGetFieldName();
  }

  void        SetFieldType(const STRING& NewFieldType) {
    Attributes.AttrSetFieldType(NewFieldType);
  }
  bool GetFieldType(STRING *StringBuffer) const;
  STRING      GetFieldType() const {
    return Attributes.AttrGetFieldType();
  }
#endif

  void                SetFileNumber(const INT newFileNumber) {
    FileNumber = newFileNumber;
  }
  INT                 GetFileNumber() const { return FileNumber; }

  void                SetAttributes(const ATTRLIST& newAttributes) {
    Attributes = newAttributes;
  }
  void                GetAttributes(ATTRLIST *Ptr) const { *Ptr = Attributes; }
  const ATTRLIST     *GetAttributesPtr() const { return &Attributes; }

  void                SetFcLayout(DFD_FC_LAYOUT value) { FcLayout = value; }
  DFD_FC_LAYOUT       GetFcLayout () const             { return FcLayout;  }

~DFD();
private:
  // Persistent (created during indexing)
  uint16_t    FileNumber;
  ATTRLIST    Attributes;

  // Runtime-derived state; not part of the variable-length DFD dump.
  DFD_FC_LAYOUT FcLayout = DFD_FC_LAYOUT::Unknown;

};

typedef DFD* PDFD;

#endif
