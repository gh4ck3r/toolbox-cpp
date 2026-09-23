# toolbox-cpp

A collection of Modern C++ (C++17/C++20) header-only utility snippets and abstractions.

All components are encapsulated within the `gh4ck3r` namespace (and its sub-namespaces).

## Table of Contents
- [Data Structures & Containers](#data-structures--containers)
  - [list_head](#list_head-gh4ck3rlist_headhh)
  - [LazyGetter](#lazygetter-gh4ck3rlazygetterhh)
  - [Singleton](#singleton-gh4ck3rsingletonhh)
- [String & Formatting Utilities](#string--formatting-utilities)
  - [split](#split-gh4ck3rsplithh)
  - [hexdump](#hexdump-gh4ck3rhexdumphh)
  - [concatenate](#concatenate-gh4ck3rconcathh)
  - [base64](#base64-gh4ck3rbase64hh)
- [System & Process Management](#system--process-management)
  - [process](#process-gh4ck3rprocesshh)
  - [file](#file-gh4ck3rfilehh)
  - [Logger](#logger-gh4ck3rloggerhh)
  - [reaper](#reaper-gh4ck3rreaperhh)
  - [defer](#defer-gh4ck3rdeferhh)
- [Functional & Metaprogramming](#functional--metaprogramming)
  - [recipe](#recipe-gh4ck3rrecipehh)
  - [function_traits](#function_traits-gh4ck3rfunction_traitshh)
  - [hash](#hash-gh4ck3rhashhh)
  - [type_traits](#type_traits-gh4ck3rtype_traitshh)
  - [typemap](#typemap-gh4ck3rtypemaphh)
- [Cryptography](#cryptography)
  - [crypto](#crypto-gh4ck3rcryptohh)

---

## Data Structures & Containers

### `list_head` (`gh4ck3r/list_head.hh`)
Intrusive doubly-linked list implementation inspired by the Linux kernel `list_head`, adapted for Modern C++ with STL-compatible iterators and range views.

### `LazyGetter<GETTER>` (`gh4ck3r/lazygetter.hh`)
A wrapper for lazy evaluation that defers value fetching until first demanded via `GETTER()`. Once populated, the value is cached and the getter function is discarded.

### `Singleton` (`gh4ck3r/singleton.hh`)
- **`SharedSingleton<T>`**: Ref-counted singleton managed via `std::shared_ptr<T>`. Instantiates `T` on first use and automatically destructs when all references are released. Subsequent accesses recreate the instance as needed. Thread-safe instance locking.
- **`StaticSingleton<T>`**: Meyer's static singleton wrapper enforcing non-copyable, non-movable semantics for `T`.

---

## String & Formatting Utilities

### `split` (`gh4ck3r/split.hh`)
Splits a `std::string_view` by a character delimiter with optional escape character support:
```cpp
auto tokens = gh4ck3r::split<',', '\\'>("1\\,2,3"); // {"1\\,2", "3"}
```

### `hexdump` (`gh4ck3r/hexdump.hh`)
Formats binary buffers into human-readable hexadecimal string representation. Accepts raw pointer + length, iterator ranges, or standard containers.

### `concatenate` (`gh4ck3r/concat.hh`)
Compile-time (`constexpr`) concatenation utilities for string views and arrays:
- `concat<sv1, sv2, ...>()`: Concatenates static `std::string_view` literals at compile time.
- `concat(array1, array2, ...)`: Concatenates `std::array` instances at compile time.

### `base64` (`gh4ck3r/base64.hh`)
Base64 encoding and decoding functions supporting `std::string`, `std::string_view`, and byte buffers.

---

## System & Process Management

### `process` (`gh4ck3r/process.hh`)
Process inspection and execution helpers for Linux:
- `ppidof()`, `nameof()`, `cmdof()`, `execof()`, `exists()`: Query process information via `/proc`.
- Process execution with I/O redirection, timeout handling, and exit status collection.

### `file` (`gh4ck3r/file.hh`)
High-level file I/O operations including `read_file()`, `write_file()`, `append_file()`, and temporary file generation.

### `Logger` (`gh4ck3r/logger.hh`)
Indentation-aware `std::ostream` wrapper for structured logging. Supports `indent()` and `unindent()` controls alongside standard stream manipulators.

### `reaper` (`gh4ck3r/reaper.hh`)
Generic RAII resource manager (`Reaper<T, Cleaner>`) for safe resource cleanup (file descriptors, `FILE*`, dynamic memory, custom handles).

### `defer` (`gh4ck3r/defer.hh`)
Scope-bound execution guard (`defer`) that runs a lambda or callable upon leaving the current scope.

---

## Functional & Metaprogramming

### `recipe` (`gh4ck3r/recipe.hh`)
Function composition helper `recipe(f1, f2, ...)` returning a callable pipeline that forwards arguments through a series of functions sequentially. Supports move-only callables and perfect forwarding.

### `function_traits` (`gh4ck3r/function_traits.hh`)
Extracts signature details (return type, argument types, arity) from callables, lambdas, function pointers, and member function pointers.

### `hash` (`gh4ck3r/hash.hh`)
Hash generation and `hash_combine` utilities for custom structs, `std::pair`, `std::tuple`, and enum types for use with unordered STL containers.

### `type_traits` (`gh4ck3r/type_traits.hh`)
Metaprogramming type traits such as `is_complete_v<T>` to inspect type completeness at compile time.

### `typemap` (`gh4ck3r/typemap.hh`)
Compile-time type-to-type and key-to-type mapping system via `typedef_t`, `declare_t`, and `at<KEY>`. Supports mixed key types (integers, enums, etc.).

---

## Cryptography

### `crypto` (`gh4ck3r/crypto.hh`)
OpenSSL wrappers for cryptographic operations including SEED-CBC cipher encryption/decryption and `OSSL_LIB_CTX` management.
