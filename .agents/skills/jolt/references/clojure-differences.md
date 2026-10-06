# Clojure differences

Contents: [host boundary](#host-boundary), [reader and files](#reader-and-files),
[values](#values), [concurrency and namespaces](#concurrency-and-namespaces),
[porting](#porting-checklist).

Scope: Chez Jolt v0.8.16 and the source revision recorded in `SKILL.md`.
Evidence paths below are relative to the Jolt checkout, normally `~/src/jolt`.
The [website's differences guide](https://jolt-lang.net/docs/differences.html)
is useful orientation but contains superseded claims; the implementation,
executable checks, and `test/conformance/known-divergences.edn` resolve them.

## Host boundary

`Class.`, `Class/method`, `.method`, `instance?`, and `:import` work against a
modeled class hierarchy and implemented shims, not loaded JVM classes. Maven
resolution extracts Clojure source; Java-only JARs do not supply runnable code.
There is no JVM bytecode generation, JNI, arbitrary class loading, or JVM GC.
`(:gen-class)` in an `ns` form does not produce a Java class.

Do not turn that into a blanket ban on Java-shaped APIs:

- Current `class` returns a modeled Class object. Use `.getName` when a class
  name is needed; older prose describing every class token as a string is stale.
- `clojure.reflect` and member reflection exist. They report registry knowledge;
  methods implemented outside those registries may be absent. Parameter types
  and overload information are incomplete; there is no ASM bytecode reflector.
- `proxy` exists, including delegation to shimmed concrete base objects and
  `proxy-super`. It does not generate JVM subclasses. A base object's internal
  call cannot dispatch back into a proxy override. `ThreadLocal` proxy support
  is specialized; inspect the macro before assuming arbitrary overrides work.
- Common `java.io`, `java.nio`, `java.util.concurrent`, and base `java.time`
  types exist. Check the exact class/method; do not add a dependency merely
  because an old page says all time classes are external.
- `System/getProperty "jolt.version"` identifies Jolt; `"java.version"` is
  deliberately nil. Do not invent a JDK version to select a library's code path.

For missing methods, inspect `host/chez/java/` and reproduce the exact call.
The additive `jolt.host/extend-class!` seam can fill a missing method; its
explicit override mode affects all callers. Consult its implementation in
`host/chez/java/class-extensions.ss` before registering. Avoid replacing an
entire class constructor to patch one member.

Evidence: `stdlib/clojure/reflect.clj`, `jolt-core/clojure/core/30-macros.clj`
(`proxy`), `host/chez/java/proxy.ss`, `host/chez/java/host-static-classes.ss`.

## Reader and files

The current feature set is `#{:jolt :clj :default}`. **`:bb` was removed** after
0.8.5. Matching follows source order, not an implicit priority among keys:

```clojure
#?(:jolt :native :clj :jvm :cljs :browser) ; => :native
#?(:clj :jvm :jolt :native)                ; => :jvm, even on Jolt
#?(:bb :babashka :clj :clojure)            ; => :clojure
```

Reader conditionals and splicing work in `.clj` and `.jolt` as well as `.cljc`.
Use `.cljc` for shared conditional code so the JVM also reads it correctly.
For a namespace on a source root, extension preference is `.jolt`, `.clj`,
`.cljc`; `data_readers.jolt` similarly overrides its portable sibling. Preserve
normal namespace-to-path spelling (dots to directories, hyphens to underscores).

Other useful distinctions:

- `1_000_000` and `0xFF_FF` are source number literals here. Separators must be
  between valid digits; `clojure.edn` rejects them. Do not put them in `deps.edn`.
- `#$"hello ~{name}"` is built-in interpolation. Custom punctuation dispatch
  readers are possible; see [reader extensions](runtime-and-extensions.md#reader-extensions).
- Core `read-string` returns nil on empty input and an inert tagged literal for
  an unknown tag. An unknown tag in compiled source still errors. Keep these
  claims separate from `clojure.edn/read-string` and explicit reader options.
- Reader position metadata, quoted-form metadata, and gensym spellings have
  differences. Test tools that inspect reader output rather than copying JVM
  metadata or generated-name assumptions.

Evidence: `host/chez/reader.ss` (`rdr-features`, `rdr-read-reader-cond`),
`stdlib/jolt/loader.clj`, `stdlib/jolt/reader.clj`, and the divergence registry.
The [CLJC guide](https://jolt-lang.net/docs/cljc-interop.html) describes the
override mechanisms, but its inclusion of `:bb` is outdated for this baseline.

## Values

| Area | Current behavior and practical consequence |
| --- | --- |
| Ordinary numbers | Exact integers, bignums, ratios, doubles, and BigDecimal (`M`, `with-precision`) work. `(= 3 3.0)` is false; `(== 3 3.0)` is true. Do not repeat the homepage's old "no BigDecimal" limitation. |
| Numeric classes | Narrow integer values share the exact-integer representation: `(class (byte 5))` names `java.lang.Long`. Float values also do not have a separate JVM-style box. Avoid dispatch based on narrow box classes/identity. This does **not** mean every narrowing cast ignores range checks. |
| Numeric edges | Some integer paths promote where JVM checked arithmetic overflows. NaN equality and Java `.equals`/`.hashCode` on decimal scale have tracked differences. Test bounds/NaNs if the algorithm relies on them; prefer Clojure value semantics. |
| Strings | `count`, `nth`, `seq`, `subs`, and matcher positions use codepoints. `(count "😀")` is 1 and `(subs "a😀b" 1 2)` is `"😀"`. Codepoints are still not grapheme clusters. |
| Regex | irregex-backed, with a substantial Java-pattern compatibility layer. Test required flags, captures, lookarounds, and Unicode cases; neither "all Java regex works" nor "all advanced regex is missing" is reliable. |
| Laziness | Current `Repeat`, `Cycle`, and many other specialized seq types and chunking behaviors match Clojure. Old claims that every seq is `LazySeq` are stale. Remaining chunking/type differences are in the registry; avoid relying on side-effect counts. |
| Printing | `(str (map inc [1 2]))` renders contents here. Prefer `pr-str` for readable data; do not parse JVM `Class@hash` display output. |
| Extra permissiveness | Examples include `(.-value {:value 41})`, `keys` on a vector of pairs, and lookup of bare `deftype` fields without `ILookup`. Successful Jolt execution does not establish JVM portability. |

## Concurrency and namespaces

- Atoms, futures, promises, agents, `pmap`, refs/STM, and core.async exist.
  Explicitly wait for work that must finish: an outstanding OS thread does not
  keep the process alive after main returns; `.setDaemon` does not change this.
- `compare-and-set!` now uses identity (`identical?` / Chez `eq?`), **not value
  equality**. `(let [a (atom [1])] (compare-and-set! a [1] [2]))` is false.
  Immediate numbers/characters still lack the JVM's separate allocation/cache
  identities. Keep normal read/compute/CAS loops; do not emulate boxing tests.
- Ordinary namespace privacy and one-argument `ns-aliases` work on this
  baseline. Older claims that privacy is universally ignored or `ns-aliases`
  only accepts zero arguments are not a sound basis for new code.
- Redefining `deftype` replaces its modeled type; old instances/call sites do
  not get the JVM's isolated old class definition. Recreate instances after
  changing a type layout in a REPL.

Evidence: `host/chez/atoms.ss`, `host/chez/java/concurrency.ss`,
`test/conformance/known-divergences.edn`, and `test/chez/corpus.edn`.
See [runtime guidance](runtime-and-extensions.md#threads-and-fibers) for fibers.

## Porting checklist

1. Run the existing code/tests before rewriting it; the compatible surface is
   larger than the absence of a JVM suggests.
2. Classify failures: missing source/dependency, unimplemented host member,
   actual language difference, or a stale assumption about Jolt's version.
3. Keep isolated changes in `#?(:jolt ... :clj ...)`. Use a `.jolt` namespace
   sibling when the implementation substantially differs, and C FFI for native APIs.
4. Verify both runtimes when the source remains shared. In particular, test
   strings/offsets, numeric boundaries, required host calls, and shutdown behavior.
