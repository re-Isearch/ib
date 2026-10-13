#include "fieldtype_promotion.hxx"
#include "dfdt.hxx"
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
  require(STRING("42").IsCurrency() && STRING("12.50").IsCurrency(),
          "plain numbers also pass the monetary parser");

  for (bool frozen : {false, true})
    {
      check_promotion(integer, "42", frozen, integer, 1, 0, 0, 0);
      check_promotion(integer, "  -42  ", frozen, integer, 1, 0, 0, 0);
      check_promotion(integer, "170141183460469231731687303715884105727",
                      frozen, integer, 1, 0, 0, 0);
      check_promotion(integer, "12.50", frozen, frozen ? integer : number, 1, 1, 0, 1);
      check_promotion(integer, "1e3", frozen, frozen ? integer : number, 1, 1, 0, 1);
      check_promotion(integer, "$12.50", frozen, frozen ? integer : money, 1, 1, 1, 1);
      check_promotion(integer, "no amount here", frozen, integer, 1, 1, 1, 0);
      check_promotion(number, "42", frozen, number, 0, 1, 0, 0);
      check_promotion(number, "12.50", frozen, number, 0, 1, 0, 0);
      check_promotion(number, "1e3", frozen, number, 0, 1, 0, 0);
      check_promotion(number, "$12.50", frozen, frozen ? number : money, 0, 1, 1, 1);
      check_promotion(number, "no amount here", frozen, number, 0, 1, 1, 0);
      for (INT type = FIELDTYPE::any; type <= FIELDTYPE::special; ++type)
        if (type != integer && type != number)
          check_promotion(type, "$12.50", frozen, type, 0, 0, 0, 0);
    }
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
  promotion_regressions();
  state_regressions();
  type_metadata_regressions();
  std::cout << "Field type promotion, predicate counts and persistence passed\n";
}
