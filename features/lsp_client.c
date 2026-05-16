#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>

#include "estruct.h"
#include "cJSON.h"
#include "lsp_client.h"
#include "edef.h"
#include "nanox.h"

#define LSP_RBUF_SZ 65536

typedef struct lspmsg {
	int reqid;
	cJSON *result;
	struct lspmsg *next;
} lspmsg_t;

static struct {
	int active;
	pid_t pid;
	int wfd;
	int rfd;
	int reqid;
	char *cmd;
	pthread_t rthr;
	pthread_mutex_t lock;
	lspmsg_t *msgs;
	char rbuf[LSP_RBUF_SZ];
	size_t rlen;
} srv;

/* --- json-rpc helpers --- */

static void
lsp_write_raw(const char *s)
{
	size_t n = strlen(s);
	write(srv.wfd, s, n);
}

static void
lsp_send_json(cJSON *o)
{
	char *j = cJSON_PrintUnformatted(o);
	if (!j) { cJSON_Delete(o); return; }
	char hdr[64];
	snprintf(hdr, sizeof(hdr), "Content-Length: %zu\r\n\r\n", strlen(j));
	lsp_write_raw(hdr);
	lsp_write_raw(j);
	free(j);
	cJSON_Delete(o);
}

static cJSON *
mkreq(const char *method, cJSON *params, int id)
{
	cJSON *o = cJSON_CreateObject();
	cJSON_AddStringToObject(o, "jsonrpc", "2.0");
	cJSON_AddStringToObject(o, "method", method);
	cJSON_AddItemToObject(o, "params", params ? params : cJSON_CreateObject());
	if (id >= 0)
		cJSON_AddNumberToObject(o, "id", id);
	return o;
}

/* --- reader thread --- */

static int
read_header(size_t *bodylen)
{
	for (;;) {
		char *end = memmem(srv.rbuf, srv.rlen, "\r\n\r\n", 4);
		if (end) {
			size_t hlen = (size_t)(end + 4 - srv.rbuf);
			char *p = memmem(srv.rbuf, hlen, "Content-Length:", 15);
			if (!p) return -1;
			p += 15;
			while (*p == ' ') p++;
			*bodylen = (size_t)strtoul(p, NULL, 10);
			srv.rlen -= hlen;
			memmove(srv.rbuf, srv.rbuf + hlen, srv.rlen);
			return 0;
		}
		if (srv.rlen >= LSP_RBUF_SZ - 1) return -1;
		ssize_t n = read(srv.rfd, srv.rbuf + srv.rlen, LSP_RBUF_SZ - srv.rlen - 1);
		if (n <= 0) return -1;
		srv.rlen += (size_t)n;
		srv.rbuf[srv.rlen] = '\0';
	}
}

static int
read_body(size_t want)
{
	while (srv.rlen < want) {
		ssize_t n = read(srv.rfd, srv.rbuf + srv.rlen, LSP_RBUF_SZ - srv.rlen - 1);
		if (n <= 0) return -1;
		srv.rlen += (size_t)n;
	}
	return 0;
}

static void *
lsp_reader(void *arg)
{
	(void)arg;
	while (srv.active) {
		size_t want = 0;
		if (read_header(&want) < 0) break;
		if (read_body(want) < 0) break;

		srv.rbuf[want] = '\0';
		cJSON *msg = cJSON_Parse(srv.rbuf);
		if (msg) {
			cJSON *idj = cJSON_GetObjectItemCaseSensitive(msg, "id");
			cJSON *res = cJSON_GetObjectItemCaseSensitive(msg, "result");
			if (idj && cJSON_IsNumber(idj) && res) {
				lspmsg_t *m = malloc(sizeof(lspmsg_t));
				if (m) {
					m->reqid = idj->valueint;
					m->result = cJSON_Duplicate(res, 1);
					m->next = NULL;
					pthread_mutex_lock(&srv.lock);
					if (!srv.msgs) srv.msgs = m;
					else {
						lspmsg_t *t = srv.msgs;
						while (t->next) t = t->next;
						t->next = m;
					}
					pthread_mutex_unlock(&srv.lock);
				}
			}
			cJSON_Delete(msg);
		}
		srv.rlen -= want;
		memmove(srv.rbuf, srv.rbuf + want, srv.rlen);
	}
	srv.active = 0;
	return NULL;
}

