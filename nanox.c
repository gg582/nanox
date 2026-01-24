/*
 * nanox.c
 *
 * Nano-inspired UI helpers for MicroEmacs (nanox layer)
 */

#include "nanox.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "util.h"
#include "version.h"

struct nanox_config nanox_cfg = {
	.hint_bar = true,
	.warning_lamp = true,
	.warning_format = "--W",
	.error_format = "--E",
	.help_key = SPEC | 'P',
	.soft_tab = true,
	.soft_tab_width = 4,
	.case_sensitive_default = false,
};

char file_reserve[4][PATH_MAX];

static enum nanox_lamp_state lamp_state = NANOX_LAMP_OFF;

struct nanox_help_topic {
	const char *title;
	const char *const *lines;
	size_t line_count;
};

static bool help_active;
static int help_selected;
static int help_scroll;
static bool help_show_section;

static const char *const help_keys[] = {
	"Alt+O  open file",
	"Alt+S  save file",
	"Alt+Q  quit editor",
	"Alt+W  search forward",
	"Alt+H  replace text",
	"Alt+G  goto line",
	"Alt+K  cut line",
	"Alt+U  paste",
	"F2 Save / F3 Open / F4 Quit",
	"F5 Search / F6 Replace",
	"F7 Cut line / F8 Paste",
	"F9..F12 reservation slots",
};

static const char *const help_file_mode[] = {
	"File Mode always speaks about files.",
	"Buffers exist internally but are hidden.",
	"Lists, switches, and status text say File.",
	"Messages use the [File Mode] prefix.",
};

static const char *const help_slots[] = {
	"Reserve slots with Ctrl+F9..F12 or Alt+9/0/-/=",
	"Jump with F9..F12. Paths are stored globally.",
	"Empty jumps raise a warning lamp.",
	"Messages show which slot is in use.",
};

static const char *const help_config[] = {
	"Config path: ~/.local/share/nanox/config",
	"[ui] hint_bar=true|false",
	"[ui] warning_lamp=true|false",
	"[ui] warning_format=--W",
	"[ui] error_format=--E",
	"[ui] help_key=F1",
	"[edit] soft_tab=true|false",
	"[edit] soft_tab_width=4",
	"[search] case_sensitive_default=false",
};

static const char *const help_build[] = {
	"Engine: MicroEmacs/uEmacs core",
	"Renderer: termcap based",
	"Spell: hunspell (if available)",
	"UTF-8 aware editing",
	"Nano-style UI layer (nanox)",
};

static const struct nanox_help_topic help_topics[] = {
	{ "Key bindings", help_keys, ARRAY_SIZE(help_keys) },
	{ "File Mode concept", help_file_mode, ARRAY_SIZE(help_file_mode) },
	{ "File reservation slots", help_slots, ARRAY_SIZE(help_slots) },
	{ "Configuration options", help_config, ARRAY_SIZE(help_config) },
	{ "Build features", help_build, ARRAY_SIZE(help_build) },
};

static void config_defaults(void)
{
	nanox_cfg.hint_bar = true;
	nanox_cfg.warning_lamp = true;
	mystrscpy(nanox_cfg.warning_format, "--W", sizeof(nanox_cfg.warning_format));
	mystrscpy(nanox_cfg.error_format, "--E", sizeof(nanox_cfg.error_format));
	nanox_cfg.help_key = SPEC | 'P';
	nanox_cfg.soft_tab = true;
	nanox_cfg.soft_tab_width = 4;
	nanox_cfg.case_sensitive_default = false;
}

static bool parse_bool(const char *value, bool *out)
{
	if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 ||
	    strcmp(value, "1") == 0) {
		*out = true;
		return true;
	}
	if (strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 ||
	    strcmp(value, "0") == 0) {
		*out = false;
		return true;
	}
	return false;
}

static void mark_config_error(void)
{
	nanox_set_lamp(NANOX_LAMP_ERROR);
}

