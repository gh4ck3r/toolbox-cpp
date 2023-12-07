# cpp-snippets

## namespace
 Namespace `gh4ck3r` is applied to entire snippets.

## Functionality
### hexdump
 Dump linear buffer to `std::string`. Following forms are possible.
  * `hexdump(ptr, len)`: dump from given `ptr` to `len` in byte unit.
  * `hexdump(beg, end)`: dump iterator based range
