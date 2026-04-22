#include "render_plugin.h"
#include <stdlib.h>
#include <string.h>

#define MAX_PLUGINS 32

static render_plugin_t plugins[MAX_PLUGINS];
static int plugin_count = 0;

void render_plugin_register(render_plugin_t plugin) {
    if (plugin_count < MAX_PLUGINS) {
        plugins[plugin_count++] = plugin;
    }
}

void render_plugin_execute(render_hook_type_t type, render_ctx_t *ctx) {
    for (int i = 0; i < plugin_count; i++) {
        if (plugins[i].type == type) {
            plugins[i].fn(ctx);
        }
    }
}
