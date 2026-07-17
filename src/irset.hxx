/*
Copyright (c) 2020-26 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
/************************************************************************
************************************************************************/

/*@@@
File:		irset.hxx
Version:	2.00
Description:	Class IRSET - Internal Search Result Set
@@@*/

#ifndef IRSET_HXX
#define IRSET_HXX

#include "defs.hxx"
#include "idbobj.hxx"
#include "string.hxx"
#include "iresult.hxx"
#include "rset.hxx"
#include "operand.hxx"

#include <memory>
#include <utility>


// NOTE:
//
// What was CosineMetricNormalization shall in the future be called EuclideanNormalization
//
// Cosine shall become traditional Cosine
// pCosine shall be added
///
////
extern enum NormalizationMethods {
  Unnormalized = 0, NoNormalization, NormalizationL2, NormalizationL1, MaxNormalization, LogNormalization, BytesNormalization,
  preCosineMetricNormalization, CosineMetricNormalization, EuclideanNormalization,
  AuxNormalization1, AuxNormalization2, AuxNormalization3, NormalizationAF,  HybridNormalization,  UndefinedNormalization
} defaultNormalization;
const int CosineNormalization=NormalizationL2;


class atomicIRSET : public OPERAND {
public:
  atomicIRSET(const PIDBOBJ DbParent = NULL, size_t Reserve = 0);
  atomicIRSET(const OPOBJ& OtherIrset);

  atomicIRSET& operator = (const atomicIRSET& OtherIrset);
  OPOBJ& operator =(const OPOBJ& OtherIrset) override;

  OPOBJ& operator +=(const OPOBJ& OtherIrset);
  atomicIRSET& Concat (const atomicIRSET& OtherIrset); 

  OPOBJ& Cat(const OPOBJ& OtherIrset, bool AddHitCounts = false);
  OPOBJ& Cat (const OPOBJ& OtherIrset, size_t Total, bool AddHitCounts = false);

  t_Operand GetOperandType () const override { return TypeRset; };

  //operator const RSET *() const;
  void   GC(); // Garbage collect

  OPOBJ* Duplicate () const override;
  atomicIRSET* Duplicate();

  void MergeEntries(const bool AddHitCounts = true);
  void Adjust_Scores();

  bool GetMdtrec(size_t i, MDTREC *Mdtrec) const;
 
  void SetMdt(const IDBOBJ *Idb);
  void SetMdt(const MDT *NewMdt);
  const MDT *GetMdt(size_t i) const;

  void SetVirtualIndex(const UCHR NewvIndex);
  UCHR GetVirtualIndex(size_t i) const;

  void LoadTable (const STRING& FileName);
  void SaveTable (const STRING& FileName) const;

  void AddEntry (const IRESULT& ResultRecord, const bool AddHitCounts);
  void FastAddEntry(const IRESULT& ResultRecord);

  bool GetEntry (const size_t Index, PIRESULT ResultRecord) const override;
  IRESULT     GetEntry (const size_t Index) const;

  const IRESULT operator[](size_t n) const { return Table[n]; }
  IRESULT& operator[](size_t n)            { return Table[n]; }

  INDEX_ID    GetIndex(size_t i) {
    if (i>0 && i<= TotalEntries)
      return Table[i-1].GetIndex();
    return 0;
  }
  bool SetSortIndex(size_t i, const SORT_INDEX_ID& sort) {
    if (i>0 && i<= TotalEntries)
      Table[i-1].SetSortIndex(sort);
    else
      return false;
    return true;
  }

  PRSET GetRset () const;
  PRSET GetRset (size_t Total) const;
  // Getting RSet in two passes...
  PRSET GetRset(size_t Start, size_t End) const; // Start count with 0

  // 
  // Fill(0) returns empty RSET
  // Fill() or Fill(1) return the entire IRSET as RSET
  PRSET Fill(size_t Start=1, size_t End = 0, PRSET set = NULL) const; // Fill start count with 1

  bool IsEmpty() const override { return TotalEntries == 0; }

  void Expand ();
  void Reserve (const size_t Entries) { if (Entries > MaxEntries) Resize(Entries); };
  void CleanUp ();
  void Resize (const size_t Entries);
  size_t GetTotalEntries () const override { return TotalEntries; }
  size_t SetTotalEntries(size_t NewTotal);
  size_t GetHitTotal ();

