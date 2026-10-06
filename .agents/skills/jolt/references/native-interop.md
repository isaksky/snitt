# Native interop

Contents: [declarations](#libraries-and-functions), [memory](#memory-and-layouts),
[callbacks](#blocking-calls-and-callbacks), [embedding](#embedding-jolt).

Read `stdlib/jolt/ffi.clj` in the Jolt checkout for exact signatures and current
contracts. It is more current than parts of the
[native guide](https://jolt-lang.net/docs/native-interop.html). Compiler lowering
lives in `jolt-core/jolt/analyzer.clj` / `backend_scheme.clj`; host support and
focused tests are indexed in [sources](sources-and-verification.md).

## Libraries and functions

Wrap C APIs in a normal Clojure namespace using `jolt.ffi`. Declare shared
libraries in `deps.edn` so startup loads them before requiring source:

```clojure
{:paths ["src"]
 :jolt/native [{:name "sqlite3"
                :darwin ["libsqlite3.0.dylib" "libsqlite3.dylib"]
                :linux ["libsqlite3.so.0" "libsqlite3.so"]
                :windows ["sqlite3.dll"]}]}
```

Names without separators use the OS loader search path. Relative paths such as
`native/libshim.so` resolve against the **declaring dependency's** `deps.edn`,
not the consuming application's directory. `:process true` selects symbols
already in the process; `:optional true` permits a missing optional library but
does not make calling its functions valid. Build your own native artifact
before running code that requires it.

For standalone bindings, explicitly load before use; for libc a process load
can suffice. This example returns C's UTF-8 byte count, not a codepoint count:

```clojure
(require '[jolt.ffi :as ffi])
(ffi/load-library)
(ffi/defcfn c-strlen "strlen" [:string] :size_t)
(c-strlen "abc") ; => 3
```

`defcfn` creates a named callable; `foreign-fn` creates one without a def.
`cfn` is an alias macro. Signatures and symbol names must be literal at compile
time. A computed signature or arbitrary function-pointer target is not the
Babashka FFI function API, despite similar names.

Match the actual C header/ABI:

- `:int`, `:uint`, `:long`, `:ulong`, fixed widths (`:int32`, `:uint64`, etc.),
  `:size_t`/`:ssize_t`, `:pointer`, `:float`, `:double`, `:string`, `:void`.
  Do not assume C `long` is pointer-sized on every platform.
- `:bool` is C99 `_Bool` (one byte), not every library's integer `BOOL` typedef.
  It returns a Jolt boolean. An ordinary C integer result of 0 is truthy in
  Clojure, so test numeric return codes explicitly.
- `:string` marshals strings and maps NULL to/from nil. False and other
  non-string values are rejected. `:pointer` uses `ffi/null`/`ffi/null?`.
- Use `:pointer` for a returned C string whose ownership must be released;
  decode then call that API's destructor. An automatic `:string` conversion
  does not establish an ownership policy.
- Variadic signatures use `:&`; inspect the source for declared versus inferred
  tails. A newly inferred tail shape incurs compilation. Check C promotions.

`:static {:archive "native/libshim.a"}` (optionally per platform) supplies a
build-time archive; `:static {:lib "name" :libdir "..."}` is also supported.
Runtime candidates and static link inputs serve different purposes.
Do not link one dependency against a private static copy of another when both
must share native globals. Jolt can report duplicate native symbols;
`ffi/defining-libraries` helps identify them.

## Memory and layouts

Pointers are raw integer addresses, not bounds-checked JVM memory segments.
`pointer?` is not proof of a live allocation. Preserve ownership, byte counts,
alignment, and the native API's allocator/free pairing.

Prefer a scope or explicit arena lifetime for owned memory:

```clojure
(ffi/with-out [p :int]
  (ffi/write p :int 42)
  (ffi/read p :int)) ; => 42

(def point (ffi/layout [:struct [[:x :double] [:y :double]]]))
(ffi/with-layout [p point]
  (ffi/write p point {:x 1.0 :y 2.0})
  (ffi/read-field p point :y)) ; => 2.0
```

Exact ordering is `(ffi/read pointer type [byte-offset])` and
`(ffi/write pointer type value [byte-offset])`: **value precedes offset**.
Layouts are compiled by the `layout` macro from a literal descriptor; use
`layout-size`, `layout-alignment`, `field-offset`, `read-field`, `write-field`
instead of guessing padding. Unions need the caller to know the active member.

`with-alloc`, `with-out`, `with-layout`, `with-c-string`, and
`with-c-string-array` free their storage when the body exits. Pointers must not
escape those scopes if native code retains them. Arena options are confined
(one thread), shared (multiple threads), global (process lifetime), and auto
(GC lifetime). `with-arena [a]` creates/closes a confined arena. Native pointers
retained by C do not keep an auto arena reachable.

For binary data, `read-array`/`write-array` copy bytes, and `read-into!` fills an
existing byte-array slice. `read-bytes` decodes text; do not use it for arbitrary
binary payloads. `byte-buffer` views native memory without copying or owning it.
No view remains valid after its allocation is released.

Structs by value use `[:by-value descriptor]`. Aggregate returns need an extra
caller-owned destination pointer as the first Jolt argument. Aggregate
callbacks/exports and by-value unions are not supported; use a C shim when
needed. Check the detailed aggregate contract before extending a binding.

## Blocking calls and callbacks

- Declare long/blocking foreign calls with `:blocking` (or
  `{:blocking true}`), allowing other runtime threads to collect. This does
  not make a blocking operation fiber-aware; it still pins that carrier.
- Prefer `{:capture-native-error true}` on a supported non-void scalar result
  when an atomic `[result error-code]` is needed. Otherwise read `ffi/errno`
  immediately after failure; allocation, parking, or another FFI call can
  overwrite it.
- `foreign-callable` produces a C callback pointer; `free-callable` releases
  it. `callback` associates the pointer with an arena. Keep it alive until C
  has unregistered it and no invocation remains.
- Add `:collect-safe` when C can call back on a foreign OS thread **or on a Jolt
  thread deactivated inside a `:blocking` call**. Omitting it can cause a fatal
  memory fault. A callback used strictly inline by a nonblocking call has a
  different activation requirement.
- Use a shared arena for callbacks spanning threads. Unregister before closing
  the arena. Confined arenas and lexical helpers are for calls whose lifetime
  really ends at the scope boundary.

## Embedding Jolt

For C/C++/Rust hosts, use a shared-library build and exported typed callbacks:

```clojure
(ns bridge.core (:require [jolt.ffi :as ffi]))
(defn add [x y] (+ x y))
(ffi/export! "add" add [:int :int] :int)
```

```sh
jolt build --library -m bridge.core -o bridge
```

The host loads the resulting library, calls `jolt_library_init`, then uses
`jolt_lookup("add")` and the matching C function-pointer type. The library
contains a managed Scheme runtime and GC; it is not a set of independent C
functions. Read the actual ABI example in `test/chez/build-lib/driver.c` and
its release-thread variant before writing the host wrapper.

If the initialization thread returns to a native event loop, the documented
pattern is to release it with `jolt_library_release_thread` and thereafter
enter through `:collect-safe` exports. Check threading and collection rules in
the source and native guide for the target version. For GUI calls requiring
the main thread, inspect `jolt.host/call-on-main-thread` and its async variant.
