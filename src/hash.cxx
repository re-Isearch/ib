#include "common.hxx"
#include "hash.hxx"


HASH::HASH()
{
  Setup(997);
}


HASH::HASH(size_t Size)
{
  Setup(Size);
}


void HASH::Setup(size_t Size)
{
  if (Size == 0)
    Size = 997;

  Numeric.reserve(Size);
  Strings.reserve(Size);
}


STRING HASH::GetValue(const STRING& name) const
{
  STRING value;

  if (GetValue(name, &value))
    return value;

  return NulString;
}


bool HASH::GetValue(const STRING& name, STRING *value) const
{
  if (value == NULL)
    return false;

  const auto it = Strings.find(name.toStdString());

  if (it == Strings.end())
    {
      value->Clear();
      return false;
    }

  *value = it->second;
  return true;
}


void HASH::AddEntry(const STRING& name, const STRING& value)
{
  //
  // Preserve old HASH semantics: first definition wins.
  //
  Strings.emplace(name.toStdString(), value);
}


void HASH::AddEntry(const STRING& definition)
{
  STRING name(definition);
  STRING value(definition);

  const STRINGINDEX pos = definition.Search('=');

  if (pos)
    {
      name.EraseAfter(pos - 1);
      value.EraseBefore(pos + 1);
      AddEntry(name, value);
    }
}


INT HASH::Insert(const Item_type& item)
{
  auto result = Numeric.emplace(item.Key, item);

  if (!result.second)
    return 1; // duplicate key -- historical return value

  result.first->second.State = 1;

  return 0;
}


Item_type *HASH::Find(Key_type key) const
{
  const auto it = Numeric.find(key);

  if (it == Numeric.end())
    return NULL;

  Item_type *result = new Item_type(it->second);
  result->State = 1;

  return result;
}


INT HASH::Check(Key_type key) const
{
  return Numeric.find(key) != Numeric.end() ? 1 : 0;
}


void HASH::Clear()
{
  Numeric.clear();
  Strings.clear();
}