  // Score normalization (scoring functions)
  OPOBJ *ComputeScores (const float TermWeight, enum NormalizationMethods Method = defaultNormalization);
  // Methods
  OPOBJ *ComputeScoresNoNormalization (const float TermWeight);
  OPOBJ *ComputeScoresNormalizationAF (const float TermWeight);
  OPOBJ *ComputeScoresNormalizationL2 (const float TermWeight);
  OPOBJ *ComputeScoresNormalizationL1 (const float TermWeight);
  OPOBJ *ComputeScoresMaxNormalization (const float TermWeight);
  OPOBJ *ComputeScoresLogNormalization (const float TermWeight);
  OPOBJ *ComputeScoresBytesNormalization (const float TermWeight);
  OPOBJ *ComputeScoresCosineMetricNormalization (const float TermWeight);

  OPOBJ *ComputeScoresHybridNormalization(const float TermWeight);
  void SetPrecomputed(enum NormalizationMethods Method);

  // Stubs
  OPOBJ *ComputeScoresAux1Normalization (const float TermWeight);
  OPOBJ *ComputeScoresAux2Normalization (const float TermWeight);
  OPOBJ *ComputeScoresAux3Normalization (const float TermWeight);

  // Binary Functions
  virtual OPOBJ *Or (const OPOBJ& OtherIrset) override;
  virtual OPOBJ *Nor (const OPOBJ& OtherIrset) override;
  virtual OPOBJ *And (const OPOBJ& OtherIrset) override;
  virtual OPOBJ *And (const OPOBJ& OtherIrset, size_t Limit);

  virtual OPOBJ *Nand (const OPOBJ& OtherIrset) override;
  virtual OPOBJ *AndNot (const OPOBJ& OtherIrset) override;
  virtual OPOBJ *Xor (const OPOBJ& OtherIrset) override;

  virtual OPOBJ *Join (const OPOBJ& OtherIrset) override;
  virtual OPOBJ *Join (OPOBJ *OtherIrset) override;

  virtual OPOBJ *Near (const OPOBJ& OtherIrset) override;
  virtual OPOBJ *Far (const OPOBJ& OtherIrset) override;
  virtual OPOBJ *After (const OPOBJ& OtherIrset) override;
  virtual OPOBJ *Before (const OPOBJ& OtherIrset) override;
  virtual OPOBJ *Adj (const OPOBJ& OtherIrset) override;
  virtual OPOBJ *Follows (const OPOBJ& OtherIrset) override;
  virtual OPOBJ *Precedes (const OPOBJ& OtherIrset) override;
  virtual OPOBJ *Neighbor (const OPOBJ& OtherIrset) override;
  virtual OPOBJ *XPeer (const OPOBJ& OtherIrset) override;
  virtual OPOBJ *Peer (const OPOBJ& OtherIrset) override;
  virtual OPOBJ *BeforePeer (const OPOBJ& Irset) override;
  virtual OPOBJ *AfterPeer (const OPOBJ& Irset) override;


  virtual OPOBJ *Within(const OPOBJ& OtherIrset, const STRING& Fieldname) override;
  virtual OPOBJ *BeforeWithin(const OPOBJ& OtherIrset, const STRING& Fieldname) override;
  virtual OPOBJ *AfterWithin(const OPOBJ& OtherIrset, const STRING& Fieldname) override;

  // Unary Functions
  virtual OPOBJ *Within(const char *field);
  virtual OPOBJ *Within(const DATERANGE& Daterange);
  virtual OPOBJ *Within(const STRING& FieldName) override;
  virtual OPOBJ *Inside(const STRING& FieldName) override;
  virtual OPOBJ *Sibling ( ) override;
  virtual OPOBJ *Inclusive(const STRING& FieldName) override;
  virtual OPOBJ *XWithin(const STRING& FieldName) override;
  virtual OPOBJ *Not ( ) override;

  // Special Unary Functions
  virtual OPOBJ *WithinFile(const STRING& FileSpec) override;
  virtual OPOBJ *WithinDoctype(const STRING& DoctypeNameSpec) override;
  virtual OPOBJ *WithKey(const STRING& KeySpec) override;

  virtual OPOBJ *Not (const STRING& FieldName) override;

  // Unary
  virtual OPOBJ *Reduce(const float Metric=0.0) override;
  virtual OPOBJ *Focus(const float Metric=0.0) override;
  virtual OPOBJ *Trim(const float Metric=0.0) override;
  virtual OPOBJ *HitCount(const float Metric=0.0) override;

  // Score = Score * Weight
  OPOBJ *BoostScore (const float Weight = 1) override;

 // Unary Sort
  virtual OPOBJ *SortBy (const STRING& ByWhat) override;