static void parse_ui_option(const char *key, const char *value)
{
	if (strcasecmp(key, "hint_bar") == 0) {
		if (!parse_bool(value, &nanox_cfg.hint_bar))
			mark_config_error();
	} else if (strcasecmp(key, "warning_lamp") == 0) {
		if (!parse_bool(value, &nanox_cfg.warning_lamp))
			mark_config_error();
	} else if (strcasecmp(key, "warning_format") == 0) {
		mystrscpy(nanox_cfg.warning_format, value, sizeof(nanox_cfg.warning_format));
	} else if (strcasecmp(key, "error_format") == 0) {
		mystrscpy(nanox_cfg.error_format, value, sizeof(nanox_cfg.error_format));
	} else if (strcasecmp(key, "help_key") == 0) {
		if (strcasecmp(value, "F1") == 0)
			nanox_cfg.help_key = SPEC | 'P';
		else
			mark_config_error();
	}
}

static void parse_edit_option(const char *key, const char *value)
{
	if (strcasecmp(key, "soft_tab") == 0) {
		if (!parse_bool(value, &nanox_cfg.soft_tab))
			mark_config_error();
	} else if (strcasecmp(key, "soft_tab_width") == 0) {
		int width = atoi(value);
		if (width <= 0)
			mark_config_error();
		else
			nanox_cfg.soft_tab_width = width;
	}
}

static void parse_search_option(const char *key, const char *value)
{
	if (strcasecmp(key, "case_sensitive_default") == 0) {
		if (!parse_bool(value, &nanox_cfg.case_sensitive_default))
			mark_config_error();
	}
}

static void parse_config_line(const char *section, char *line)
{
	char *equals = strchr(line, '=');

	if (!equals)
		return;
	*equals = 0;
	char *key = line;
	char *value = equals + 1;
	while (*key && isspace((unsigned char)*key))
		key++;
	while (*value && isspace((unsigned char)*value))
		value++;

	char *end = value + strlen(value);
	while (end > value && isspace((unsigned char)*(end - 1)))
		*--end = 0;
	end = key + strlen(key);
	while (end > key && isspace((unsigned char)*(end - 1)))
		*--end = 0;

	if (strcasecmp(section, "ui") == 0)
		parse_ui_option(key, value);
	else if (strcasecmp(section, "edit") == 0)
		parse_edit_option(key, value);
	else if (strcasecmp(section, "search") == 0)
		parse_search_option(key, value);
}

static void parse_config_file(void)
{
	const char *home = getenv("HOME");
	char path[PATH_MAX];
	FILE *fp;
	char line[512];
	char section[32] = "";

	if (!home)
		return;
	snprintf(path, sizeof(path), "%s/.local/share/nanox/config", home);
	fp = fopen(path, "r");
	if (!fp)
		return;

	while (fgets(line, sizeof(line), fp)) {
		char *ptr = line;

		while (*ptr && isspace((unsigned char)*ptr))
			ptr++;
		if (*ptr == '#' || *ptr == ';' || *ptr == 0)
			continue;
		if (*ptr == '[') {
			char *close = strchr(ptr, ']');
			if (close) {
				*close = 0;
				mystrscpy(section, ptr + 1, sizeof(section));
			}
			continue;
		}
		parse_config_line(section, ptr);
	}
	fclose(fp);
}

void nanox_apply_config(void)
{
	if (nanox_cfg.soft_tab)
		tabsize = nanox_cfg.soft_tab_width;
	else
		tabsize = 0;

	if (nanox_cfg.case_sensitive_default)
		gmode |= MDEXACT;
	else
		gmode &= ~MDEXACT;
}

void nanox_init(void)
{
	config_defaults();
	parse_config_file();
	nanox_apply_config();
}

