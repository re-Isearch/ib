PIRSET INDEX::IntegerSearch(const INT16 key, const STRING& FieldName, INT4 Relation)
{
  PIRSET pirset = new IRSET(Parent);

  if (Parent == NULL)
    return NULL;

  const FIELDTYPE ft = Parent->GetFieldType(FieldName);

  if (!ft.IsInteger()) {
    Parent->SetErrorCode(113);
    return pirset;
  }

  STRING Fn;
  STRING TextFn;

  if (!Parent->DfdtGetFileName(FieldName, ft, &Fn))
    return pirset;

  if (!FileExists(Fn))
    return pirset;

  Parent->DfdtGetFileName(FieldName, &TextFn);

  INTEGERLIST List;

  INT4 Start = -1;
  INT4 End   = -1;

  SearchState status;

  if (Relation == ZRelNE)
    status = List.FindIndexes(Fn, key, ZRelEQ, &Start, &End);
  else
    status = List.FindIndexes(Fn, key,
                              (ZRelation_t)Relation,
                              &Start, &End);

  /*
   * != of a value absent from the index means everything in the field.
   */
  if (status == NO_MATCH) {
    if (Relation == ZRelNE) {
      pirset->Not(FieldName);
    }
    return pirset;
  }

  IRESULT result;

  result.SetVirtualIndex((UCHR)Parent->GetVolume(NULL));
  result.SetMdt(Parent->GetMainMdt());
  result.SetHitCount(1);
  result.SetAuxCount(1);
  result.SetScore(0);

  FILE *fp = ffopen(TextFn, "rb");

  for (INT4 i = Start; i <= End; ++i) {
    const GPTYPE gp = List.GetGlobalStart(i);

    const size_t mdtIndex =
        Parent->GetMainMdt()->LookupByGp(gp);

    if (mdtIndex == 0)
      continue;

    result.SetMdtIndex(mdtIndex);

    if (Relation != ZRelNE) {
      IRESULT::hit_type fc =
          FieldCache->FcInField(gp, fp);

      result.SetHitTable(fc);
    }

    pirset->FastAddEntry(result);
  }

  if (fp)
    ffclose(fp);

  pirset->MergeEntries(true);

  if (Relation == ZRelNE)
    pirset->Not(FieldName);

  return pirset;
}