  // Distance metric operator (Distance >= 0 near, Distance < 0 far)
  OPOBJ *CharProx (const OPOBJ& OtherIrset, const float Metric,
	DIR_T dir = OPOBJ::BEFOREorAFTER) override;
  // Convienience
  OPOBJ *WithinXChars (const OPOBJ& OtherIrset, float Metric = 50) {
     return CharProx (OtherIrset, Metric, BEFOREorAFTER);
  }
  OPOBJ *WithinXChars_Before (const OPOBJ& OtherIrset, float Metric = 50) {
     return CharProx (OtherIrset, Metric, BEFORE);
  }
  OPOBJ *WithinXChars_After (const OPOBJ& OtherIrset, float Metric = 50) {
       return CharProx (OtherIrset, Metric, AFTER);
  }
  OPOBJ *WithinXPercent (const OPOBJ& OtherIrset, float Metric) {
     if (Metric >= 100.0)
	return And(OtherIrset);
     return CharProx (OtherIrset, Metric/100, BEFOREorAFTER);
  }
  OPOBJ *WithinXPercent_Before (const OPOBJ& OtherIrset, float Metric) {
     if (Metric >= 100.0)
	return Before(OtherIrset);
     return CharProx (OtherIrset, Metric/100, BEFORE);
  }
  OPOBJ *WithinXPercent_After (const OPOBJ& OtherIrset, float Metric) {
     if (Metric >= 100.0)
	return After(OtherIrset);
     return CharProx (OtherIrset, Metric/100, AFTER);
  }

  void  setPrivateSortUserData(void *ptr) { userData = ptr; }
  void *getPrivateSortUserData() const    { return userData; }

  void SortBy(int (*func)(const void *, const void *)) {
    installSortFunction (func);
    SortByFunction(func);
  }
  void installSortFunction(int (*func)(const void *, const void *)) {
    if (IrsetCompar != func)
      {
	if (Sort == ByFunction && func) Sort = Unsorted;
	IrsetCompar = func;
      }
  }

  void SortBy(enum SortBy SortBy) {
    if      (SortBy == ByScore)       SortByScore();
    else if (SortBy == ByHits)        SortByHits();
    else if (SortBy == ByReverseHits) SortByReverseHits();
    else if (SortBy == ByAuxCount)    SortByAuxCount();
    else if (SortBy == ByIndex)       SortByIndex();
    else if (SortBy == ByDate)        SortByDate();
    else if (SortBy == ByKey)         SortByKey();
    else if (SortBy == ByReverseDate) SortByReverseDate();
    else if (SortBy == ByNewsrank)    SortByNewsrank();
    else if (SortBy == ByFunction)    SortByFunction(IrsetCompar);
    else if (SortBy >= ByPrivate && SortBy < ByExtIndex)
	SortByPrivate((int)SortBy-(int)ByPrivate);
    else if (SortBy >= ByExtIndex && SortBy <= ByExtIndexLast)
	SortByExtIndex(SortBy);
    SortRequest = SortBy;
  }
  INT GetSort() const override { return (INT)Sort; } 
  void setSortedByScore() { Sort = ByScore; }

  void SetParent (PIDBOBJ const NewParent) override { Parent = NewParent; }
  PIDBOBJ GetParent () const override               { return Parent;      }

  DOUBLE GetMaxScore() const override { return MaxScore; }
  DOUBLE GetMinScore() const override { return MinScore; }

  void   SetMaxEntriesAdvice (size_t nMax) { MaxEntriesAdvice=nMax; }
  size_t GetMaxEntriesAdvice () const      { return MaxEntriesAdvice; }

  void Write (PFILE fp) const;
  bool Read (PFILE fp);

  void Dump(INT Skip =0, ostream& os = cout) const {
   Fill (1, TotalEntries, NULL)->Dump(Skip, os);
  };

  void     Clear();

  ~atomicIRSET();
private:
  typedef  bool (*peer_t) (const FC&, const FC&);
  OPOBJ   *Peer (const OPOBJ& OtherIrset, peer_t Func);
  OPOBJ   *Within(const OPOBJ& OtherIrset, const STRING& Fieldname,  peer_t Func);
  bool FieldExists(const STRING& FieldName);
//  void     Clear();
  PIRESULT StealTable();
  size_t   FindByMdtIndex(size_t Index) const;
  void     SortByScore ();
  void     SortByHits ();
  void     SortByReverseHits ();
  void     SortByAuxCount ();
  void     SortByIndex ();
  void     SortByDate ();
  void     SortByReverseDate ();
  void     SortByKey ();
  void     SortByNewsrank ();
  void     SortByFunction (int (*func)(const void *, const void *));
  void     SortByPrivate (int n);
  void     SortByExtIndex(enum SortBy SortByWhich);
  OPOBJ   *_Or (const OPOBJ& OtherIrset);
  OPOBJ   *_And (const OPOBJ& OtherIrset, size_t Limit=0);
  OPOBJ   *_Xor (const OPOBJ& OtherIrset);
  OPOBJ   *_AndNot (const OPOBJ& OtherIrset);
  OPOBJ   *_CharProx (const OPOBJ& OtherIrset, const float Metric, DIR_T dir = BEFOREorAFTER);
  void     Set(const atomicIRSET *OtherPtr);

