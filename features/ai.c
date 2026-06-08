#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>

#include "nanox.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "util.h"
#include "cJSON.h"

// Parse URL: http://host:port/path
static bool parse_url(const char *url, char *host, int *port, char *path)
{
    *port = 80;
    strcpy(path, "/");

    const char *p = url;
    if (strncasecmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncasecmp(p, "https://", 8) == 0) {
        p += 8;
        *port = 443;
    }

    const char *slash = strchr(p, '/');
    char host_port[256];
    if (slash) {
        size_t len = (size_t)(slash - p);
        if (len >= sizeof(host_port)) len = sizeof(host_port) - 1;
        memcpy(host_port, p, len);
        host_port[len] = '\0';
        mystrscpy(path, slash, 256);
    } else {
        mystrscpy(host_port, p, sizeof(host_port));
    }

    char *colon = strchr(host_port, ':');
    if (colon) {
        *colon = '\0';
        *port = atoi(colon + 1);
    }
    mystrscpy(host, host_port, 256);
    return true;
}

static int http_post(const char *url, const char *json_body, char *response_buf, size_t response_cap)
{
    char host[256];
    int port;
    char path[256];
    if (!parse_url(url, host, &port, path)) {
        return -1;
    }

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        return -1;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        return -1;
    }

    struct timeval timeout;
    timeout.tv_sec = 8; // 8 seconds timeout for AI generation
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    char req_hdr[1024];
    snprintf(req_hdr, sizeof(req_hdr),
             "POST %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n",
             path, host, port, strlen(json_body));

    if (send(sock, req_hdr, strlen(req_hdr), 0) < 0 ||
        send(sock, json_body, strlen(json_body), 0) < 0) {
        close(sock);
        return -1;
    }

    size_t total_received = 0;
    while (total_received < response_cap - 1) {
        ssize_t n = recv(sock, response_buf + total_received, response_cap - 1 - total_received, 0);
        if (n < 0) {
            close(sock);
            return -1;
        }
        if (n == 0) {
            break;
        }
        total_received += (size_t)n;
    }
    response_buf[total_received] = '\0';
    close(sock);
    return 0;
}

static void get_ai_context(char *buf, size_t max_size)
{
    buf[0] = '\0';

    struct line *start_line = curwp->w_dotp;
    int count = 0;
    while (lback(start_line) != curbp->b_linep && count < 30) {
        start_line = lback(start_line);
        count++;
    }

    size_t len = 0;
    struct line *lp = start_line;
    while (lp != curwp->w_dotp) {
        int u = llength(lp);
        if (len + (size_t)u + 2 < max_size) {
            memcpy(buf + len, ltext(lp), (size_t)u);
            len += (size_t)u;
            buf[len++] = '\n';
        }
        lp = lforw(lp);
    }

    int u = curwp->w_doto;
    if (u > llength(curwp->w_dotp)) u = llength(curwp->w_dotp);
    if (len + (size_t)u + 1 < max_size) {
        memcpy(buf + len, ltext(curwp->w_dotp), (size_t)u);
        len += (size_t)u;
    }
    buf[len] = '\0';
}

int ai_complete(int f, int n)
{
    (void)f;
    (void)n;

    if (!nanox_cfg.ai_enabled) {
        mlwrite("AI Copilot is disabled in config. Enable it under [ai] section.");
        return FALSE;
    }

    char prompt_buf[4096];
    get_ai_context(prompt_buf, sizeof(prompt_buf));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", nanox_cfg.ai_model);
    cJSON_AddStringToObject(root, "prompt", prompt_buf);
    cJSON_AddBoolToObject(root, "stream", false);

    cJSON *options = cJSON_CreateObject();
    cJSON_AddNumberToObject(options, "num_predict", 128);
    cJSON_AddNumberToObject(options, "temperature", nanox_cfg.ai_temperature);
    cJSON_AddItemToObject(root, "options", options);

    cJSON_AddStringToObject(root, "system", "You are a code completion copilot. Complete the given code suffix based on the prefix. Output ONLY the completed code. Do not include markdown code block formatting or explanations.");

    char *json_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    mlwrite("AI: Querying Ollama...");
    sgarbf = TRUE;
    update(TRUE);

    char *response_buf = malloc(65536);
    if (!response_buf) {
        free(json_body);
        mlwrite("AI Error: Out of memory.");
        return FALSE;
    }
    memset(response_buf, 0, 65536);

    int res = http_post(nanox_cfg.ai_endpoint, json_body, response_buf, 65536);
    free(json_body);

    if (res < 0) {
        mlwrite("AI Error: Failed to connect or receive from Ollama.");
        free(response_buf);
        return FALSE;
    }

    char *body = strstr(response_buf, "\r\n\r\n");
    if (body) {
        body += 4;
    } else {
        body = response_buf;
    }

    cJSON *resp_root = cJSON_Parse(body);
    if (!resp_root) {
        mlwrite("AI Error: Failed to parse response JSON.");
        free(response_buf);
        return FALSE;
    }

    cJSON *resp_item = cJSON_GetObjectItem(resp_root, "response");
    if (!resp_item || !cJSON_IsString(resp_item)) {
        mlwrite("AI Error: Missing response text.");
        cJSON_Delete(resp_root);
        free(response_buf);
        return FALSE;
    }

    char *suggestion = resp_item->valuestring;
    if (!suggestion || !*suggestion) {
        mlwrite("AI: No suggestion returned.");
        cJSON_Delete(resp_root);
        free(response_buf);
        return FALSE;
    }

    // Normalize \r\n to \n in suggestion
    char *normalized = malloc(strlen(suggestion) + 1);
    if (!normalized) {
        cJSON_Delete(resp_root);
        free(response_buf);
        return FALSE;
    }
    char *src = suggestion;
    char *dst = normalized;
    while (*src) {
        if (*src == '\r' && *(src + 1) == '\n') {
            *dst++ = '\n';
            src += 2;
        } else if (*src == '\r') {
            *dst++ = '\n';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';

    struct line *orig_line = curwp->w_dotp;
    int orig_offset = curwp->w_doto;

    // Insert suggestion
    int insert_status = linstr(normalized);
    if (insert_status != TRUE) {
        mlwrite("AI Error: Failed to insert suggestion.");
        free(normalized);
        cJSON_Delete(resp_root);
        free(response_buf);
        return FALSE;
    }

    sgarbf = TRUE;
    update(TRUE);

    // Prompt user for confirmation/rejection
    mlwrite("AI Copilot Suggestion. Accept? (y/n): ");
    int key = getcmd();

    if (key == 'y' || key == 'Y' || key == '\r' || key == '\n' || key == (CONTROL | 'M')) {
        mlwrite("AI: Suggestion accepted.");
    } else {
        // Revert
        curwp->w_dotp = orig_line;
        curwp->w_doto = orig_offset;
        ldelete((long)strlen(normalized), FALSE);
        mlwrite("AI: Suggestion rejected.");
    }

    free(normalized);
    cJSON_Delete(resp_root);
    free(response_buf);

    sgarbf = TRUE;
    update(TRUE);
    return TRUE;
}
