#ifndef HASH_HXX
#define HASH_HXX

#include <string>
#include <unordered_map>

typedef INT    Key_type;
typedef STRING Data_type;

class Item_type {
public:
  Item_type() : Key(-1), State(0) {}
  ~Item_type() = default;

  Key_type  Key;
  INT       State;
  Data_type Block;
};


class HASH {
public:
  HASH();
  explicit HASH(size_t Size);

  //
  // Legacy numeric-key interface.
  //
  INT        Insert(const Item_type& item);
  Item_type *Find(Key_type key) const;
  INT        Check(Key_type key) const;

  //
  // String-key interface.
  //
  bool       GetValue(const STRING& name, STRING *value) const;
  STRING     GetValue(const STRING& name) const;

  void       AddEntry(const STRING& definition);
  void       AddEntry(const STRING& name, const STRING& value);

  void       Clear();

  ~HASH() = default;

private:
  using NumericTable =
      std::unordered_map<Key_type, Item_type>;

  using StringTable =
      std::unordered_map<std::string, Data_type>;

  NumericTable Numeric;
  StringTable  Strings;

  void Setup(size_t Size);
};

typedef HASH *PHASH;

#endif