  IRESULT *Table;
  size_t   TotalEntries;
  size_t   MaxEntries;
  size_t   MaxEntriesAdvice;
  size_t   HitTotal;
  size_t   Increment;
  long     allocs;
  enum NormalizationMethods ComputedS;

  DOUBLE   MaxScore, MinScore;
  enum SortBy Sort, SortRequest;
  PIDBOBJ  Parent;
  int     (*IrsetCompar)(const void *, const void *);
  void    *userData; // For private Sort
};

extern "C" int (* __Private_IRSET_Sort) (void *Table, int Total, void *IDBParent, int which, void *UserData);


typedef atomicIRSET* PatomicIRSET;

// Common functions

inline void Write(const atomicIRSET Irset, PFILE Fp)
{
  Irset.Write(Fp);
}


inline bool Read(PatomicIRSET IrsetPtr, PFILE Fp)
{
  return IrsetPtr->Read(Fp);
}

/////////////////////////////////////////////////////////////////
// Freestore management for CACHED atomicIRSETs 
/////////////////////////////////////////////////////////////////

class atomicIRSETptr {
  public:
    atomicIRSETptr(const PIDBOBJ DbParent, size_t Reserve) {
      count_ = 1;
      ptr_ = new atomicIRSET(DbParent, Reserve);
    }
    atomicIRSETptr(const atomicIRSET& Irset) {
      count_ = 1;
      ptr_ = new atomicIRSET(Irset);
    }
   ~atomicIRSETptr() { delete ptr_; }
  private:
    friend class   _IRSET;
    atomicIRSET         *ptr_;
    signed   int   count_;      // reference count
};

/////////////////////////////////////////////////////////////////
/// _IRSET: copy-on-write wrapper around atomicIRSET
/////////////////////////////////////////////////////////////////
///
///
/// Important:
///   - _IRSET copies share the contained atomicIRSET.
///   - Mutating operations detach first.
///   - The inherited OPERAND state is never copied because historical
///     OPERAND/ATTRLIST copying is not ownership-safe.
///
/////////////////////////////////////////////////////////////////

class _IRSET : public OPERAND
{
private:
  using shared_type = std::shared_ptr<atomicIRSET>;

  /*
   * Create a completely initialized atomicIRSET and then copy the source
   * through atomicIRSET's assignment operator.
   *
   * This deliberately avoids the implicit atomicIRSET copy constructor.
   */
  static shared_type DeepCopy(const atomicIRSET& Source)
  {
    shared_type Copy = std::make_shared<atomicIRSET>(
        Source.GetParent(),
        Source.GetTotalEntries());

    *Copy = Source;
    return Copy;
  }

  /*
   * Construct from an OPOBJ only when it is actually an atomicIRSET.
   */
  static shared_type FromOperand(const OPOBJ& Source)
  {
    const atomicIRSET* Irset =
        dynamic_cast<const atomicIRSET*>(&Source);

    if (Irset == NULL) {
      message_log(
          LOG_ERROR,
          "_IRSET: cannot construct from non-atomicIRSET operand");

      return std::make_shared<atomicIRSET>();
    }

    return DeepCopy(*Irset);
  }

  /*
   * Const access never detaches.
   */
  const atomicIRSET* cnode() const noexcept
  {
    return p_.get();
  }

  /*
   * Mutable access performs copy-on-write detachment.
   */
  atomicIRSET* node()
  {
    if (p_.use_count() != 1)
      p_ = DeepCopy(*p_);

    return p_.get();
  }

  /*
   * Used when the following operation replaces the complete logical
   * contents, such as Read() or LoadTable().
   *
   * When shared, this avoids copying data that is immediately replaced.
   */
  atomicIRSET* replacement_node()
  {
    if (p_.use_count() != 1) {
      const PIDBOBJ Parent = p_->GetParent();
      p_ = std::make_shared<atomicIRSET>(Parent);
    }

    return p_.get();
  }

public:
  explicit _IRSET(
      const PIDBOBJ DbParent = NULL,
      const size_t Reserve = 0)
    : OPERAND(),
      p_(std::make_shared<atomicIRSET>(DbParent, Reserve))
  {
  }

