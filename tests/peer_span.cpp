#include "common.hxx"
#include "index.hxx"
#include "squery.hxx"
#include "operator.hxx"
#include "peer_span.hxx"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <tuple>
#include <vector>

static void require(bool ok, const char *message)
{
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
using Hit = IRESULT::hit_type;
using Hits = std::vector<Hit>;
using Key = std::tuple<GPTYPE, GPTYPE, FCSOURCE>;

struct Database : IDBOBJ {
  DFDT fields;
  std::vector<FC> zones;
  size_t calls = 0;
  bool debug = false;
  explicit Database(std::vector<FC> z = {FC(0,199),FC(200,399)}) : zones(z) {
    if (!zones.empty()) { DFD field; field.SetFieldName("P"); fields.AddEntry(field); }
  }
  DFDT* GetDfdt() override { return &fields; }
  bool GetDebugMode() const override { return debug; }
  FC container(const FC& hit) const {
    FC best = hit; bool found = false;
    for (const auto& zone : zones)
      if (zone.Contains(hit) && (!found || best.Contains(zone))) { best=zone; found=true; }
    return best;
  }
  FC GetPeerFc(const FC& hit, STRING *name = NULL) override {
    ++calls; if (name) *name = "P"; return container(hit);
  }
};

static atomicIRSET operand(Database& db, const Hits& hits, double score=1) {
  atomicIRSET result(&db);
  IRESULT row; row.SetIndex(1); row.SetScore(score); row.SetAuxCount(1);
  HITTABLE table; for (const auto& hit : hits) table.AddEntry(hit);
  row.SetHitTable(table); row.SetHitCount(hits.size()); result.FastAddEntry(row);
  return atomicIRSET(result);
}
static std::set<Key> keys(const HITTABLE& hits) {
  std::set<Key> result;
  for (const auto& hit : hits)
    result.emplace(hit.GetFieldStart(),hit.GetFieldEnd(),hit.GetSourceId());
  return result;
}
static size_t oracle(Database& db, const Hits& left, const Hits& right, GPTYPE span,
                     int direction, std::set<Key>* evidence) {
  size_t count=0;
  for (const auto& a : left) for (const auto& b : right) {
    if (a.IsEmpty() || b.IsEmpty()) continue;
    if (direction == 1 && !(a >= b)) continue;
    if (direction == -1 && !(a <= b)) continue;
    if (!db.zones.empty() && !db.container(a).Contains(b)) continue;
    GPTYPE gap=0;
    if (a.GetFieldEnd() < b.GetFieldStart()) gap=b.GetFieldStart()-a.GetFieldEnd()-1;
    else if (b.GetFieldEnd() < a.GetFieldStart()) gap=a.GetFieldStart()-b.GetFieldEnd()-1;
    if (gap > span) continue;
    ++count;
    evidence->emplace(a.GetFieldStart(),a.GetFieldEnd(),a.GetSourceId());
    evidence->emplace(b.GetFieldStart(),b.GetFieldEnd(),b.GetSourceId());
  }
  return count;
}
static void compare(Database& db, const Hits& left, const Hits& right, GPTYPE span, int direction) {
  std::set<Key> expected;
  size_t count=oracle(db,left,right,span,direction,&expected);
  auto a=operand(db,left,2), b=operand(db,right,3);
  if (direction==1) a.BeforePeer(b,span);
  else if (direction==-1) a.AfterPeer(b,span);
  else a.Peer(b,span);
  require(a.GetTotalEntries()==(count?1:0),"bounded peer record membership agrees with pair oracle");
  if (count) {
    require(a[0].GetHitCount()==count,"bounded peer counts only qualifying pairs");
    require(a[0].GetScore()==5 && a[0].GetAuxCount()==2,"bounded peer preserves score combination and aux count");
    require(keys(a[0].GetHitTable())==expected,"bounded peer retains precisely the participating source hits");
  }
}
static std::unique_ptr<OPOBJ> parsed(const std::string& token) {
  SQUERY q; require(q.SetRpnTerm(STRING(("alpha beta "+token).c_str()))>0,"parse RPN fixture");
  OPSTACK stack; q.GetOpstack(&stack); OPOBJ* op=NULL; stack >> op;
  return std::unique_ptr<OPOBJ>(op);
}
static void parser() {
  for (const auto* name : {"PEER","PEERb","PEERa"}) {
    const auto type=std::string(name)=="PEER"?OperatorPeer:std::string(name)=="PEERb"?OperatorBeforePeer:OperatorAfterPeer;
    for (const auto& argument : {std::string("0"),std::string("250"),std::string("16777217"),
         std::to_string(std::numeric_limits<GPTYPE>::max())}) {
      const std::string token=std::string(name)+":"+argument;
      auto op=parsed(token); GPTYPE span;
      require(op->GetOpType()==TypeOperator && op->GetOperatorType()==type &&
              HasPeerByteSpan(*op) && GetPeerByteSpan(*op,&span),"bounded operator type and exact argument");
      require(std::to_string(span)==argument,"distance retains integers beyond FLOAT precision");
      SQUERY q; q.SetRpnTerm(STRING(("alpha beta "+token).c_str()));
      require(std::string(q.GetRpnTerm().c_str()).find(token)!=std::string::npos,"RPN prints exact byte bound, including zero");
      SQUERY infix; require(infix.SetInfixTerm(STRING(("alpha "+token+" beta").c_str()))>0,"parse bounded infix operator");
      OPSTACK stack; infix.GetOpstack(&stack); OPOBJ* item=NULL; stack >> item;
      std::unique_ptr<OPOBJ> holder(item);
      require(item && item->GetOperatorType()==type && GetPeerByteSpan(*item,&span),"infix preserves bounded operator and argument");
    }
    auto plain=parsed(name); require(!HasPeerByteSpan(*plain),"plain peer has no distance restriction");
    for (const auto* arg : {"-1","1.5","nan","inf","3junk","","18446744073709551616"}) {
      auto invalid=parsed(std::string(name)+":"+arg);
      require(invalid->GetOpType()!=TypeOperator || invalid->GetOperatorType()!=type,"malformed or overflowing span is not accepted as bounded PEER");
    }
  }
  SQUERY nested; nested.SetRpnTerm("a b c PEER:7 PEER:7");
  const STRING before=nested.GetRpnTerm(); int error=0; nested.Rewrite(&error);
  require(error==0 && nested.GetRpnTerm()==before,"bounded PEER must not be flattened as associative");
  SQUERY sibling; sibling.SetRpnTerm("a b PEER:0 SIBLING"); sibling.Rewrite(&error);
  require(error==0 && std::string(sibling.GetRpnTerm().c_str()).find("PEER:0")!=std::string::npos,
          "SIBLING rewrite preserves an explicit zero bound");
  SQUERY chain; chain.SetInfixTerm("a PEER:7 b PEERa:9 c");
  SQUERY rpn; rpn.SetRpnTerm("a b PEER:7 c PEERa:9");
  require(chain.GetRpnTerm()==rpn.GetRpnTerm(),"infix bounded chains bind left to right");
  SQUERY grouped; grouped.SetInfixTerm("a PEER:7 (b PEERa:9 c)");
  rpn.SetRpnTerm("a b c PEERa:9 PEER:7");
  require(grouped.GetRpnTerm()==rpn.GetRpnTerm(),"infix bounded parentheses preserve grouping");
  auto lower=parsed("peerb:250");
  require(lower->GetOperatorType()==OperatorBeforePeer,"bounded names accept case-insensitive infix spelling");
  SQUERY saved; saved.SetRpnTerm("a b PEER:0 c PEERb:16777217");
  FILE* file=fopen("peer-query.bin","w+b"); require(file!=NULL,"create query serialization fixture");
  saved.Write(file); rewind(file); SQUERY loaded;
  require(loaded.Read(file) && loaded.GetRpnTerm()==saved.GetRpnTerm(),"serialized queries preserve exact bounds");
  fclose(file); std::remove("peer-query.bin");
  SQUERY composed, extra; composed.SetRpnTerm("a b PEER:7"); extra.SetRpnTerm("c");
  composed.Cat(extra,OperatorPeer);
  OPSTACK plainStack; composed.GetOpstack(&plainStack); OPOBJ* glue=NULL; plainStack >> glue;
  require(glue && !HasPeerByteSpan(*glue),"plain composition must not inherit a child's byte bound"); delete glue;
  composed.Cat(extra,STRING("PEERb:16777217"));
  OPSTACK boundStack; composed.GetOpstack(&boundStack); boundStack >> glue; GPTYPE composedSpan=0;
  require(glue && glue->GetOperatorType()==OperatorBeforePeer && GetPeerByteSpan(*glue,&composedSpan) &&
          composedSpan==16777217,"composition by operator name preserves its exact byte bound"); delete glue;
  for (FLOAT value : {FLOAT(-1),FLOAT(1.5),std::numeric_limits<FLOAT>::infinity(),
                      std::numeric_limits<FLOAT>::quiet_NaN()}) {
    OPERATOR op(OperatorPeer); op.SetOperatorMetric(value); GPTYPE span;
    require(HasPeerByteSpan(op),"invalid nonzero metrics must not silently select unbounded PEER");
    require(!GetPeerByteSpan(op,&span),"invalid programmatic metrics are rejected before integer conversion");
  }
  for (FLOAT value : {0.0f,-0.0f}) {
    OPERATOR op(OperatorPeer); op.SetOperatorMetric(value);
    require(!HasPeerByteSpan(op),"both signed metric zeros retain unbounded legacy meaning");
  }
  if (sizeof(GPTYPE)>4) {
    SQUERY q; q.SetRpnTerm("a b AND"); OPERATOR op(OperatorPeer); op.SetOperatorMetric(1e10f);
    require(q.SetOperator(op),"set programmatic peer metric");
    SQUERY copy; copy.SetRpnTerm(q.GetRpnTerm()); OPSTACK stack; copy.GetOpstack(&stack);
    OPOBJ* last=NULL; stack >> last; GPTYPE span=0;
    require(last && GetPeerByteSpan(*last,&span) && span==UINT64_C(10000000000),
            "large whole programmatic metrics print as decimal integers"); delete last;
  }
}
static void examples() {
  Database db;
  const Hits a={Hit(10,12,1)}, b={Hit(20,22,2)};
  compare(db,a,b,6,0); require(db.calls==0,"reject distant candidates before any field lookup");
  compare(db,a,b,7,0); compare(db,a,b,8,0);
  compare(db,a,{Hit(13,15,2)},0,0); compare(db,a,{Hit(12,14,2)},0,0);
  compare(db,a,{Hit(14,16,2)},0,0);
  for (int dir : {-1,0,1}) {
    compare(db,a,b,7,dir); compare(db,b,a,7,dir);
    // The near occurrence is across a boundary; the same-field occurrence is too far.
    compare(db,{Hit(198,199,1)},{Hit(200,201,2),Hit(100,101,2)},5,dir);
    // Nonmonotone ends exercise broad/nested interval operands.
    compare(db,{Hit(40,44,1),Hit(120,124,1)},
      {Hit(5,150,2),Hit(20,22,2),Hit(80,81,2),Hit(125,127,2)},9,dir);
  }
  Database flat(std::vector<FC>{}); compare(flat,a,b,7,0); compare(flat,a,b,7,-1);
  if (sizeof(GPTYPE)>4) {
    GPTYPE base=GPTYPE(UINT64_C(1)<<40);
    Database wide({FC(base,base+999)});
    compare(wide,{Hit(base+10,base+12,1)},{Hit(base+20,base+22,2)},7,0);
  }
  const GPTYPE top=std::numeric_limits<GPTYPE>::max();
  Database edge({FC(top-100,top-1)});
  compare(edge,{Hit(top-80,top-78,1)},{Hit(top-20,top-18,2)},top,0);
  for (int dir : {-1,0,1}) {
    auto a=operand(db,{Hit(20,22,1)}), b=operand(db,{Hit(10,12,2)});
    _IRSET wrapped(a), other(b), original(wrapped);
    OPOBJ& polymorphic=wrapped;
    if (dir==1) polymorphic.BeforePeer(other,GPTYPE(7));
    else if (dir==-1) polymorphic.AfterPeer(other,GPTYPE(7));
    else polymorphic.Peer(other,GPTYPE(7));
    require(wrapped.GetTotalEntries()==(dir==-1?0:1),"bounded wrapper accepts wrapped operands through OPOBJ");
    require(original.GetTotalEntries()==1 && original[0].GetHitTable().Size()==1,
            "bounded wrapper detaches before modifying shared hits");
  }
}
static void randomized() {
  std::mt19937 random(74021);
  Database db({FC(0,399),FC(0,199),FC(50,149),FC(200,399)});
  for (size_t trial=0;trial<80;++trial) {
    Hits a,b;
    for (size_t i=0;i<13;++i) { GPTYPE s=(random()%180)*2; a.emplace_back(s,s+random()%60,1); }
    for (size_t i=0;i<17;++i) { GPTYPE s=(random()%180)*2+1; b.emplace_back(s,s+random()%70,2); }
    for (GPTYPE span : {GPTYPE(0),GPTYPE(1),GPTYPE(9),GPTYPE(100)})
      for (int dir : {-1,0,1}) compare(db,a,b,span,dir);
  }
}
struct SearchDatabase : Database {
  STRING stem="peer-dispatch";
  mutable MDT mdt;
  SearchDatabase() : Database({FC(0,100000000)}), mdt(stem,false) {
    MDTREC row; row.SetKey("fixture");
    require(row.SetLocalRecordRange(0,100000000) && mdt.AddEntry(row)==1,"seed dispatch MDT");
  }
  PMDT GetMainMdt() const override { return &mdt; }
  STRING ComposeDbFn(const CHR* ext) const override { return stem+ext; }
};
static void dispatch() {
  SearchDatabase db; INDEX index(&db,db.stem+".inx");
  const auto run = [&](const Hits& left, const Hits& right, const char* token, bool expected) {
    auto a=operand(db,left), b=operand(db,right); auto op=parsed(token);
    OPSTACK stack; stack << a << b << *op;
    SQUERY q; q.SetOpstack(stack);
    std::unique_ptr<IRSET> result(index.Search(QUERY(q,Unsorted,NoNormalization)));
    require(result && result->GetTotalEntries()==(expected?1:0),"INDEX dispatch applies bound and RPN operand order");
  };
  const Hits a={Hit(10,12,1)}, b={Hit(20,22,2)};
  run(a,b,"PEER:6",false); run(a,b,"PEER:7",true);
  run(a,b,"PEERb:7",true); run(a,b,"PEERa:7",false);
  run(b,a,"PEERb:7",false); run(b,a,"PEERa:7",true);
  run(a,b,"PEER",true); run(a,b,"PEERb",true); run(a,b,"PEERa",false);
  run(a,{Hit(13,15,2)},"PEER:0",true); run(a,b,"PEER:0",false);
  run(a,{Hit(16777230,16777232,2)},"PEER:16777217",true);
  run(a,{Hit(16777230,16777232,2)},"PEER:16777216",false);
}
static void dense() {
  const size_t n=30000;
  Hits left,right; left.reserve(n); right.reserve(n);
  for (size_t i=0;i<n;++i) { left.emplace_back(2*i,2*i,1); right.emplace_back(2*i+1,2*i+1,2); }
  Database db({FC(0,2*n+10)});
  auto a=operand(db,left), b=operand(db,right);
  const auto start=std::chrono::steady_clock::now();
  a.Peer(b,GPTYPE(2*n));
  const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  require(a.GetTotalEntries()==1 && a[0].GetHitCount()==n*n,"dense range join counts 900 million pairs without enumerating them");
  require(a[0].GetHitTable().Size()==2*n,"dense range join emits each contributing hit once");
  std::cout << "Dense bounded PEER: " << n*n << " pairs, " << db.calls << " container lookups, " << ms << " ms\n";
}
static std::vector<std::string> profileMessages;
static int captureProfile(int, const char* message) {
  if (std::string(message).find("Bounded PEER profile:")!=std::string::npos)
    profileMessages.emplace_back(message);
  return 0;
}
static void diagnostics() {
  require(_globalMessageLogger.Init(LOG_ALL,"peer-profile",captureProfile),"enable profile log capture");
  Database db; const Hits left={Hit(10,12,1),Hit(90,92,1)}, right={Hit(20,22,2)};
  const auto run = [&](bool debug, GPTYPE span) {
    db.debug=debug; auto a=operand(db,left), b=operand(db,right); a.Peer(b,span);
    require(a.GetTotalEntries()==(span>=7?1:0),"profiling preserves bounded query membership");
    if (span>=7) require(a[0].GetHitCount()==1 && a[0].GetHitTable().Size()==2,
                        "profiling preserves pair count and evidence");
  };
  run(false,7); require(profileMessages.empty(),"normal queries do not emit profile diagnostics");
  run(true,7); require(profileMessages.size()==1,"debug query emits one bounded-join profile");
  require(profileMessages.back().find("PEER:7 records=1 left_hits=2 right_hits=1 positional_hits=1 "
      "container_lookups=1 matched_records=1 pairs=1")!=std::string::npos,
      "profile identifies candidate reduction, actual field lookups, and qualifying pairs");
  require(profileMessages.back().find("elapsed_ms=")!=std::string::npos &&
      profileMessages.back().find("container_ms=")!=std::string::npos &&
      profileMessages.back().find("other_ms=")!=std::string::npos,"profile separates container time from other join work");
  run(true,6); require(profileMessages.size()==2 &&
      profileMessages.back().find("positional_hits=0 container_lookups=0 matched_records=0 pairs=0")!=std::string::npos,
      "profile distinguishes a distance rejection without field access");
  Database flat(std::vector<FC>{}); flat.debug=true;
  auto a=operand(flat,left), b=operand(flat,right); a.Peer(b,GPTYPE(7));
  require(profileMessages.size()==3 && profileMessages.back().find("positional_hits=1 container_lookups=0")!=std::string::npos,
      "fieldless proximity does not report synthetic containers as field lookups");
  atomicIRSET empty(&db); auto other=operand(db,right); empty.Peer(other,GPTYPE(7));
  require(profileMessages.size()==4 && profileMessages.back().find("records=0")!=std::string::npos,
      "empty joins still emit a complete debug profile");
  _globalMessageLogger.Init(static_cast<_MessageFunc_t>(NULL));
  _globalMessageLogger.Init(LOG_ALL & ~LOG_DEBUG,"peer-span",stderr);
}
int main() {
  parser(); examples(); randomized(); dispatch(); dense(); diagnostics();
  std::cout << "Bounded PEER parsing, ordering, distance, structure, provenance, and dense joins passed\n";
}
