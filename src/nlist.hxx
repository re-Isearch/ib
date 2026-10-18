#ifndef NUMERICLIST_HXX
#define NUMERICLIST_HXX

#include "nfield.hxx"
#include "integerlist.hxx"

#ifndef DATELIST_HXX
enum SearchState { NO_MATCH=-1, TOO_LOW, TOO_HIGH, MATCH };
enum IntType { AT_START, INSIDE, AT_END };
enum NumBlock { NONE, VAL_BLOCK, GP_BLOCK };
typedef NumBlock blktype;
#endif

struct NUMERIC_INDEX_TRAITS : public INTEGER_INDEX_TRAITS {
  using field_type = NUMERICFLD;
  struct mapped_type {
    const BYTE *value_block = nullptr;
    const BYTE *gp_block = nullptr;
    size_t count = 0;
  };
  static constexpr const char *Name() { return "NUMERICAL"; }
  static value_type Value(const field_type& field) { return field.GetNumericValue().GetTicks(); }
  static GPTYPE GlobalStart(const field_type& field) { return field.GetGlobalStart(); }
  static value_type MappedValueAt(const BYTE *block, size_t i) { return field_type::DecodeValue(block+i*field_type::DISK_SIZE); }
  static GPTYPE MappedGlobalStartAt(const BYTE *block, size_t i) { return field_type::DecodeGlobalStart(block+i*field_type::DISK_SIZE); }
  static span_type MappedSpanAt(const BYTE *, size_t) { return 0; }
  static FILE *OpenForAppend(const STRING&);
  static bool LoadRawBlock(const STRING&, std::vector<field_type>*);
  static bool LoadValueBlock(const STRING&, std::vector<field_type>*);
  static bool MapIndexed(const STRING&, MultiMMapSession&, mapped_type*);
  static bool WriteIndex(const STRING&, const std::vector<field_type>&, const std::vector<field_type>&);
};

class NUMERICLIST : public MAPPEDORDEREDLIST<NUMERIC_INDEX_TRAITS> {
public:
  using base_type = MAPPEDORDEREDLIST<NUMERIC_INDEX_TRAITS>;
  NUMERICLIST() = default;
  explicit NUMERICLIST(INT coords) : Ncoords(coords) {}
  void Empty() { Clear(); }
  void SetFileName(const STRING& name) { FileName = name; }
  void SetCoords(INT value) { Ncoords = value; }
  INT GetCoords() const { return Ncoords; }
  void SetAttribute(INT value) { Attribute = value; }
  INT GetAttribute() const { return Attribute; }
  void SetRelation(ZRelation_t value) { Relation = value; }
  INT GetRelation() const { return Relation; }
  NUMERICOBJ GetNumericValue(size_t i) const { return (*this)[i].GetNumericValue(); }
  bool Resize(size_t count) { Reserve(count); return true; }
  bool Expand(size_t count) { return Resize(count); }
  bool Expand() { return Resize(GetCount()+50); }
  void Cleanup() {}
  size_t LoadTable(NumBlock block) { return LoadTable(0, -1, block); }
  size_t LoadTable(INT4 start, INT4 end) { return LoadTable(start, end, NONE); }
  size_t LoadTable(INT4 start, INT4 end, NumBlock block);
  SearchState Find(const STRING& name, const NUMERICOBJ& key, ZRelation_t relation, INT4 *index);
  SearchState Find(const NUMERICOBJ& key, ZRelation_t relation, INT4 *index);
  SearchState Find(const STRING& name, GPTYPE key, ZRelation_t relation, INT4 *index);
  SearchState Find(GPTYPE key, ZRelation_t relation, INT4 *index);
  SearchState FindIndexes(const STRING& name, const NUMERICOBJ& key, ZRelation_t relation, INT4 *first, INT4 *last);
  template <class T, typename std::enable_if<std::is_floating_point<T>::value, int>::type = 0>
  SearchState Find(const STRING& name, T value, ZRelation_t relation, INT4 *index) {
    return Find(name, NUMERICOBJ(value), relation, index);
  }
  template <class T, typename std::enable_if<std::is_floating_point<T>::value, int>::type = 0>
  SearchState Find(T value, ZRelation_t relation, INT4 *index) {
    return Find(NUMERICOBJ(value), relation, index);
  }
  void ResetHitPosition() { Pointer = StartIndex; }
  GPTYPE GetNextHitPosition() { return Pointer < EndIndex ? GetGlobalStart(Pointer++) : GPTYPE(-1); }
  void Dump(ostream& os = cout) const;
  void Dump(INT4 first, INT4 last, ostream& os = cout) const;
private:
  STRING FileName;
  INT Attribute = 0, Ncoords = 1;
  ZRelation_t Relation = ZRelEQ;
  size_t Pointer = 0, StartIndex = 0, EndIndex = 0;
};
typedef NUMERICLIST* PNUMERICLIST;
#endif