  explicit _IRSET(const atomicIRSET& OtherIrset)
    : OPERAND(),
      p_(DeepCopy(OtherIrset))
  {
  }

  explicit _IRSET(const OPOBJ& OtherOperand)
    : OPERAND(),
      p_(FromOperand(OtherOperand))
  {
  }

  /*
   * Do not default these operations.
   *
   * Defaulting them would also copy OPERAND, whose ATTRLIST state has
   * historical shallow-copy ownership behavior.
   */
  _IRSET(const _IRSET& Other) noexcept
    : OPERAND(),
      p_(Other.p_)
  {
  }

  _IRSET(_IRSET&& Other) noexcept
    : OPERAND(),
      p_(std::move(Other.p_))
  {
  }

  _IRSET& operator=(const _IRSET& Other) noexcept
  {
    if (this != &Other)
      p_ = Other.p_;

    return *this;
  }

  _IRSET& operator=(_IRSET&& Other) noexcept
  {
    if (this != &Other)
      p_ = std::move(Other.p_);

    return *this;
  }

  ~_IRSET() override = default;

  /////////////////////////////////////////////////////////////////
  // Underlying-object access
  /////////////////////////////////////////////////////////////////

  atomicIRSET* operator->()
  {
    return node();
  }

  const atomicIRSET* operator->() const noexcept
  {
    return cnode();
  }

  atomicIRSET& operator*()
  {
    return *node();
  }

  const atomicIRSET& operator*() const noexcept
  {
    return *cnode();
  }

  operator const OPOBJ*() const noexcept
  {
    return cnode();
  }

  /////////////////////////////////////////////////////////////////
  // Copying and concatenation
  /////////////////////////////////////////////////////////////////

  OPOBJ* Duplicate() const override
  {
    return new _IRSET(*this);
  }

  _IRSET* Duplicate()
  {
    return new _IRSET(*this);
  }

  _IRSET& Concat(const _IRSET& OtherIrset)
  {
    node()->Concat(*OtherIrset.cnode());
    return *this;
  }

  /////////////////////////////////////////////////////////////////
  // Basic information
  /////////////////////////////////////////////////////////////////

  INT GetSort() const override
  {
    return cnode()->GetSort();
  }

  DOUBLE GetMaxScore() const override
  {
    return cnode()->GetMaxScore();
  }

  DOUBLE GetMinScore() const override
  {
    return cnode()->GetMinScore();
  }

  size_t GetHitTotal() const
  {
    /*
     * Logically const, even if atomicIRSET's declaration is not const.
     */
    return p_->GetHitTotal();
  }

  size_t GetTotalEntries() const override
  {
    return cnode()->GetTotalEntries();
  }

  size_t SetTotalEntries(const size_t NewTotal) 
  {
    return node()->SetTotalEntries(NewTotal);
  }

  t_Operand GetOperandType() const override
  {
    return cnode()->GetOperandType();
  }

  bool IsEmpty() const override
  {
    return cnode()->IsEmpty();
  }

  /////////////////////////////////////////////////////////////////
  // RSET conversion
  /////////////////////////////////////////////////////////////////

  PRSET GetRset() const
  {
    return cnode()->GetRset();
  }

  PRSET GetRset(const size_t Total) const
  {
    return cnode()->GetRset(Total);
  }

  PRSET GetRset(
      const size_t Start,
      const size_t End) const
  {
    return cnode()->GetRset(Start, End);
  }

  PRSET Fill(
      const size_t Start = 1,
      const size_t End = 0,
      PRSET Set = NULL) const
  {
    return cnode()->Fill(Start, End, Set);
  }

  /////////////////////////////////////////////////////////////////
  // Storage and table operations
  /////////////////////////////////////////////////////////////////

  void LoadTable(const STRING& FileName)
  {
    replacement_node()->LoadTable(FileName);
  }

  void SaveTable(const STRING& FileName) const
  {
    cnode()->SaveTable(FileName);
  }

  void Resize(const size_t Entries)
  {
    node()->Resize(Entries);
  }

  void Reserve(const size_t Entries)
  {
    node()->Reserve(Entries);
  }

  void Expand()
  {
    node()->Expand();
  }

  void CleanUp()
  {
    node()->CleanUp();
  }

  void Clear()
  {
    node()->Clear();
  }

  void GC()
  {
    node()->GC();
  }

  /////////////////////////////////////////////////////////////////
  // Entry access
  /////////////////////////////////////////////////////////////////

  void AddEntry( const IRESULT& ResultRecord, const bool AddHitCounts) 
  {
    node()->AddEntry(ResultRecord, AddHitCounts);
  }

