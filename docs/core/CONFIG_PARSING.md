# INI Configuration Parsing in NanoX

NanoX uses a simple INI-style configuration format for both global settings and syntax highlighting rules. This document explains how the parsing is implemented and how to add new configuration options.

## 1. Overview of INI Format

The configuration files follow this general structure:

```ini
[section]
key = value
# comment
; another comment
```

## 2. Parsing Mechanism

The parsing logic is primarily found in `nanox.c` (for UI/Editor settings) and `highlight.c` (for syntax highlighting).

### Core Functions

- **`trim(char *s)`**: Removes leading and trailing whitespace from a string. Essential for handling `key = value` spaces.
- **`parse_config_file()` / `load_config_file()`**: Opens the file and reads it line by line using `fgets`.
- **`parse_config_line()`**: 
  1. Identifies sections by checking if a line starts with `[`.
  2. Splits lines by the `=` character to separate keys and values.
  3. Discards comments starting with `#` or `;`.

### Example Implementation (`nanox.c`)

```c
static void parse_config_line(const char *section, char *line) {
    char *equals = strchr(line, '=');
    if (!equals) return;
    *equals = 0;
    char *key = trim(line);
    char *value = trim(equals + 1);

    if (strcasecmp(section, "ui") == 0)
        parse_ui_option(key, value);
    // ...
}
```

## 3. Configuration Locations

NanoX searches for configuration files in the following order:
1. User config directory (typically `~/.config/nanox/`)
2. User data directory (typically `~/.local/share/nanox/`)
3. Relative path `configs/nanox/` from the binary location.

## 4. Syntax Highlighting INIs (`highlight.c`)

Syntax rules are more complex and involve lists of keywords. `highlight.c` uses `strtok(val, ",")` to parse comma-separated lists for:
- `extensions`: File extensions mapped to the profile.
- `keywords`: Language-specific reserved words.
- `line_comment_tokens`: Tokens like `//` or `#`.
- `block_comment_pairs`: Start and end tokens for blocks (e.g., `/* */`).

## 5. Adding New Options

To add a new configuration option:
1. Define the variable in the appropriate header (e.g., `struct nanox_config` in `nanox.h`).
2. Update the `parse_*_option` function in `nanox.c` or `highlight.c` to recognize the new key.
3. Use `parse_bool` or `atoi` to convert the string value to the appropriate type.
4. Ensure the default value is set in `config_defaults()`.
