# Runtime and extensions

Contents: [threads/fibers](#threads-and-fibers), [reader](#reader-extensions),
[Scheme](#scheme-interop), [images](#program-images), [performance](#performance).
These are optional capabilities; use the section that the task needs.

## Threads and fibers

Native Chez Jolt has real OS threads over a shared heap. `future`, agents,
promises, `pmap`, refs/STM, and core.async are implemented. Main returning ends
the process: deref/join/await work that must complete.

Current core.async defaults `*go-backend*` to `:thread`; `go` is not necessarily
the JVM's state-machine scheduling model. A fiber backend can be selected:

```clojure
(require '[clojure.core.async :as a])
(a/<!! (binding [a/*go-backend* :fiber] (a/go 42))) ; => 42
```

For direct fiber handles:

```clojure
(require '[jolt.fibers :as fibers])
(fibers/join (fibers/spawn (fn [] 42))) ; => 42
```

Fibers remain on their carrier thread, convey dynamic bindings (not the parent
STM transaction), and are preempted. Channel waits, deref, `Thread/sleep`, and
Jolt socket/process I/O can park them. Arbitrary blocking FFI pins the carrier.
Use an OS thread for uncooperative blocking work. `fibers/interrupt!` injects an
exception; it differs from setting a Thread interrupt flag. `join` propagates
the body's error. Consult `stdlib/jolt/fibers.clj`,
`stdlib/clojure/core/async.clj`, and `host/chez/java/async.ss` for current behavior.

Do not transfer these concurrency/native assumptions to the Gambit browser
backend. Check its capabilities and reduced-build exclusions separately.

## Reader extensions

```clojure
(let [name "Ada"] #$"hello ~{name}") ; => "hello Ada"
```

`jolt.reader/set-dispatch-macro!` registers punctuation after `#`. In the normal
tier a function receives the next form and returns its replacement. The raw
tier `{:raw true}` receives source and index as two arguments and returns
`[form end-index]`. Read its docstring before implementing a new literal.

Registration is **process-wide** and becomes effective after its top-level
form executes. Register and use in separate top-level forms; an enclosing
`do` does not make a later unreadable literal readable while that `do` is
being read. Reserved dispatch characters and tag-starting letters/digits
cannot be replaced. `clojure.edn` never uses this table. Keep custom reader
syntax in Jolt-specific source. Evidence: `stdlib/jolt/reader.clj`.

## Scheme interop

`jolt.scheme/proc`, `call`, `eval-string`, and `defsfn` expose the underlying
Scheme environment. Prefer normal Jolt APIs or C FFI when they express the task.
This layer is intentionally host-specific and passes values **without conversion**:
Jolt nil is neither Scheme `#f` nor the empty list; Jolt collections are not
Scheme lists/vectors. Numbers, strings, booleans, and characters share representations.

```clojure
(require '[jolt.scheme :as scm])
(scm/call "string-length" "abc") ; => 3
```

The `scheme` macro renders Jolt-read forms to Scheme text; use its documented
spellings, not an assumed second reader. Runtime name lookup may fail after
tree-shaking. See `stdlib/jolt/scheme.clj` for the raw-value contract.

`jolt.continuations/call-cc` / `letcc` provide a one-shot scoped escape. It may
run only while its capture is live, on the same thread/fiber; it is not a
re-entrant generator continuation. Exiting runs `finally` and restores dynamic
bindings. See `stdlib/jolt/continuations.clj` before composing escapes with fibers.

## Program images

`jolt.image/dump!` and `read-image` persist a value graph, including supported
functions/lazy values; `dump-world!` captures namespace/var state. They do not
resume thread stacks, live continuations, or arbitrary OS resources.
Use `scan`/`dumpable?` to diagnose unsupported values before writing an image.
Handlers and optional stubs can represent resources that need reopening.

Restoring closures compiles their source: an image is executable input, not
plain EDN. Restore trusted images. Images are pinned to a runtime version even
when portable across CPU architectures. See `stdlib/jolt/image.clj` for the
serialization contract and the site index for the Program Images guide.

## Performance

Measure the deployed build mode; REPL behavior includes dynamic calls and may
not predict release code. Current default release builds already infer types,
inline, and direct-link. Hints such as `^long`/`^double` and explicit casts can
enable numeric specialization when inference lacks evidence. Treat a hint as
a contract; don't add it speculatively or assume JVM primitive boxing rules.
Chez fixnum width is not Java long width. Inspect
`jolt-core/jolt/passes/numeric.clj` for overflow/unchecked arithmetic work.

The [performance guide](https://jolt-lang.net/docs/performance-idioms.html)
explains two particularly useful representation costs:

- Growing a string with repeated `str` copies the accumulator. Use
  `StringBuilder` append then one final `toString`, or `str/join`/`apply str`
  for a collection already in hand. Avoid repeatedly forcing the builder.
- `subs` copies its range. For scanning, reuse a `re-matcher` and set its
  `.region` instead of allocating a suffix for every regex attempt. Matcher
  indices remain codepoint indices. Use noncapturing groups when captures
  are unnecessary and verify their performance on the target pattern.

Use Jolt heap/GC diagnostics from [tooling](projects-and-tooling.md#repl-and-diagnostics)
instead of JVM flags. Keep timing claims local to the workload and build.