void nanox_set_lamp(enum nanox_lamp_state state)
{
	if (!nanox_cfg.warning_lamp)
		return;
	if (state == NANOX_LAMP_OFF) {
		lamp_state = NANOX_LAMP_OFF;
		return;
	}
	if (state > lamp_state)
		lamp_state = state;
}

enum nanox_lamp_state nanox_current_lamp(void)
{
	if (!nanox_cfg.warning_lamp)
		return NANOX_LAMP_OFF;
	return lamp_state;
}

const char *nanox_lamp_label(void)
{
	switch (nanox_current_lamp()) {
	case NANOX_LAMP_WARN:
		return nanox_cfg.warning_format;
	case NANOX_LAMP_ERROR:
		return nanox_cfg.error_format;
	default:
		return "";
	}
}

int nanox_text_rows(void)
{
	int rows = term.t_nrow - 2;
	if (rows < 1)
		rows = 1;
	return rows;
}

int nanox_hint_top_row(void)
{
	int row = term.t_nrow - 2;
	return row < 0 ? 0 : row;
}

int nanox_hint_bottom_row(void)
{
	int row = term.t_nrow - 1;
	return row < 0 ? 0 : row;
}

static void replace_all(char *buffer, size_t bufsz, const char *needle, const char *replacement)
{
	char tmp[1024];
	size_t needle_len = strlen(needle);
	size_t repl_len = strlen(replacement);
	size_t tmp_size = sizeof(tmp);

	if (!needle_len)
		return;

	while (1) {
		char *pos = strstr(buffer, needle);
		size_t prefix_len, suffix_len, total;

		if (!pos)
			break;
		prefix_len = pos - buffer;
		suffix_len = strlen(pos + needle_len);
		total = prefix_len + repl_len + suffix_len;
		if (total + 1 > tmp_size)
			break;
		memcpy(tmp, buffer, prefix_len);
		memcpy(tmp + prefix_len, replacement, repl_len);
		memcpy(tmp + prefix_len + repl_len, pos + needle_len, suffix_len + 1);
		mystrscpy(buffer, tmp, bufsz);
	}
}

void nanox_message_prefix(const char *input, char *output, size_t outsz)
{
	char temp[1024];

	mystrscpy(temp, input ? input : "", sizeof(temp));
	replace_all(temp, sizeof(temp), "Buffer List", "File List");
	replace_all(temp, sizeof(temp), "buffer list", "file list");
	replace_all(temp, sizeof(temp), "Switch Buffer", "Switch File");
	replace_all(temp, sizeof(temp), "switch buffer", "switch file");
	replace_all(temp, sizeof(temp), "Buffer", "File");
	replace_all(temp, sizeof(temp), "buffer", "file");
	replace_all(temp, sizeof(temp), "BUFFER", "FILE");

	if (!*temp) {
		mystrscpy(output, temp, outsz);
		return;
	}

	if (strncmp(temp, "[File Mode]", 11) == 0)
		mystrscpy(output, temp, outsz);
	else
		snprintf(output, outsz, "[File Mode] %s", temp);
}

void nanox_notify_message(const char *text)
{
	if (!text || !*text)
		nanox_set_lamp(NANOX_LAMP_OFF);
}

static void help_puts(const char *text)
{
	while (*text)
		ttputc(*text++);
}

static void help_blank_line(int row)
{
	int col;

	movecursor(row, 0);
	for (col = 0; col < term.t_ncol; ++col)
		ttputc(' ');
}

void nanox_help_render(void)
{
	int row;
	const size_t topics = ARRAY_SIZE(help_topics);

	for (row = 0; row <= term.t_nrow; ++row)
		help_blank_line(row);

	movecursor(0, 0);
	help_puts("nanox help - i: Up, k: Down, Enter open, Backspace back");

	if (!help_show_section) {
		for (size_t i = 0; i < topics && (int)i + 2 < nanox_hint_top_row(); ++i) {
			row = 2 + (int)i;
			movecursor(row, 0);
			if ((int)i == help_selected)
				help_puts(" > ");
			else
				help_puts("   ");
			help_puts(help_topics[i].title);
		}
	} else {
		const struct nanox_help_topic *topic = &help_topics[help_selected];
		int r = 2;
		movecursor(1, 0);
		help_puts(topic->title);
		for (size_t i = help_scroll; i < topic->line_count && r < nanox_hint_top_row(); ++i, ++r) {
			movecursor(r, 0);
			help_puts(topic->lines[i]);
		}
	}
	ttflush();
}

