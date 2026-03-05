# Language Support Matrix

Nanox now ships with **118 built-in syntax profiles**. Each profile defines comment delimiters, flow/type/keyword sets, and highlighting toggles for triple quotes, brackets, and numerics. All built-ins are loaded from `syntax.ini`, and you can add more via the per-user `langs/` directory.

Profiles marked **regex** use `file_matches` (filename regex) instead of extension matching — these cover files like `Makefile`, `CMakeLists.txt`, `Dockerfile`, and systemd units that have no conventional extension.

| # | Language | Support | Extensions / File Match | Notes |
|---|----------|---------|-------------------------|-------|
| 1 | C | FULL | .c, .h | Includes C11 preprocessor directives |
| 2 | C++ | FULL | .cpp, .hpp, .cxx, .hxx, .cc | Templates, constexpr, and noexcept keywords |
| 3 | C# | FULL | .cs | Async/await aware |
| 4 | Objective-C | FULL | .m, .mm | Highlights @interface blocks and literals |
| 5 | Java | FULL | .java | Records, modules, and modern control flow |
| 6 | Kotlin | FULL | .kt, .kts | Covers coroutines and sealed classes |
| 7 | Scala | FULL | .scala, .sc | Match/case plus implicit constructs |
| 8 | Swift | FULL | .swift | Triple-quoted strings and protocol keywords |
| 9 | Go | FULL | .go | Channels, goroutines, defer |
| 10 | Rust | FULL | .rs | Ownership keywords, async/await |
| 11 | Python | FULL | .py, .pyw, .pyx, .pyi | Complete flow keywords, match/case, triple quotes |
| 12 | Ruby | FULL | .rb, .erb, .rake, .gemspec | Blocks, modules, DSL helpers |
| 13 | JavaScript | FULL | .js, .mjs, .cjs | Modern ES modules and async constructs |
| 14 | TypeScript | FULL | .ts, .tsx | Types, namespaces, declaration syntax; tsx included |
| 15 | PHP | FULL | .php, .phtml | Traits, namespaces, generators |
| 16 | Perl | FULL | .pl, .pm, .t | Given/when and common pragmas |
| 17 | R | FULL | .r | Base functions and constants |
| 18 | Julia | FULL | .jl | Block comments and macros |
| 19 | Dart | FULL | .dart | async/await plus mixins |
| 20 | Lua | FULL | .lua | Long brackets, local/block flow |
| 21 | POSIX Shell | FULL | .sh, .bash, .zsh, .ksh | Case/select, function defs |
| 22 | PowerShell | FULL | .ps1, .psm1, .psd1 | Blocks, trap, workflow |
| 23 | SQL | FULL | .sql | ANSI joins and DDL/DML verbs |
| 24 | HTML | FULL | .html, .htm | Tag-aware brackets |
| 25 | CSS | FULL | .css, .scss, .less | Selector braces and numbers |
| 26 | JSON/JSON5 | FULL | .json, .json5 | Bracket + numeric highlight |
| 27 | YAML | FULL | .yaml, .yml | Inline scalars |
| 28 | TOML | FULL | .toml | Tables and numeric literals |
| 29 | Markdown | FULL | .md, .markdown | Triple quotes for code fences |
| 30 | Visual Basic | FULL | .vb, .vbs, .bas | REM and `'` comments |
| 31 | Groovy/Gradle | FULL | .groovy, .gradle | DSL keywords; covers Gradle build scripts |
| 32 | Haskell | FULL | .hs, .lhs | `{- -}` block comments |
| 33 | INI/Config | FULL | .ini, .cfg, .reg | Section/key-value files, `;` and `#` comments |
| 34 | Makefile | FULL | **regex** `[Mm]akefile`, `*.mk`, `*.make` | ifeq/ifdef directives, .PHONY |
| 35 | Kconfig | FULL | **regex** `Kconfig`, `Kbuild` | Linux kernel config language |
| 36 | Systemd Unit | FULL | **regex** `.service`, `.socket`, `.timer`, `.target`, `.mount`, `.network`, … | Full unit file keyword set |
| 37 | VimScript | FULL | .vim + **regex** `.vimrc`, `init.vim` | VimScript 9 keywords included |
| 38 | CMake | FULL | .cmake + **regex** `CMakeLists.txt` | Functions, macros, find_package |
| 39 | QMake | FULL | .pro, .pri, .prf | Qt project files |
| 40 | XML | FULL | .xml, .xsd, .xsl, .xslt, .svg, .wsdl, .rss, .atom, .xaml, .xhtml, .plist, .resx | Tag-aware brackets |
| 41 | Maven | FULL | **regex** `pom.xml` | Maven POM keyword set |
| 42 | Ant | FULL | **regex** `build.xml`, `ivy.xml` | Ant/Ivy build target keywords |
| 43 | Protobuf | FULL | .proto | Messages, services, RPC stream types |
| 44 | Delphi/Pascal | FULL | .pas, .dpr, .dfm, .pp, .lpr | `{ }` and `(* *)` block comments |
| 45 | Vue | FULL | .vue | SFC template/script/style sections |
| 46 | JSX | FULL | .jsx | React JSX component files |
| 47 | Svelte | FULL | .svelte | each/await/then flow, reactive keyword |
| 48 | Astro | FULL | .astro | Frontmatter + component keywords |
| 49 | GraphQL | FULL | .graphql, .gql | Types, interfaces, directives |
| 50 | Dockerfile | FULL | **regex** `Dockerfile`, `Dockerfile.*` | All Docker instructions |
| 51 | Dotenv | FULL | .env + **regex** `.env`, `.env.*` | Environment variable files |
| 52 | Nix | FULL | .nix | Derivations, builtins, lib helpers |
| 53 | Zig | FULL | .zig | comptime, errdefer, orelse |
| 54 | Terraform/HCL (tf) | FULL | .tf, .tfvars | Resources, providers, lifecycle |
| 55 | HCL | FULL | .hcl | HashiCorp config language |
| 56 | Bazel/Starlark | FULL | .bazel, .bzl + **regex** `BUILD`, `WORKSPACE` | Rules, providers, depsets |
| 57 | Crystal | FULL | .cr | Ruby-like syntax with static types |
| 58 | Nim | FULL | .nim, .nims | `#[ ]#` block comments, pragma style |
| 59 | OCaml | FULL | .ml, .mli, .mll, .mly | `(* *)` block comments, functors |
| 60 | F# | FULL | .fs, .fsi, .fsx, .fsproj | Computation expressions, async |
| 61 | D | FULL | .d, .di | `/* */` and `/+ +/` nesting comments |
| 62 | Clojure | FULL | .clj, .cljs, .cljc, .edn | Lisp macros, threading macros |
| 63 | Elm | FULL | .elm | `{- -}` block comments, type aliases |
| 64 | Solidity | FULL | .sol | Smart contracts, modifiers, events |
| 65 | Awk | FULL | .awk | BEGIN/END blocks, built-in variables |
| 66 | MDX | FULL | .mdx | Markdown + JSX, triple-quote fences |
| 67 | V | FULL | .v, .vv | V language structs, interfaces |
| 68 | Meson | FULL | .meson + **regex** `meson.build` | Build targets, subprojects |
| 69 | Thrift | FULL | .thrift | Services, structs, type definitions |
| 70 | Bicep | FULL | .bicep | Azure resource definitions |
| 71 | Gleam | FULL | .gleam | Functional, type-safe Erlang-VM language |
| 72 | Odin | FULL | .odin | Data-oriented C alternative |
| 73 | ReScript | FULL | .res, .resi | OCaml-based JS compiler |
| 74 | Fish Shell | FULL | .fish | Fish-specific control flow and builtins |
| 75 | Nginx Config | FULL | **regex** `nginx.conf`, `*.nginx` | Server/location blocks, proxy directives |
| 76 | JSX (React) | FULL | .jsx | All React hooks, context, portals, concurrent API; enhanced in this update |
| 77 | Angular | FULL | **regex** `*.component.ts`, `*.service.ts`, `*.module.ts`, `*.directive.ts`, `*.pipe.ts`, `*.guard.ts`, `*.interceptor.ts`, `*.resolver.ts`, `*.component.html` | Angular decorators, RxJS, Signals API |
| 78 | CoffeeScript | FULL | .coffee, .litcoffee | Indented syntax, `###` block comments |
| 79 | Stylus | FULL | .styl | CSS preprocessor with JS-like syntax |
| 80 | Pug/Jade | FULL | .pug, .jade | Indented HTML template engine |
| 81 | Handlebars | FULL | .hbs, .handlebars, .mustache | Mustache-compatible templates |
| 82 | EJS | FULL | .ejs | Embedded JavaScript templates |
| 83 | Liquid | FULL | .liquid | Shopify/Jekyll template language |
| 84 | Jinja | FULL | .jinja, .jinja2, .j2 | Python template engine (Flask/Ansible) |
| 85 | GLSL | FULL | .glsl, .vert, .frag, .geom, .comp, .tesc, .tese, .rgen, .rmiss, .rchit, .rahit | OpenGL/Vulkan shader language |
| 86 | HLSL | FULL | .hlsl, .fx, .fxh | DirectX shader language |
| 87 | WGSL | FULL | .wgsl | WebGPU shader language |
| 88 | CUDA | FULL | .cu, .cuh | NVIDIA GPU programming |
| 89 | SystemVerilog | FULL | .sv, .svh | Hardware description / verification |
| 90 | VHDL | FULL | .vhd, .vhdl | VHSIC hardware description language |
| 91 | Assembly (NASM) | FULL | .asm, .nasm | x86/x64 assembly with NASM directives |
| 92 | Tcl/Tk | FULL | .tcl, .tk | Scripting + GUI toolkit |
| 93 | AutoHotkey | FULL | .ahk, .ahkl | Windows automation scripting |
| 94 | AppleScript | FULL | .applescript, .scpt | macOS automation |
| 95 | Batch/CMD | FULL | .bat, .cmd | Windows command shell scripts |
| 96 | Racket | FULL | .rkt, .rktl, .rktd | `#\|...\|#` block comments, match |
| 97 | Scheme | FULL | .scm, .ss, .sls, .sld | R7RS compatible |
| 98 | Common Lisp | FULL | .lisp, .lsp, .cl, .fas, .fasl | CLOS, loop macro, conditions |
| 99 | Standard ML | FULL | .sml, .sig | Functors, signatures, modules |
| 100 | PureScript | FULL | .purs | Haskell-like for JS compilation |
| 101 | Idris | FULL | .idr, .lidr, .ipkg | Dependent types, totality checking |
| 102 | Lean 4 | FULL | .lean | Proof assistant + programming language |
| 103 | Agda | FULL | .agda, .lagda | Dependently typed proof assistant |
| 104 | SPARQL | FULL | .sparql, .rq | RDF query language |
| 105 | WebAssembly Text | FULL | .wat, .wast | WAT/WAST text format |
| 106 | Dhall | FULL | .dhall | Typed configuration language |
| 107 | CUE | FULL | .cue | Data constraint language |
| 108 | KDL | FULL | .kdl | Document language (kcl/kdl) |
| 109 | Janet | FULL | .janet | Lisp-inspired scripting language |
| 110 | Fennel | FULL | .fnl | Lua-targeting Lisp dialect |
| 111 | Haxe | FULL | .hx, .hxml | Multi-target compiled language |
| 112 | Wren | FULL | .wren | Scripting language for game engines |
| 113 | LaTeX | FULL | .tex, .sty, .cls, .bib, .dtx, .ins | Document typesetting |
| 114 | reStructuredText | FULL | .rst, .rest | Sphinx documentation markup |
| 115 | AsciiDoc | FULL | .adoc, .asc, .asciidoc | Technical documentation markup |
| 116 | Puppet | FULL | .epp + **regex** `manifests/*.pp` | Infrastructure-as-code DSL |
| 117 | Wolfram Language | FULL | .wl, .nb, .wlt | Mathematica / Wolfram Engine |
| 118 | ActionScript | FULL | .as, .mxml | Flash/AIR and Flex framework |
| 119 | WebIDL | FULL | .webidl, .idl | Web API interface definitions |

## Extending Beyond Built-in Languages

Place additional `.ini` files under `~/.config/nanox/langs` (or `~/.local/share/nanox/langs`). Every file can define one or more `[language]` sections using the same keys as the core profiles. Nanox merges these after loading the defaults, so you can override a bundled language or add niche languages such as Ada, COBOL, Erlang, Elixir, or Fortran.

Example (`~/.config/nanox/langs/erlang.ini`):

```ini
[erlang]
extensions = erl,hrl
line_comment_tokens = %
string_delims = ",'
flow = if,of,case,receive,after,fun,try,catch,throw,begin,end
keywords = module,export,import,record,when,spawn,let,apply
return_keywords = return
```

Reference profiles for Ada, COBOL, Elixir, Erlang, and Fortran ship under `configs/nanox/langs` to serve as templates.