/* --- process mgmt --- */

static const char *
lsp_cmd_for_buf(void)
{
	static const struct { const char *ext; const char *cmd; } m[] = {
		{".c","clangd"}, {".h","clangd"}, {".cpp","clangd"}, {".hpp","clangd"},
		{".cc","clangd"}, {".cxx","clangd"}, {".m","clangd"}, {".mm","clangd"},
		{".py","pylsp"}, {".py","pyright-langserver"},
		{".js","typescript-language-server"}, {".jsx","typescript-language-server"},
		{".ts","typescript-language-server"}, {".tsx","typescript-language-server"},
		{".go","gopls"}, {".rs","rust-analyzer"}, {".java","jdtls"}
	};
	if (!curbp || !curbp->b_fname[0]) return NULL;
	size_t fl = strlen(curbp->b_fname);
	for (size_t i = 0; i < sizeof(m)/sizeof(m[0]); i++) {
		size_t el = strlen(m[i].ext);
		if (fl >= el && strcasecmp(curbp->b_fname + fl - el, m[i].ext) == 0)
			return m[i].cmd;
	}
	return NULL;
}

void
lsp_init(void)
{
	memset(&srv, 0, sizeof(srv));
	srv.wfd = -1;
	srv.rfd = -1;
	pthread_mutex_init(&srv.lock, NULL);
}

void
lsp_shutdown(void)
{
	if (!srv.active) return;
	srv.active = 0;

	if (srv.wfd >= 0) {
		lsp_send_json(mkreq("shutdown", NULL, -1));
		close(srv.wfd);
		srv.wfd = -1;
	}
	pthread_join(srv.rthr, NULL);
	if (srv.rfd >= 0) { close(srv.rfd); srv.rfd = -1; }

	if (srv.pid > 0) {
		kill(srv.pid, SIGTERM);
		waitpid(srv.pid, NULL, 0);
		srv.pid = 0;
	}
	free(srv.cmd); srv.cmd = NULL;

	pthread_mutex_lock(&srv.lock);
	lspmsg_t *m = srv.msgs;
	while (m) {
		lspmsg_t *n = m->next;
		cJSON_Delete(m->result);
		free(m);
		m = n;
	}
	srv.msgs = NULL;
	pthread_mutex_unlock(&srv.lock);
}

int
lsp_ensure_server(void)
{
	if (!nanox_cfg.use_lsp) return 0;
	const char *cmd = lsp_cmd_for_buf();
	if (!cmd) return 0;
	if (srv.active && srv.cmd && strcmp(srv.cmd, cmd) == 0) return 1;

	lsp_shutdown();

	int p2c[2], c2p[2];
	if (pipe(p2c) < 0 || pipe(c2p) < 0) return 0;
	pid_t pid = fork();
	if (pid < 0) return 0;
	if (pid == 0) {
		close(p2c[1]); close(c2p[0]);
		dup2(p2c[0], STDIN_FILENO);
		dup2(c2p[1], STDOUT_FILENO);
		close(p2c[0]); close(c2p[1]);
		execlp(cmd, cmd, "--stdio", NULL);
		_exit(127);
	}
	close(p2c[0]); close(c2p[1]);
	srv.pid = pid;
	srv.wfd = p2c[1];
	srv.rfd = c2p[0];
	srv.active = 1;
	srv.reqid = 1;
	srv.cmd = strdup(cmd);

	cJSON *p = cJSON_CreateObject();
	cJSON_AddNumberToObject(p, "processId", getpid());
	cJSON *caps = cJSON_CreateObject();
	cJSON_AddItemToObject(p, "capabilities", caps);
	cJSON *ws = cJSON_CreateObject();
	cJSON_AddItemToObject(ws, "workspaceFolders", cJSON_CreateArray());
	cJSON_AddItemToObject(p, "workspace", ws);
	lsp_send_json(mkreq("initialize", p, 0));

	usleep(150000);
	lsp_send_json(mkreq("initialized", NULL, -1));

	pthread_create(&srv.rthr, NULL, lsp_reader, NULL);
	return 1;
}

