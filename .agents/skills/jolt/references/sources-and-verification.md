# Sources and verification

Contents: [baseline](#baseline-and-conflicting-claims), [source map](#source-map),
[probes](#small-probes), [Jolt implementation work](#changing-jolt-itself).

## Baseline and conflicting claims

Prepared 2026-10-06 from [jolt-lang.net](https://jolt-lang.net/), local checkout
`~/src/jolt` at `fb941836647cdaa307df9c50356493040be4e0a5`, and installed
`jolt v0.8.16` on macOS. The checkout was inspected; probes used the installed
binary. These are separate evidence sources, not proof that the binary was
built from that exact commit.

For a version-sensitive question: identify the target executable/version,
inspect its matching implementation and tests, then run a small probe. The
divergence registry's executable `:check` forms are more useful than old prose
summaries. Even README/llms comments can lag implementation.

| Stale claim encountered | Baseline finding | Where to verify |
| --- | --- | --- |
| BigDecimal is unavailable | `1M`, `decimal?`, `with-precision` work | `host/chez/java/bigdec.ss`; numeric corpus |
| Jolt selects `:bb` | Features are `:jolt :clj :default`; clause order wins | `host/chez/reader.ss`, `rdr-features` |
| CAS compares by value | Reference values use identity; immediate boxing differs | `host/chez/atoms.ss`; registry `:concurrency-model` |
| No proxy or reflection | Partial modeled support exists, no JVM class generation | core `30-macros.clj`; `stdlib/clojure/reflect.clj` |
| Every lazy seq is `LazySeq` | Many specialized seq types now exist | registry `:seq-type-model`; corpus |
| Private vars aren't enforced; ns-aliases is zero-arity only | Current privacy enforcement and one-argument alias query work | analyzer; `host/chez/ns.ss`; corpus |
| Direct-linking/inference require opt-in | Release defaults enable them; `--dev`/`--no-direct-link` opt out | `jolt-core/jolt/main.clj`, build handling |

Site starting points: [documentation](https://jolt-lang.net/docs/getting-started.html),
[differences](https://jolt-lang.net/docs/differences.html),
[host interop](https://jolt-lang.net/docs/host-interop.html),
[native interop](https://jolt-lang.net/docs/native-interop.html),
[CLJC](https://jolt-lang.net/docs/cljc-interop.html). Use the docs navigation or
search for specialized API pages instead of guessing a URL. If the local
checkout is absent, use the [source at the baseline commit](https://github.com/jolt-lang/jolt/tree/fb941836647cdaa307df9c50356493040be4e0a5)
or the revision matching the target runtime.

## Source map

All paths here are relative to the Jolt checkout, not the consuming project.
Use `rg` to narrow first; the corpus, changelog, and registry are large.

| Question | Open/search |
| --- | --- |
| Tracked Clojure differences and executable examples | `test/conformance/known-divergences.edn`; read `:legend`, then search the behavior/category |
| Expected ordinary behavior | `test/chez/corpus.edn`, `test/chez/unit.edn`, focused files under `test/chez/` |
| CLI, build defaults, native declaration loading | `jolt-core/jolt/main.clj` |
| Dependency resolution, aliases, add-deps | `jolt-core/jolt/deps.clj`, `jolt-core/jolt/deps/`, `jolt-core/clojurestar/deps.cljc` |
| Tasks | `jolt-core/jolt/tasks.clj`, `stdlib/babashka/tasks.clj` |
| Reader features/literals | `host/chez/reader.ss`, `stdlib/jolt/reader.clj` |
| Namespace source search and resource loading | `stdlib/jolt/loader.clj`, `host/chez/ns.ss` |
| Core macros/functions | `jolt-core/clojure/core/NN-*.clj`; docs tier may repeat JVM text without guaranteeing implementation |
| Host classes/methods | `host/chez/java/`; `host-static-classes.ss`, `class-hierarchy.ss`, `class-extensions.ss`, `proxy.ss` |
| Atom identity, threads, channels, fibers | `host/chez/atoms.ss`, `host/chez/java/concurrency.ss`, `host/chez/java/async.ss`, `host/chez/fibers.ss` |
| FFI API and ABI | `stdlib/jolt/ffi.clj`, `host/chez/java/ffi.ss`, `test/chez/build-lib/driver.c` |
| FFI examples/tests | `test/chez/jolt-ffi-*-test.clj`, `test/chez/ffi-app/`, `test/chez/build-lib/` |
| Compiler and numeric specialization | `jolt-core/jolt/{analyzer,ir,backend_scheme,passes}.clj`, `jolt-core/jolt/passes/` |
| Scheme backend capabilities | `host/scheme-adapter/CONTRACT.txt`, `host/gambit/`, `CONTRIBUTING.md` |
| Build prerequisites and gates | `Makefile`, `CONTRIBUTING.md`, `llms.txt` |

Example targeted search (set the checkout path to its actual location):

```sh
rg -n 'compare-and-set|concurrency-model' "$HOME/src/jolt/test/conformance/known-divergences.edn"
rg -n 'rdr-features|reader-cond' "$HOME/src/jolt/host/chez/reader.ss"
rg -n 'closed-world|direct-link|--dev' "$HOME/src/jolt/jolt-core/jolt/main.clj"
```

## Small probes

Use a temporary directory without project deps, and `-Srepro` to exclude user
deps, when the question is about the runtime alone. For application behavior,
run in the actual project with its aliases. A quoted heredoc avoids shell
expansion of Clojure syntax:

```sh
jolt -Srepro - <<'JOLT'
(prn :version (System/getProperty "jolt.version"))
(prn :feature (read-string "#?(:bb :bb :jolt :jolt :clj :clj)"))
(prn :decimal (decimal? 1M) (with-precision 4 (/ 1M 3M)))
(prn :unicode (count "😀") (subs "a😀b" 1 2))
(prn :cas (let [a (atom [1])] [(compare-and-set! a [1] [2]) @a]))
JOLT
```

Baseline outputs: `:jolt`; `true 0.3333M`; `1 "😀"`; `[false [1]]`.
Keep probes proportionate: one disputed API or one representative application
path, then the relevant tests. Do not certify a library by `require` alone.

## Changing Jolt itself

This section applies only to implementation work inside Jolt, not ordinary
application edits. Read its current contributor instructions first.

- The checked-in seed embeds the reader/compiler/core and seven standard
  namespaces: `clojure.string`, `walk`, `template`, `edn`, `set`, `pprint`,
  `repl`. Changes to those need `make remint`; merely rebuilding a binary can
  silently leave old behavior. Confirm the list in `host/chez/emit-image.ss`
  (`ei-prelude-ns-files`) as it evolves.
- Runtime-only Chez shims generally need no remint. Generated Gambit files
  follow their Chez sources: use `make gambitgen` and `make gambitgencheck`.
- New `jolt.host` registrations need the manifest checked with
  `make manifestcheck`.
- Choose the relevant gate from the Makefile (`corpus`, `unit`, `ffi`, etc.).
  `make documented` checks documented Jolt results; `make certify` also uses
  JVM Clojure as the reference. `make selfhost` checks the compiler seed;
  `make test` is the broader gate. Preserve exit status when capturing logs.