bool nanox_help_is_active(void)
{
	return help_active;
}

int nanox_help_command(int f, int n)
{
	help_active = true;
	help_selected = 0;
	help_scroll = 0;
	help_show_section = false;
	nanox_help_render();
	return TRUE;
}

static void help_close(void)
{
	help_active = false;
	sgarbf = TRUE;
}

int nanox_help_handle_key(int key)
{
	if (!help_active)
		return FALSE;

	switch (key) {
	case CONTROL | 'G':
	case CONTROL | '[':
	case 27:
		help_close();
		return TRUE;
	case CONTROL | 'M':
	case '\r':
		help_show_section = true;
		help_scroll = 0;
		break;
	case 0x7F:
	case CONTROL | 'H':
		if (help_show_section)
			help_show_section = false;
		else
			help_close();
		break;
	case 'i':
	case 'I':
		if (!help_show_section && help_selected > 0)
			--help_selected;
		else if (help_show_section && help_scroll > 0)
			--help_scroll;
		break;
	case 'k':
	case 'K': {
		if (!help_show_section) {
			if (help_selected < (int)ARRAY_SIZE(help_topics) - 1)
				++help_selected;
		} else {
			const struct nanox_help_topic *topic = &help_topics[help_selected];
			if (help_scroll + (nanox_hint_top_row() - 2) < (int)topic->line_count)
				++help_scroll;
		}
		break;
	}
	default:
		break;
	}
	nanox_help_render();
	return TRUE;
}

static const char *slot_name(int slot)
{
	static const char *names[] = { "F9", "F10", "F11", "F12" };
	if (slot < 0 || slot >= 4)
		return "?";
	return names[slot];
}

static int reserve_set(int slot)
{
	char prompt[64];
	char path[PATH_MAX];
	int rc;

	snprintf(prompt, sizeof(prompt), "Reserve %s file: ", slot_name(slot));
	rc = mlreply(prompt, path, sizeof(path));
	if (rc != TRUE)
		return rc;
	if (!*path)
		return FALSE;
	mystrscpy(file_reserve[slot], path, sizeof(file_reserve[slot]));
	mlwrite("Reserved %s = %s", slot_name(slot), path);
	return TRUE;
}

static int reserve_jump(int slot)
{
	int rc;

	if (!file_reserve[slot][0]) {
		nanox_set_lamp(NANOX_LAMP_WARN);
		mlwrite("%s is empty (use Ctrl+%s to reserve)",
			slot_name(slot), slot_name(slot));
		return FALSE;
	}
	rc = getfile(file_reserve[slot], TRUE);
	if (rc == TRUE) {
		mlwrite("Jump %s -> %s", slot_name(slot), file_reserve[slot]);
		nanox_set_lamp(NANOX_LAMP_OFF);
	} else {
		nanox_set_lamp(NANOX_LAMP_ERROR);
	}
	return rc;
}

int reserve_set_1(int f, int n) { return reserve_set(0); }
int reserve_set_2(int f, int n) { return reserve_set(1); }
int reserve_set_3(int f, int n) { return reserve_set(2); }
int reserve_set_4(int f, int n) { return reserve_set(3); }

int reserve_jump_1(int f, int n) { return reserve_jump(0); }
int reserve_jump_2(int f, int n) { return reserve_jump(1); }
int reserve_jump_3(int f, int n) { return reserve_jump(2); }
int reserve_jump_4(int f, int n) { return reserve_jump(3); }