/* --- notifications --- */

void
lsp_notify_didopen(const char *uri, const char *langid, const char *text)
{
	if (!srv.active) return;
	cJSON *p = cJSON_CreateObject();
	cJSON *td = cJSON_CreateObject();
	cJSON_AddStringToObject(td, "uri", uri);
	cJSON_AddStringToObject(td, "languageId", langid ? langid : "");
	cJSON_AddNumberToObject(td, "version", 1);
	cJSON_AddStringToObject(td, "text", text ? text : "");
	cJSON_AddItemToObject(p, "textDocument", td);
	lsp_send_json(mkreq("textDocument/didOpen", p, -1));
}

void
lsp_notify_didchange(const char *uri, const char *text)
{
	if (!srv.active) return;
	cJSON *p = cJSON_CreateObject();
	cJSON *td = cJSON_CreateObject();
	cJSON_AddStringToObject(td, "uri", uri);
	cJSON_AddNumberToObject(td, "version", 2);
	cJSON_AddItemToObject(p, "textDocument", td);

	cJSON *ch = cJSON_CreateArray();
	cJSON *it = cJSON_CreateObject();
	cJSON *rng = cJSON_CreateObject();
	cJSON *st = cJSON_CreateObject(); cJSON_AddNumberToObject(st, "line", 0); cJSON_AddNumberToObject(st, "character", 0);
	cJSON *ed = cJSON_CreateObject(); cJSON_AddNumberToObject(ed, "line", 999999); cJSON_AddNumberToObject(ed, "character", 999999);
	cJSON_AddItemToObject(rng, "start", st);
	cJSON_AddItemToObject(rng, "end", ed);
	cJSON_AddItemToObject(it, "range", rng);
	cJSON_AddStringToObject(it, "text", text ? text : "");
	cJSON_AddItemToArray(ch, it);
	cJSON_AddItemToObject(p, "contentChanges", ch);

	lsp_send_json(mkreq("textDocument/didChange", p, -1));
}

void
lsp_notify_didclose(const char *uri)
{
	if (!srv.active) return;
	cJSON *p = cJSON_CreateObject();
	cJSON *td = cJSON_CreateObject();
	cJSON_AddStringToObject(td, "uri", uri);
	cJSON_AddItemToObject(p, "textDocument", td);
	lsp_send_json(mkreq("textDocument/didClose", p, -1));
}

/* --- completion --- */

int
lsp_request_completion(const char *uri, int line, int col)
{
	if (!srv.active) return -1;
	int id = srv.reqid++;
	cJSON *p = cJSON_CreateObject();
	cJSON *td = cJSON_CreateObject();
	cJSON_AddStringToObject(td, "uri", uri);
	cJSON_AddItemToObject(p, "textDocument", td);
	cJSON *pos = cJSON_CreateObject();
	cJSON_AddNumberToObject(pos, "line", line);
	cJSON_AddNumberToObject(pos, "character", col);
	cJSON_AddItemToObject(p, "position", pos);
	lsp_send_json(mkreq("textDocument/completion", p, id));
	return id;
}

int
lsp_poll_completion(int reqid, cJSON **result)
{
	if (!srv.active) return -1;
	pthread_mutex_lock(&srv.lock);
	lspmsg_t **pp = &srv.msgs;
	while (*pp) {
		if ((*pp)->reqid == reqid) {
			lspmsg_t *m = *pp;
			*pp = m->next;
			pthread_mutex_unlock(&srv.lock);
			*result = m->result;
			free(m);
			return 1;
		}
		pp = &(*pp)->next;
	}
	pthread_mutex_unlock(&srv.lock);
	return 0;
}
