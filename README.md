# cpp-snippets

## namespace
 Namespace `gh4ck3r` is applied to entire snippets.

## Type
### LazyGetter<GETTER>
 * a type that fetch value from `GETTER()` on first demand.
 * can be assigned with a value prior to `GETTER()`
 * `GETTER` is gone once it has a value by any means.

## Functionality
### hexdump
 Dump linear buffer to `std::string`. Following forms are possible.
  * `hexdump(ptr, len)`: dump from given `ptr` to `len` in byte unit.
  * `hexdump(beg, end)`: dump iterator based range
