# Language Support Matrix

Nanox now ships with **32 built-in syntax profiles**. Each profile defines comment delimiters, flow/type/keyword sets, and highlighting toggles for triple quotes, brackets, and numerics. All built-ins are loaded from `syntax.ini`, and you can add more via the per-user `langs/` directory.

| # | Language | Support | Extensions | Notes |
|---|----------|---------|------------|-------|
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
| 14 | TypeScript | FULL | .ts, .tsx | Types, namespaces, and declaration syntax |
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
| 31 | Groovy/Gradle | FULL | .groovy, .gradle | DSL keywords |
| 32 | Haskell | FULL | .hs, .lhs | `{- -}` block comments |

## Extending Beyond 32 Languages

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
