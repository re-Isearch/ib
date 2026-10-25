#include "common.hxx"
#include "index.hxx"
#include "operator.hxx"
#include "squery.hxx"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <tuple>
#include <vector>

static void require(bool ok,const char *message)
{
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
using Hit = IRESULT::hit_type;
using Hits = std::vector<Hit>;
using Key = std::tuple<GPTYPE,GPTYPE,FCSOURCE>;
using Direction = OPOBJ::DIR_T;
static const Direction directions[] = {OPOBJ::BEFOREorAFTER,OPOBJ::BEFORE,OPOBJ::AFTER};

struct Database : IDBOBJ
{
  DFDT fields;
  STRING stem = "proximity-fixture";
  mutable MDT mdt;
  Database() : mdt(stem,false)
  {
    MDTREC record;
    for (size_t i = 1; i <= 3; ++i)
      {
        record.SetKey(STRING().form("record-%zu",i));
        record.SetGlobalFileStart((i-1)*10000);
        require(record.SetLocalRecordRange(0,1000) && mdt.AddEntry(record) == i,"create proximity MDT fixture");
      }
  }
  DFDT *GetDfdt() override { return &fields; }
  PMDT GetMainMdt() const override { return &mdt; }
  STRING ComposeDbFn(const CHR *ext) const override { return stem+ext; }
};

static void add(atomicIRSET& set,size_t index,const Hits& hits,bool normalize,double score = 2)
{
  HITTABLE table;
  for (const auto& hit : hits) table.AddEntry(hit);
  if (normalize) table.Normalize();
  IRESULT row;
  row.SetIndex(index); row.SetHitTable(table); row.SetHitCount(table.Size()); row.SetScore(score);
  row.SetAuxCount(1); set.FastAddEntry(row);
}
static atomicIRSET operand(Database& db,const Hits& hits,bool normalize = true)
{
  atomicIRSET set(&db); add(set,1,hits,normalize); set.SortBy(ByIndex); return atomicIRSET(set);
}
static std::set<Key> keys(const HITTABLE& table)
{
  std::set<Key> result;
  for (const auto& hit : table) result.emplace(hit.GetFieldStart(),hit.GetFieldEnd(),hit.GetSourceId());
  return result;
}
static size_t oracle(const HITTABLE& left,const HITTABLE& right,float metric,
    Direction direction,std::set<Key>& evidence)
{
  double distance = std::trunc(static_cast<double>(metric));
  if (metric > -1 && metric < 1)
    {
      distance = std::trunc(0.5 + metric*GPTYPE(1000));
      if (distance == 0 || distance == 1) distance = 2;
    }
  size_t pairs = 0;
  for (const auto& a : left) for (const auto& b : right)
    {
      if (a.IsEmpty() || b.IsEmpty()) continue;
      GPTYPE gap;
      if (b.GetFieldEnd() < a.GetFieldStart())
        {
          if (direction == OPOBJ::AFTER) continue;
          gap = a.GetFieldStart()-b.GetFieldEnd()-1;
        }
      else if (a.GetFieldEnd() < b.GetFieldStart())
        {
          if (direction == OPOBJ::BEFORE) continue;
          gap = b.GetFieldStart()-a.GetFieldEnd()-1;
        }
      else continue; // Existing PROX semantics require distinct nonoverlapping hits.
      const bool matches = distance >= 0 ? static_cast<long double>(gap) <= distance :
        static_cast<long double>(gap) >= -distance;
      if (!matches) continue;
      ++pairs;
      evidence.emplace(a.GetFieldStart(),a.GetFieldEnd(),a.GetSourceId());
      evidence.emplace(b.GetFieldStart(),b.GetFieldEnd(),b.GetSourceId());
    }
  return pairs;
}
static void compare(Database& db,const Hits& left,const Hits& right,float metric,
    Direction direction,bool normalize = true)
{
  auto a = operand(db,left,normalize), b = operand(db,right,normalize);
  const auto originalLeft = a[0].GetHitTable(), originalRight = b[0].GetHitTable();
  std::set<Key> expected;
  const size_t pairs = oracle(originalLeft,originalRight,metric,direction,expected);
  a.GetHitTotal(); // The cached input count must be invalidated after the join.
  a.CharProx(b,metric,direction);
  require(a.GetTotalEntries() == (pairs ? 1 : 0),"PROX record membership agrees with pair oracle");
  require(a.GetHitTotal() == pairs,"PROX aggregate count reflects the qualifying pairs");
  if (pairs)
    {
      require(a[0].GetHitCount() == pairs && a[0].GetAuxCount() == 2,"PROX pair count and aux count");
      require(keys(a[0].GetHitTable()) == expected,"PROX retains participating original coordinates and source IDs");
      require(a[0].GetScore() == 2,"PROX preserves the existing left score");
    }
  require(keys(b[0].GetHitTable()) == keys(originalRight),"PROX leaves the right operand unchanged");
}
static void regression(Database& db)
{
  // Sorted hits: the first 'a' is distant, but the next one follows 'is'.
  const Hits is = {Hit(100,101,1)}, a = {Hit(0,0,2),Hit(103,103,2)};
  auto left = operand(db,is), right = operand(db,a);
  left.CharProx(right,10);
  require(left.GetTotalEntries() == 1 && left[0].GetHitCount() == 1,
          "sorted PROX must find a later nearby hit after the first distant occurrence");
  for (float span : {1.0f,10.0f,50.0f,500.0f})
    for (Direction direction : directions)
      {
        compare(db,is,a,span,direction);
        compare(db,a,is,span,direction);
      }
}
static void examples(Database& db)
{
  for (Direction direction : directions)
    for (float metric : {1.0f,7.0f,10.0f,50.0f,-1.0f,-7.0f,-50.0f,0.05f,-0.05f,0.001f,-0.001f})
      {
        compare(db,{Hit(100,101,1),Hit(1000,1001,1)},
          {Hit(0,0,2),Hit(103,103,2),Hit(900,901,2),Hit(1003,1003,2)},metric,direction);
        // End order differs from start order; an earlier broad span must
        // neither hide a later small span nor incorrectly satisfy FAR.
        compare(db,{Hit(100,105,1),Hit(300,302,1)},
          {Hit(0,500,2),Hit(10,11,2),Hit(98,99,2),Hit(110,111,2),Hit(310,311,2)},metric,direction);
        compare(db,{Hit(100,101,1)},
          {Hit(103,103,2),Hit(0,0,2),Hit(98,99,2)},metric,direction,false);
        compare(db,{Hit(10,15,1)},{Hit(10,15,1)},metric,direction);
      }
  const GPTYPE top = std::numeric_limits<GPTYPE>::max();
  for (Direction direction : directions)
    for (float metric : {1.0f,50.0f,-1.0f,-50.0f,std::ldexp(1.0f,65),-std::ldexp(1.0f,65)})
      {
        compare(db,{Hit(0,0,1),Hit(top-10,top-9,1)},
          {Hit(2,2,2),Hit(top-7,top-6,2),Hit(top-1,top,2)},metric,direction);
        if (sizeof(GPTYPE) > 4)
          compare(db,{Hit(GPTYPE(UINT64_C(1)<<40),GPTYPE(UINT64_C(1)<<40)+1,1)},
            {Hit(0,0,2),Hit(GPTYPE(UINT64_C(1)<<40)+3,GPTYPE(UINT64_C(1)<<40)+3,2)},metric,direction);
      }
  for (uint32_t bits : {UINT32_C(0x7f800000),UINT32_C(0xff800000),UINT32_C(0x7fc00000)})
    {
      float metric; std::memcpy(&metric,&bits,sizeof(metric));
      auto a = operand(db,{Hit(1,2,1)}), b = operand(db,{Hit(4,5,2)});
      a.CharProx(b,metric); require(a.IsEmpty(),"nonfinite PROX metrics are rejected before integer conversion");
    }
  auto a = operand(db,{Hit(1,2,1)}), b = operand(db,{Hit(900,901,2)});
  a.CharProx(b,0); require(a.GetTotalEntries() == 1,"zero-metric PROX retains PEER semantics");
}
static void randomized(Database& db)
{
  std::mt19937 random(104729);
  for (size_t trial = 0; trial < 100; ++trial)
    {
      Hits left,right;
      for (size_t i = 0; i < 17; ++i)
        {
          const GPTYPE start = (random()%400)*2;
          left.emplace_back(start,start+random()%80,1);
        }
      for (size_t i = 0; i < 23; ++i)
        {
          const GPTYPE start = (random()%400)*2+1;
          right.emplace_back(start,start+random()%100,2);
        }
      for (Direction direction : directions)
        for (float metric : {1.0f,10.0f,50.0f,-1.0f,-10.0f,-50.0f})
          compare(db,left,right,metric,direction,trial%2 == 0);
    }
}
static void recordOrder(Database& db)
{
  for (float metric : {10.0f,-10.0f,0.05f,-0.05f})
    {
      atomicIRSET a(&db), b(&db);
      add(a,1,{Hit(100,101,1)},true,2); add(a,2,{Hit(200,201,1)},true,4);
      add(b,1,{Hit(103,103,2),Hit(400,401,2)},true,5);
      add(b,2,{Hit(203,203,2),Hit(500,501,2)},true,6);
      add(b,3,{Hit(100,101,2)},true,3);
      a.SortBy(ByIndex); b.SortBy(ByIndex);
      atomicIRSET expected(a), sorted(b); expected.CharProx(sorted,metric);
      b.SortBy([](const void *x,const void *y) {
        const auto a = static_cast<const IRESULT*>(x)->GetIndex();
        const auto b = static_cast<const IRESULT*>(y)->GetIndex();
        return a < b ? 1 : a > b ? -1 : 0;
      });
      const auto originalSort = b.GetSort();
      a.CharProx(b,metric);
      if (a.GetTotalEntries() != expected.GetTotalEntries() || a.GetHitTotal() != expected.GetHitTotal())
        std::cerr << "record-order metric=" << metric << " sort=" << originalSort << " actual="
          << a.GetTotalEntries() << '/' << a.GetHitTotal() << " expected="
          << expected.GetTotalEntries() << '/' << expected.GetHitTotal() << '\n';
      require(a.GetTotalEntries() == expected.GetTotalEntries() && a.GetHitTotal() == expected.GetHitTotal(),
              "unsorted records use the same proximity counts, including fractional bounds");
      for (size_t i = 0; i < a.GetTotalEntries(); ++i)
        require(a[i].GetIndex() == expected[i].GetIndex() && a[i].GetScore() == expected[i].GetScore() &&
                keys(a[i].GetHitTable()) == keys(expected[i].GetHitTable()),"record sorting preserves membership, score and evidence");
      require(b.GetSort() == originalSort,"the fallback does not reorder its const operand");
    }
}
static std::unique_ptr<OPOBJ> topOperator(SQUERY& query)
{
  OPSTACK stack; query.GetOpstack(&stack); OPOBJ *op = NULL; stack >> op;
  return std::unique_ptr<OPOBJ>(op);
}
static void parser()
{
  for (const auto *name : {"PROX","PROXIMITY"})
    for (const auto *argument : {"1","10","50","500","-10","0.05","5%"})
      {
        const STRING token = STRING(name)+":"+argument;
        float expected = std::strtof(argument,NULL);
        if (argument[std::strlen(argument)-1] == '%') expected /= 100.0;
        SQUERY rpn, composed, other;
        rpn.SetRpnTerm(STRING("is a ")+token);
        composed.SetRpnTerm("is"); other.SetRpnTerm("a"); composed.Cat(other,token);
        for (SQUERY *query : {&rpn,&composed})
          {
            auto op = topOperator(*query);
            if (!op || op->GetOpType() != TypeOperator || op->GetOperatorType() != OperatorProximity ||
                op->GetOperatorMetric() != expected)
              std::cerr << "parser token=" << token << " composed=" << (query == &composed)
                << " metric=" << (op ? op->GetOperatorMetric() : 0) << " expected=" << expected
                << " rpn=" << query->GetRpnTerm() << '\n';
            require(op && op->GetOpType() == TypeOperator && op->GetOperatorType() == OperatorProximity &&
                    op->GetOperatorMetric() == expected,"short and long PROX spellings retain the correct metric");
          }
        FILE *fp = fopen("prox-query.bin","w+b"); require(fp != NULL,"create serialized PROX query");
        rpn.Write(fp); rewind(fp); SQUERY loaded;
        require(loaded.Read(fp),"read serialized PROX query"); fclose(fp); std::remove("prox-query.bin");
        auto op = topOperator(loaded);
        require(op && op->GetOperatorType() == OperatorProximity && op->GetOperatorMetric() == expected,
                "proximity bounds survive serialization");
      }
  SQUERY a,b; a.SetRpnTerm("is"); b.SetRpnTerm("a"); a.Cat(b,"AND:CLAIM-TEXT");
  auto field = topOperator(a);
  require(field && field->GetOperatorType() == OperatorAndWithin && field->GetOperatorString() == "CLAIM-TEXT",
          "named composition retains parsed field arguments as well as numeric metrics");
}
static void dispatch(Database& db)
{
  INDEX index(&db,db.stem+".inx");
  for (const auto *token : {"PROX:10","PROX:50","PROX:500","PROXIMITY:10"})
    {
      SQUERY parsed; parsed.SetRpnTerm(STRING("is a ")+token);
      OPSTACK parsedStack; parsed.GetOpstack(&parsedStack); OPOBJ *op = NULL; parsedStack >> op;
      std::unique_ptr<OPOBJ> owned(op);
      require(op && op->GetOpType() == TypeOperator && op->GetOperatorType() == OperatorProximity &&
              op->GetOperatorMetric() == std::strtof(std::strchr(token,':')+1,NULL),
              "PROX parser produces a proximity operator");
      auto a = operand(db,{Hit(100,101,1)}), b = operand(db,{Hit(0,0,2),Hit(103,103,2)});
      OPSTACK stack; stack << a << b << *op;
      SQUERY query; query.SetOpstack(stack);
      std::unique_ptr<IRSET> result(index.Search(QUERY(query,Unsorted,NoNormalization)));
      const size_t expectedPairs = op->GetOperatorMetric() >= 99 ? 2 : 1;
      if (!result || result->GetTotalEntries() != 1 || (*result)[0].GetHitCount() != expectedPairs)
        std::cerr << "dispatch " << token << " parsed=" << parsed.GetRpnTerm() << " metric=" << op->GetOperatorMetric()
          << " records=" << (result ? result->GetTotalEntries() : 0)
          << " pairs=" << (result && result->GetTotalEntries() ? (*result)[0].GetHitCount() : 0) << '\n';
      require(result && result->GetTotalEntries() == 1 && (*result)[0].GetHitCount() == expectedPairs,
              "INDEX dispatch for is a PROX:n finds the later adjacent pair");
    }
}
static void dense(Database& db)
{
  const size_t n = 30000;
  Hits left,right;
  for (size_t i = 0; i < n; ++i) { left.emplace_back(2*i,2*i,1); right.emplace_back(2*i+1,2*i+1,2); }
  const auto start = std::chrono::steady_clock::now();
  for (Direction direction : directions)
    {
      auto a = operand(db,left), b = operand(db,right);
      a.CharProx(b,2*n,direction);
      const size_t pairs = direction == OPOBJ::BEFOREorAFTER ? n*n :
        direction == OPOBJ::BEFORE ? n*(n-1)/2 : n*(n+1)/2;
      require(a.GetTotalEntries() == 1 && a[0].GetHitCount() == pairs,"dense PROX counts ranges without enumerating pairs");
      require(a[0].GetHitTable().Size() <= 2*n,"dense PROX retains linear evidence storage");
    }
  auto a = operand(db,left), b = operand(db,right); a.CharProx(b,-1);
  require(a.GetTotalEntries() == 1 && a[0].GetHitCount() == n*n-(2*n-1),"dense FAR excludes only zero-gap pairs");
  std::cout << "Dense PROX: " << n*n << " possible pairs, linear evidence, "
    << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count() << " ms for four joins\n";
}
int main(int argc,char **argv)
{
  Database db; regression(db);
  if (argc == 2 && std::string(argv[1]) == "--regression-only") return 0;
  examples(db); randomized(db); recordOrder(db); parser(); dispatch(db); dense(db);
  std::cout << "PROX sorted-hit regression, pair oracle, bounds, ordering, provenance and dense joins passed\n";
}
