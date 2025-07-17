# toolbox-cpp

## namespace
 Namespace `gh4ck3r` is applied to entire snippets.

## Type
### LazyGetter<GETTER>
 * a type that fetch value from `GETTER()` on first demand.
 * can be assigned with a value prior to `GETTER()`
 * `GETTER` is gone once it has a value by any means.

### Logger
 * `std::ostream` wrapper that is capable of indentation.
 * Indentation level can be adjusted via `indent()`, `unindent()`
     method or manipulator like one defined in iomanip (e.g. std::hex).

### Singleton
### SharedSingleton<T>
 `SharedSingleton<T>` is a singleton based on `std::shared_ptr<T>`. it
 constructs instance of T on first construction of `SharedSingle<T>` instance.
 Once the instance is constructed it can be shared as `std::shared_ptr<T>` do
 and destructed once the reference count decreased to 0. Even though the
 instance is gone, making next first instance makes new `T` instance.

### StaticSingleton<T>
 `StaticSingle<T>` is a template class which makes given `T` into typical
 singleton instance.

## Functionality
### hexdump
 Dump linear buffer to `std::string`. Following forms are possible.
  * `hexdump(ptr, len)`: dump from given `ptr` to `len` in byte unit.
  * `hexdump(beg, end)`: dump iterator based range

### concatenate
 Concatenate `constexpr` containers, i.e. std::array, std::string_view.
  * `concat<string_view, string_view, ...>()`: concatenate given `string_view`s
    * Template parameters should have `static` storage class.
    * Concatenated string_view ended with `null`.
  * `concat(std::array...)`: concatenate given `std::array`s

### recipe
 Function `recipe(invocable1, invocable2, ...)` returns a lambda function which
forward given arguments to `invocable1` and forward its return to next one until
last argument which returns final return value. It's similar to `std::range`
from C++20 semantically.
