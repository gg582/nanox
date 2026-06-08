#ifndef LSP_CLIENT_H
#define LSP_CLIENT_H

struct cJSON;

void lsp_init(void);
void lsp_shutdown(void);

extern char lsp_open_uri[512];

/* start server matching current buffer; returns 1 if ready */
int lsp_ensure_server(void);

/* notify server with full document text */
void lsp_notify_didopen(const char *uri, const char *langid, const char *text);
void lsp_notify_didchange(const char *uri, const char *text);
void lsp_notify_didclose(const char *uri);

/* request completion; returns reqid or -1 */
int lsp_request_completion(const char *uri, int line, int col);

/* poll for a completion response.
 *  1 = got it, *result is set (caller must cJSON_Delete)
 *  0 = pending
 * -1 = error / not found */
int lsp_poll_completion(int reqid, struct cJSON **result);

#endif
