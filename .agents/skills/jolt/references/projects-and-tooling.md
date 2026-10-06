# Projects and tooling

Contents: [run](#run), [dependencies](#dependencies), [REPL](#repl-and-diagnostics),
[tests](#tests), [build](#standalone-builds).

Source authority: Jolt `jolt-core/jolt/main.clj`, `jolt-core/jolt/deps.clj`,
`jolt-core/jolt/tasks.clj`, and README CLI/build sections. The site has
[building](https://jolt-lang.net/docs/building-and-deps.html),
[testing](https://jolt-lang.net/docs/testing.html), and
[REPL](https://jolt-lang.net/docs/repl-driven-development.html) guides.
Check `jolt --help` for the actual installed version.

## Run

Use the project directory as the working directory; it supplies `deps.edn`.

```sh
jolt -e '(+ 1 2)'
jolt script.clj arg1
jolt -f script.clj             # disambiguate a filename from a task/command
jolt run -m app.core arg1      # invokes -main
jolt -M:test                  # project's test alias, if defined
jolt -A:dev -e '(println :ok)'
jolt -X:tool :option :value    # alias :exec-fn receives a map
jolt -T:tool :option :value    # tool mode replaces project paths/deps
```

`-e` resolves the current project's deps too. `-Sdeps '<edn>'` merges additional
paths/deps. `-e -` reads an expression from stdin; `jolt -` runs a script from
stdin. `--` ends option parsing. Arguments reach `*command-line-args*` (or `-main`).
Executable scripts may start with `#!/usr/bin/env jolt`.

Use an installed binary when available. To run the checkout, use
`"$HOME/src/jolt/bin/jolt"` from the application directory; the launcher preserves
that directory as `JOLT_PWD`. Source mode needs Chez and the relevant submodules;
do not assume the installed binary executes edited checkout sources.

## Dependencies

A minimal project needs `{:paths ["src"]}` and `src/app/core.clj` containing
`(ns app.core)` and a `-main`. Add libraries through ordinary `:deps` coordinates:

- `:local/root` for a local project (or a source-carrying JAR).
- `:git/url` plus a full `:git/sha`; recognized host-style lib names can derive
  the URL. A tag with a verified SHA prefix is also supported on current Jolt.
- `:mvn/version` for Clojure source in a Maven JAR. The POM supplies transitives;
  this does not execute `.class` files or acquire a Java runtime.

Top-level coordinates pin transitives. Current resolution handles exclusions
and version conflicts; inspect the chosen graph rather than assuming the
older breadth-first resolver's behavior. `org.clojure/clojure` and
`org.clojure/clojurescript` are terminal host-provided coordinates: they supply
neither an artifact nor children. Declare `org.clojure/spec.alpha` and
`org.clojure/core.specs.alpha` explicitly when needed.

```sh
jolt -Sdescribe          # version/config/cache information
jolt -A:test -Spath      # resolved source roots for these aliases
jolt -Stree             # expansion/conflict decisions
jolt -Sgraph            # selected dependency graph
jolt -P                 # fetch dependencies without running the app
jolt -Srepro -M:test     # omit user-level deps.edn for a reproducible check
```

`-Soutdated` checks repository metadata over the network. `-Strace` writes
`trace.edn`. `-Scp` uses explicit roots without expanding dependencies, so it
does not load a dependency's native declarations. `-J...`, `-Sforce`, and
`-Sthreads` are accepted but ignored; JVM tuning flags accomplish nothing here.

Aliases add `:extra-paths`/`:extra-deps`; `:main-opts` is last-wins. Existing
`:tasks` can be listed with `jolt tasks` and run with `jolt <task>`. Current Jolt
also supports `bb.edn` tasks; check `jolt.tasks` for precedence/argument parsing
when migrating a task runner. Do not assume this makes the runtime Babashka.

For runtime acquisition, `jolt.deps/add-deps` appends source roots, returns the
added roots, and **does not load their `:jolt/native` declarations**.
`clojurestar.deps/add-deps` is the portable facade (returns nil).
Prefer project deps for reproducible builds; dynamic dependency acquisition
requires special attention when producing a closed binary.

## REPL and diagnostics

`jolt repl` and `jolt nrepl-server 0` resolve project deps and native libraries.
Port 0 chooses a free port and writes `.nrepl-port`; omit the number for 7888.
The nREPL server uses dev behavior, so function redefinition affects subsequent
calls. Basic protocol support is built in; richer CIDER operations require the
appropriate middleware dependency and `:nrepl/middleware` configuration.

| Need | Use |
| --- | --- |
| Machine-readable uncaught error | `JOLT_DIAG=edn` |
| Optional located type warnings | `JOLT_CHECK=1` (warnings, not a proof) |
| Dependency/native resolution details | `JOLT_DEBUG=1` or CLI `-Sverbose` |
| Heap ceiling | `JOLT_MAX_HEAP=512m` (or a suitable size); JVM `-Xmx` is ignored |
| GC diagnostics | `JOLT_GC_LOG=1` |
| Built-program startup stages | `JOLT_STARTUP_PROFILE=1` |

For clj-kondo and FFI macros, Jolt exports hooks/config from its checkout:

```sh
mkdir -p .clj-kondo
clj-kondo --lint "$HOME/src/jolt" --dependencies --copy-configs
```

The installed binary alone does not carry this export. Use the hooks to
understand `defcfn` and scoped allocations, not blanket unresolved-symbol ignores.

## Tests

Use the project's existing runner/alias. `clojure.test` is included and the
Cognitect test runner is supported. For a focused check without a runner:

```sh
jolt -Sdeps '{:paths ["src" "test"]}' -e '
  (require (quote app.core-test) (quote clojure.test))
  (let [{:keys [fail error]} (clojure.test/run-tests (quote app.core-test))]
    (System/exit (if (zero? (+ fail error)) 0 1)))'
```

Pass namespaces explicitly and require them first. Printing the `run-tests`
summary alone does not make test failures a failing process exit. Check that
the expected tests actually ran. Keep tests in a test alias's `:extra-paths`.

## Standalone builds

```sh
jolt build -m app.core -o app
./app arg1
jolt build -m app.core --dev -o app-dev
jolt build -m app.core --closed-world -o app-small
```

Current release defaults **direct-link and infer types**; `--opt` additionally
changes Chez compile parameters, omitting information needed for Clojure
backtraces. It is not the switch that first enables all optimization.
`--dev` or `--no-direct-link` keeps application calls routed through vars.
Individual `^:redef` or `^:dynamic` vars remain dynamic when direct-linking.
Use dev/dynamic routing for an embedded REPL or hot-reloaded code.

`--closed-world` (older spelling `--tree-shake`) prunes unreachable definitions.
Runtime resolution can prevent pruning; read the reported call path. Do not
blindly copy `:jolt/tree-shake {:allow-dynamic [...]}` hints: they assert a
resolution site is dead or restricted to baked code. A wrong assertion can
silently make `resolve` return nil or remove the compiler needed later, even
in a default release build. `eval`/`load-string` cannot be waved away this way.

Bake namespaces reached only through runtime lookup with repeatable
`--include app.plugin` or `:jolt/build {:include [app.plugin]}`. Embedding a
namespace and preserving all dynamically resolved definitions are separate
concerns; exercise the actual lookup path in the finished binary.

Native libraries with a declared `:static` archive can be linked in. Those
without one remain runtime dependencies; `--dynamic` forces dynamic natives.
Inspect the build's native-dependency report before calling an artifact
self-contained. Linking needs a C toolchain and Chez development files (the
prebuilt Jolt distribution bundles the latter).

Additional targeted switches: `--boot small` compresses the heap image;
`--signable` produces a structurally complete executable for strict signing;
`--library` exposes a shared-library C ABI (see [interop](native-interop.md#embedding-jolt)).
Check target/toolchain requirements before cross-compiling.