  void FastAddEntry(const IRESULT& ResultRecord) 
  {
    node()->FastAddEntry(ResultRecord);
  }

  bool GetEntry( const size_t Index, PIRESULT ResultRecord) const override
  {
    return cnode()->GetEntry(Index, ResultRecord);
  }

  IRESULT GetEntry(const size_t Index) const
  {
    return cnode()->GetEntry(Index);
  }

  IRESULT operator[](const size_t Index) const 
  {
    return (*cnode())[Index];
  }

  IRESULT& operator[](const size_t Index)
  {
    return (*node())[Index];
  }

  INDEX_ID GetIndex(const size_t Index) const 
  {
    return p_->GetIndex(Index);
  }

  bool SetSortIndex( const size_t Index, const SORT_INDEX_ID& SortIndex) 
  {
    return node()->SetSortIndex(Index, SortIndex);
  }

  /////////////////////////////////////////////////////////////////
  // Scoring
  /////////////////////////////////////////////////////////////////

  OPOBJ* ComputeScores(
      const float TermWeight,
      const enum NormalizationMethods Method =
          defaultNormalization)
  {
    return node()->ComputeScores(TermWeight, Method);
  }

  OPOBJ* ComputeScoresNoNormalization(
      const float TermWeight)
  {
    return node()->ComputeScoresNoNormalization(TermWeight);
  }

  OPOBJ* ComputeScoresNormalizationAF(
      const float TermWeight)
  {
    return node()->ComputeScoresNormalizationAF(TermWeight);
  }

  OPOBJ* ComputeScoresNormalizationL2(
      const float TermWeight)
  {
    return node()->ComputeScoresNormalizationL2(TermWeight);
  }

  OPOBJ* ComputeScoresNormalizationL1(
      const float TermWeight)
  {
    return node()->ComputeScoresNormalizationL1(TermWeight);
  }

  OPOBJ* ComputeScoresMaxNormalization(
      const float TermWeight)
  {
    return node()->ComputeScoresMaxNormalization(TermWeight);
  }

  OPOBJ* ComputeScoresLogNormalization(
      const float TermWeight)
  {
    return node()->ComputeScoresLogNormalization(TermWeight);
  }

  OPOBJ* ComputeScoresBytesNormalization(
      const float TermWeight)
  {
    return node()->ComputeScoresBytesNormalization(TermWeight);
  }

  OPOBJ* ComputeScoresCosineMetricNormalization(
      const float TermWeight)
  {
    return node()->ComputeScoresCosineMetricNormalization(
        TermWeight);
  }

  OPOBJ* ComputeScoresHybridNormalization(
      const float TermWeight)
  {
    return node()->ComputeScoresHybridNormalization(TermWeight);
  }

  OPOBJ* ComputeScoresAux1Normalization(
      const float TermWeight)
  {
    return node()->ComputeScoresAux1Normalization(TermWeight);
  }

  OPOBJ* ComputeScoresAux2Normalization(
      const float TermWeight)
  {
    return node()->ComputeScoresAux2Normalization(TermWeight);
  }

  OPOBJ* ComputeScoresAux3Normalization(
      const float TermWeight)
  {
    return node()->ComputeScoresAux3Normalization(TermWeight);
  }

  void SetPrecomputed(
      const enum NormalizationMethods Method)
  {
    node()->SetPrecomputed(Method);
  }

  void MergeEntries(const bool AddHitCounts = true)
  {
    node()->MergeEntries(AddHitCounts);
  }

  void Adjust_Scores()
  {
    node()->Adjust_Scores();
  }

  /////////////////////////////////////////////////////////////////
  // MDT and virtual-index information
  /////////////////////////////////////////////////////////////////

  bool GetMdtrec(
      const size_t Index,
      MDTREC* Record) const
  {
    return cnode()->GetMdtrec(Index, Record);
  }

  void SetMdt(const IDBOBJ* Idb)
  {
    node()->SetMdt(Idb);
  }

  void SetMdt(const MDT* NewMdt)
  {
    node()->SetMdt(NewMdt);
  }

  const MDT* GetMdt(const size_t Index) const
  {
    return cnode()->GetMdt(Index);
  }

  void SetVirtualIndex(const UCHR NewVirtualIndex)
  {
    node()->SetVirtualIndex(NewVirtualIndex);
  }

  UCHR GetVirtualIndex(const size_t Index) const
  {
    return cnode()->GetVirtualIndex(Index);
  }

  /////////////////////////////////////////////////////////////////
  // Binary Boolean operations
  /////////////////////////////////////////////////////////////////

