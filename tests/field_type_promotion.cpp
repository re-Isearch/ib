#include "fieldtype_promotion.hxx"
#include "dfdt.hxx"
#include "numbers.hxx"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char *message)
{
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

struct CountingValue {
  STRING value;
  mutable unsigned integral = 0, number = 0, currency = 0;

  explicit CountingValue(const char *text) : value(text) {}
  bool IsIntegralNumber() const { ++integral; return value.IsIntegralNumber(); }
  bool IsNumber() const { ++number; return value.IsNumber(); }
  bool IsCurrency() const { ++currency; return value.IsCurrency(); }
};

static void check_promotion(INT current, const char *text, bool frozen,
                            INT expected, unsigned integral, unsigned number,
                            unsigned currency, unsigned state)
{
  CountingValue value(text);
  unsigned lookups = 0;
  const FIELDTYPE result = field_type_inference::GuessNumericPromotion(
    FIELDTYPE(current), value, [&] { ++lookups; return frozen; });
  require(result == expected, "numeric promotion result");
  require(value.integral == integral && value.number == number &&
          value.currency == currency, "minimal content predicate count");
  require(lookups == state, "freeze lookup only for a promotion candidate");
}

static void promotion_regressions()
{
  const INT integer = FIELDTYPE::integer, number = FIELDTYPE::numerical;
  const INT money = FIELDTYPE::currency;
  require(MONETARYOBJ(STRING("42")).Ok() && MONETARYOBJ(STRING("12.50")).Ok(),
          "explicitly typed monetary fields still accept plain amounts");

  for (bool frozen : {false, true})
    {
      check_promotion(integer, "42", frozen, integer, 1, 0, 0, 0);
      check_promotion(integer, "  -42  ", frozen, integer, 1, 0, 0, 0);
      check_promotion(integer, "170141183460469231731687303715884105727",
                      frozen, integer, 1, 0, 0, 0);
      check_promotion(integer, "12.50", frozen, frozen ? integer : number, 1, 1, 0, 1);
      check_promotion(integer, "0.00012294500029437535", frozen, frozen ? integer : number, 1, 1, 0, 1);
      check_promotion(integer, "1e3", frozen, frozen ? integer : number, 1, 1, 0, 1);
      check_promotion(integer, "$12.50", frozen, frozen ? integer : money, 1, 1, 1, 1);
      check_promotion(integer, "no amount here", frozen, integer, 1, 1, 1, 0);
      check_promotion(number, "42", frozen, number, 0, 1, 0, 0);
      check_promotion(number, "12.50", frozen, number, 0, 1, 0, 0);
      check_promotion(number, "0.00012294500029437535", frozen, number, 0, 1, 0, 0);
      check_promotion(number, "1e3", frozen, number, 0, 1, 0, 0);
      check_promotion(number, "$12.50", frozen, frozen ? number : money, 0, 1, 1, 1);
      check_promotion(number, "no amount here", frozen, number, 0, 1, 1, 0);
      for (INT type = FIELDTYPE::any; type <= FIELDTYPE::special; ++type)
        if (type != integer && type != number)
          check_promotion(type, "$12.50", frozen, type, 0, 0, 0, 0);
    }
}

static void currency_detection_regressions()
{
  for (const char *text : {"42", "12.50", "-73.9857", "40.7484", "0.00012294500029437535",
                           "1e-12", "  -12.50  ", "1,234.56", "1.234,56", "1'234.56",
                           "", "$", "$12x", "NaN", "USD12.50", "12.50USD"})
    require(!STRING(text).IsCurrency(), "currency detection needs a marker and a valid amount");
  for (const char *text : {"$12.50", "  $ -12.50  ", "\xc2\xa3" "12.50", "\xc2\xa5" "12.50",
                           "\xc2\xa4" "12.50", "\xe2\x82\xac" "1.234,56", "\xe2\x82\xb9" "1,234.50",
                           "\xe2\x82\xbf" "0.01", "25\xc2\xa2", " -25 \xc2\xa2 ",
                           "$12.50\xc2\xa0", "\xc2\xa0" "$12.50\xc2\xa0", "25\xc2\xa2\xc2\xa0",
                           "\xa3" "12.50", "\xa4" "12.50", "\xa5" "12.50", "25\xa2"})
    require(STRING(text).IsCurrency(), text);
  require(MONETARYOBJ(STRING("25\xc2\xa2")).GetTicks() == 12500 &&
          MONETARYOBJ(STRING("$12.50\xc2\xa0")).GetTicks() == 625000,
          "UTF-8 suffixes retain the whole amount");
  const STRING score("0.00012294500029437535");
  require(score.IsNumber() && !score.IsIntegralNumber() && !score.IsCurrency(),
          "Cirrus popularity score is numerical, not an integer or currency");
}

static void add_field(DFDT& fields, const char *name, INT type)
{
  DFD field;
  field.SetFieldName(name);
  field.SetFieldType(FIELDTYPE(type));
  fields.AddEntry(field);
}

static void flush_fields(DFDT& fields, const STRING& file)
{
  fields.Flush(file);
  require(!fields.GetChanged(), "field metadata flushed");
}

static void state_regressions()
{
  const STRING file("field-type-promotion-regression.dfd");
  file.Unlink();
  DFDT fields;
  add_field(fields, "PRICE", FIELDTYPE::integer);
  add_field(fields, "PRICE_TEXT", FIELDTYPE::text);
  add_field(fields, "UNSPECIFIED", FIELDTYPE::any);
  flush_fields(fields, file);

  DFDT loaded(file);
  require(loaded.GetFieldTypeState("price") == TypeStateUnknown,
          "unset type state stays unfrozen");
  require(!loaded.FreezeFieldType("missing") && !loaded.GetChanged(),
          "missing field does not dirty the table");
  require(loaded.FreezeFieldType("price") && loaded.GetChanged(),
          "first freeze marks the table changed");
  require(loaded.GetFieldTypeState("PRICE") == TypeStateFrozen &&
          loaded.GetFieldTypeState("PRICE_TEXT") == TypeStateUnknown,
          "logical field projections keep independent states");
  flush_fields(loaded, file);

  DFDT restored(file);
  require(restored.GetFieldTypeState("price") == TypeStateFrozen,
          "frozen state survives a flush and reload");
  require(restored.FreezeFieldType("PRICE") && !restored.GetChanged(),
          "repeated freeze does not dirty the table");
  require(restored.FreezeFieldType("PRICE_TEXT") && !restored.GetChanged() &&
          restored.GetFieldTypeState("PRICE_TEXT") == TypeStateUnknown,
          "text fields stay unfrozen");
  require(restored.FreezeFieldType("UNSPECIFIED") && !restored.GetChanged() &&
          restored.GetFieldTypeState("UNSPECIFIED") == TypeStateUnknown,
          "any fields stay unfrozen");
  file.Unlink();
}

static void type_metadata_regressions()
{
  const STRING file("field-type-promotion-regression.dfd");
  for (unsigned count : {1U, 6U})
    {
      DFDT fields;
      const char *names[] = {"ALPHA", "BRAVO", "CHARLIE", "DELTA", "ECHO", "FOXTROT"};
      for (unsigned i = 0; i < count; ++i)
        add_field(fields, names[i], FIELDTYPE::integer);
      flush_fields(fields, file);
      for (unsigned i = 0; i < count; ++i)
        for (INT type : {INT(FIELDTYPE::numerical), INT(FIELDTYPE::currency)})
          {
            add_field(fields, names[i], type);
            require(fields.GetChanged(), "promotion marks existing metadata changed");
            flush_fields(fields, file);
            DFDT restored(file);
            DFD field;
            require(restored.GetDfdRecord(names[i], &field) && field.GetFieldType() == type,
                    "promoted datatype survives a flush and reload");
            if (type == FIELDTYPE::currency)
              require(field.GetAttributesPtr()->AttrGetFieldType() == type &&
                      !field.GetAttributesPtr()->AttrGetFieldNumerical(),
                      "currency promotion removes the numerical structure flag");
            add_field(fields, names[i], type);
            require(!fields.GetChanged(), "unchanged datatype does not dirty the table");
          }
    }
  file.Unlink();

  ATTRLIST attributes;
  attributes.AttrSetFieldObj(FIELDOBJ("PRICE", FIELDTYPE::numerical));
  attributes.AttrSetFieldObj(FIELDOBJ("PRICE", FIELDTYPE::currency));
  require(attributes.AttrGetFieldType() == FIELDTYPE::currency,
          "field-object assignment removes the previous type flag");
}

int main()
{
  currency_detection_regressions();
  promotion_regressions();
  state_regressions();
  type_metadata_regressions();
  std::cout << "Field type promotion, predicate counts and persistence passed\n";
}