  OPOBJ* Or(const _IRSET& Other)
  {
    return node()->Or(*Other.cnode());
  }

  OPOBJ* Nor(const _IRSET& Other)
  {
    return node()->Nor(*Other.cnode());
  }

  OPOBJ* And(const _IRSET& Other)
  {
    return node()->And(*Other.cnode());
  }

  OPOBJ* And(
      const _IRSET& Other,
      const size_t Limit)
  {
    return node()->And(*Other.cnode(), Limit);
  }

  OPOBJ* Nand(const _IRSET& Other)
  {
    return node()->Nand(*Other.cnode());
  }

  OPOBJ* AndNot(const _IRSET& Other)
  {
    return node()->AndNot(*Other.cnode());
  }

  OPOBJ* Xor(const _IRSET& Other)
  {
    return node()->Xor(*Other.cnode());
  }

  OPOBJ* Join(const _IRSET& Other)
  {
    return node()->Join(*Other.cnode());
  }

  /////////////////////////////////////////////////////////////////
  // Binary proximity operations
  /////////////////////////////////////////////////////////////////

  OPOBJ* Near(const _IRSET& Other)
  {
    return node()->Near(*Other.cnode());
  }

  OPOBJ* Far(const _IRSET& Other)
  {
    return node()->Far(*Other.cnode());
  }

  OPOBJ* After(const _IRSET& Other)
  {
    return node()->After(*Other.cnode());
  }

  OPOBJ* Before(const _IRSET& Other)
  {
    return node()->Before(*Other.cnode());
  }

  OPOBJ* Adj(const _IRSET& Other)
  {
    return node()->Adj(*Other.cnode());
  }

  OPOBJ* Follows(const _IRSET& Other)
  {
    return node()->Follows(*Other.cnode());
  }

  OPOBJ* Precedes(const _IRSET& Other)
  {
    return node()->Precedes(*Other.cnode());
  }

  OPOBJ* Neighbor(const _IRSET& Other)
  {
    return node()->Neighbor(*Other.cnode());
  }

  OPOBJ* XPeer(const _IRSET& Other)
  {
    return node()->XPeer(*Other.cnode());
  }

  OPOBJ* Peer(const _IRSET& Other)
  {
    return node()->Peer(*Other.cnode());
  }

  OPOBJ* BeforePeer(const _IRSET& Other)
  {
    return node()->BeforePeer(*Other.cnode());
  }

  OPOBJ* AfterPeer(const _IRSET& Other)
  {
    return node()->AfterPeer(*Other.cnode());
  }

  OPOBJ* Within(
      const _IRSET& Other,
      const STRING& FieldName)
  {
    return node()->Within(*Other.cnode(), FieldName);
  }

  OPOBJ* BeforeWithin(
      const _IRSET& Other,
      const STRING& FieldName)
  {
    return node()->BeforeWithin(
        *Other.cnode(),
        FieldName);
  }

  OPOBJ* AfterWithin(
      const _IRSET& Other,
      const STRING& FieldName)
  {
    return node()->AfterWithin(
        *Other.cnode(),
        FieldName);
  }

  OPOBJ* CharProx(
      const _IRSET& Other,
      const float Metric,
      const DIR_T Direction = OPOBJ::BEFOREorAFTER)
  {
    return node()->CharProx(
        *Other.cnode(),
        Metric,
        Direction);
  }

  OPOBJ* WithinXChars(
      const _IRSET& Other,
      const float Metric = 50)
  {
    return CharProx(
        Other,
        Metric,
        OPOBJ::BEFOREorAFTER);
  }

  OPOBJ* WithinXChars_Before(
      const _IRSET& Other,
      const float Metric = 50)
  {
    return CharProx(
        Other,
        Metric,
        OPOBJ::BEFORE);
  }

  OPOBJ* WithinXChars_After(
      const _IRSET& Other,
      const float Metric = 50)
  {
    return CharProx(
        Other,
        Metric,
        OPOBJ::AFTER);
  }

  OPOBJ* WithinXPercent(
      const _IRSET& Other,
      const float Metric)
  {
    if (Metric >= 100.0)
      return And(Other);

    return CharProx(
        Other,
        Metric / 100.0F,
        OPOBJ::BEFOREorAFTER);
  }

  OPOBJ* WithinXPercent_Before(
      const _IRSET& Other,
      const float Metric)
  {
    if (Metric >= 100.0)
      return Before(Other);

    return CharProx(
        Other,
        Metric / 100.0F,
        OPOBJ::BEFORE);
  }

  OPOBJ* WithinXPercent_After(
      const _IRSET& Other,
      const float Metric)
  {
    if (Metric >= 100.0)
      return After(Other);

    return CharProx(
        Other,
        Metric / 100.0F,
        OPOBJ::AFTER);
  }

  /////////////////////////////////////////////////////////////////
  // Unary operations
  /////////////////////////////////////////////////////////////////

  OPOBJ* Not() override
  {
    return node()->Not();
  }

  OPOBJ* Not(const STRING& FieldName) override
  {
    return node()->Not(FieldName);
  }

  OPOBJ* Focus(const float Metric = 0.0F) override
  {
    return node()->Focus(Metric);
  }


  OPOBJ* Reduce(const float Metric = 0.0F) override
  {
    return node()->Reduce(Metric);
  }

  OPOBJ* Trim(const float Metric = 0.0F) override
  {
    return node()->Trim(Metric);
  }

  OPOBJ* HitCount(const float Metric = 0.0F) override
  {
    return node()->HitCount(Metric);
  }

  OPOBJ* BoostScore(const float Weight = 1.0F) override
  {
    return node()->BoostScore(Weight);
  }

  OPOBJ* Within(const char* FieldName) 
  {
    return node()->Within(FieldName);
  }

  OPOBJ* Within(const DATERANGE& DateRange) 
  {
    return node()->Within(DateRange);
  }

  OPOBJ* Within(const STRING& FieldName) override
  {
    return node()->Within(FieldName);
  }

  OPOBJ* Inside(const STRING& FieldName) override
  {
    return node()->Inside(FieldName);
  }

  OPOBJ* Sibling() override
  {
    return node()->Sibling();
  }

  OPOBJ* Inclusive(const STRING& FieldName) override
  {
    return node()->Inclusive(FieldName);
  }

  OPOBJ* XWithin(const STRING& FieldName) override
  {
    return node()->XWithin(FieldName);
  }

  OPOBJ* WithinFile(const STRING& FileSpecification) override
  {
    return node()->WithinFile(FileSpecification);
  }

  OPOBJ* WithinDoctype(const STRING& DoctypeSpecification) override
  {
    return node()->WithinDoctype(DoctypeSpecification);
  }

  OPOBJ* WithKey(const STRING& KeySpecification) override
  {
    return node()->WithKey(KeySpecification);
  }

  /////////////////////////////////////////////////////////////////
  // Sorting
  /////////////////////////////////////////////////////////////////

  OPOBJ* SortBy(const STRING& ByWhat) override
  {
    return node()->SortBy(ByWhat);
  }

  void SortBy(const enum SortBy SortOrder)
  {
    node()->SortBy(SortOrder);
  }

  void SortBy(int (*Function)(const void*, const void*))
  {
    node()->SortBy(Function);
  }

  void installSortFunction(
      int (*Function)(const void*, const void*))
  {
    node()->installSortFunction(Function);
  }

  void setSortedByScore()
  {
    node()->setSortedByScore();
  }

  void setPrivateSortUserData(void* Data)
  {
    node()->setPrivateSortUserData(Data);
  }

  void* getPrivateSortUserData() const
  {
    return cnode()->getPrivateSortUserData();
  }

  /////////////////////////////////////////////////////////////////
  // Parent and limits
  /////////////////////////////////////////////////////////////////

  void SetParent(PIDBOBJ const NewParent) override
  {
    node()->SetParent(NewParent);
  }

  PIDBOBJ GetParent() const override
  {
    return cnode()->GetParent();
  }

  void SetMaxEntriesAdvice(const size_t Maximum)
  {
    node()->SetMaxEntriesAdvice(Maximum);
  }

  size_t GetMaxEntriesAdvice() const
  {
    return cnode()->GetMaxEntriesAdvice();
  }

  /////////////////////////////////////////////////////////////////
  // Serialization and diagnostics
  /////////////////////////////////////////////////////////////////

  void Write(PFILE File) const
  {
    cnode()->Write(File);
  }

  bool Read(PFILE File)
  {
    return replacement_node()->Read(File);
  }

  void Dump(
      const INT Skip = 0,
      ostream& Stream = cout) const
  {
    cnode()->Dump(Skip, Stream);
  }

  bool IsShared() const noexcept
  {
    return p_.use_count() > 1;
  }

  bool IsUnique() const noexcept
  {
    return p_.use_count() == 1;
  }

  long UseCount() const noexcept
  {
    return p_.use_count();
  }

private:
  shared_type p_;
};

typedef _IRSET* P_IRSET;

#include "irset_type.hxx"

#ifdef IB_USE_COW_IRSET
# define IRSET _IRSET
#else
# define IRSET atomicIRSET
#endif


#endif
