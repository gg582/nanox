#include "scraper.h"

#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>

#define SCRAPER_MAX_SYMBOLS      512
#define SCRAPER_MAX_ENTRIES       64
#define SCRAPER_READ_LIMIT     16384
#define SCRAPER_CHILD_TIMEOUT_SEC 3

typedef struct {
    char *module;
    char **symbols;
    int count;
    int capacity;
    int ready;
    int in_progress;
    int failed;
} runtime_entry_t;

typedef struct {
    runtime_entry_t **entries;
    int count;
    int capacity;
} runtime_cache_t;

typedef struct {
    scraper_lang_t lang;
    runtime_entry_t *entry;
} scraper_job_t;

static runtime_cache_t caches[SCRAPER_LANG_COUNT];
static scraper_job_t *job_queue = NULL;
static int job_count = 0;
static int job_capacity = 0;
static pthread_mutex_t scraper_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t job_cond = PTHREAD_COND_INITIALIZER;
static pthread_t worker_thread;
static int worker_started = 0;

static const char python_script[] =
    "import importlib,sys\n"
    "mod=sys.argv[1]\n"
    "try:\n"
    "    m=importlib.import_module(mod)\n"
    "    for name in dir(m):\n"
    "        print(name)\n"
    "except Exception:\n"
    "    pass\n";

static const char node_script[] =
    "const mod=process.argv[1];\n"
    "try {\n"
    "  const m=require(mod);\n"
    "  const seen=new Set();\n"
    "  for (const k in m) {\n"
    "    if (!seen.has(k)) {\n"
    "      console.log(k);\n"
    "      seen.add(k);\n"
    "    }\n"
    "  }\n"
    "  if (m && typeof m === 'object' && m.default) {\n"
    "    console.log('default');\n"
    "  }\n"
    "} catch (e) {}\n";

static void free_entry(runtime_entry_t *entry)
{
    if (!entry)
        return;
    free(entry->module);
    entry->module = NULL;
    for (int i = 0; i < entry->count; i++)
        free(entry->symbols[i]);
    free(entry->symbols);
    entry->symbols = NULL;
    entry->count = 0;
    entry->capacity = 0;
    entry->ready = 0;
    entry->in_progress = 0;
    entry->failed = 0;
    free(entry);
}

static runtime_entry_t *find_entry(scraper_lang_t lang, const char *module)
{
    runtime_cache_t *cache = &caches[lang];
    for (int i = 0; i < cache->count; i++) {
        if (strcmp(cache->entries[i]->module, module) == 0)
            return cache->entries[i];
    }
    return NULL;
}

static void ensure_cache_capacity(runtime_cache_t *cache, int needed)
{
    if (cache->capacity >= needed)
        return;
    int new_capacity = cache->capacity ? cache->capacity * 2 : 8;
    if (new_capacity < needed)
        new_capacity = needed;
    runtime_entry_t **tmp = realloc(cache->entries, (size_t)new_capacity * sizeof(runtime_entry_t *));
    if (!tmp)
        return;
    cache->entries = tmp;
    cache->capacity = new_capacity;
}

static runtime_entry_t *create_entry(scraper_lang_t lang, const char *module)
{
    runtime_cache_t *cache = &caches[lang];
    if (cache->count >= SCRAPER_MAX_ENTRIES) {
        int removed = -1;
        for (int i = 0; i < cache->count; i++) {
            runtime_entry_t *candidate = cache->entries[i];
            if (candidate && !candidate->in_progress) {
                free_entry(candidate);
                if (i < cache->count - 1) {
                    memmove(&cache->entries[i], &cache->entries[i + 1],
                            (size_t)(cache->count - i - 1) * sizeof(runtime_entry_t *));
                }
                cache->count--;
                removed = 0;
                break;
            }
        }
        if (removed != 0)
            return NULL;
    }
    ensure_cache_capacity(cache, cache->count + 1);
    if (cache->capacity < cache->count + 1)
        return NULL;
    runtime_entry_t *entry = calloc(1, sizeof(*entry));
    if (!entry)
        return NULL;
    entry->module = strdup(module);
    if (!entry->module) {
        free(entry);
        return NULL;
    }
    cache->entries[cache->count++] = entry;
    return entry;
}

static void enqueue_job(scraper_lang_t lang, runtime_entry_t *entry)
{
    if (!entry)
        return;
    if (job_count == job_capacity) {
        int new_capacity = job_capacity ? job_capacity * 2 : 8;
        scraper_job_t *tmp = realloc(job_queue, (size_t)new_capacity * sizeof(scraper_job_t));
        if (!tmp)
            return;
        job_queue = tmp;
        job_capacity = new_capacity;
    }
    job_queue[job_count].lang = lang;
    job_queue[job_count].entry = entry;
    job_count++;
    pthread_cond_signal(&job_cond);
}

static int dequeue_job(scraper_job_t *out)
{
    if (job_count == 0)
        return 0;
    *out = job_queue[0];
    if (job_count > 1) {
        memmove(job_queue, job_queue + 1, (size_t)(job_count - 1) * sizeof(scraper_job_t));
    }
    job_count--;
    return 1;
}

static int run_child_process(const char *prog, char *const argv[],
                             char *output, size_t outsz)
{
    int pipefd[2];
    int child_done = 0;
    int stream_eof = 0;
    int timed_out = 0;
    time_t start_time;
    int status = 0;
    pid_t wait_rc;
    if (!output || outsz == 0)
        return -1;
    output[0] = '\0';
    if (pipe(pipefd) < 0)
        return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(prog, argv);
        _exit(127);
    }
    close(pipefd[1]);
    if (fcntl(pipefd[0], F_SETFL, O_NONBLOCK) < 0) {
        close(pipefd[0]);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }

    start_time = time(NULL);
    if (start_time == (time_t)-1) {
        close(pipefd[0]);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }
    size_t total = 0;
    while (!timed_out) {
        wait_rc = waitpid(pid, &status, WNOHANG);
        if (wait_rc == pid)
            child_done = 1;
        else if (wait_rc < 0) {
            if (errno == EINTR)
                continue;
            if (errno == ECHILD)
                child_done = 1;
            else
                break;
        }

        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(pipefd[0], &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int sel = select(pipefd[0] + 1, &rfds, NULL, NULL, &tv);
        if (sel > 0 && FD_ISSET(pipefd[0], &rfds)) {
            char discard[256];
            ssize_t nr;
            char *dst = discard;
            size_t room = sizeof(discard);
            if (total < outsz - 1) {
                dst = output + total;
                room = outsz - 1 - total;
            }
            nr = read(pipefd[0], dst, room);
            if (nr > 0) {
                if (total < outsz - 1)
                    total += (size_t)nr;
            } else if (nr == 0) {
                stream_eof = 1;
            } else if (errno != EAGAIN && errno != EINTR) {
                stream_eof = 1;
            }
        } else if (sel < 0 && errno != EINTR) {
            break;
        }

        if (child_done && stream_eof)
            break;

        time_t now = time(NULL);
        if (now == (time_t)-1 || now - start_time >= SCRAPER_CHILD_TIMEOUT_SEC) {
            timed_out = 1;
            break;
        }
    }

    output[total] = '\0';
    close(pipefd[0]);

    if (timed_out) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }
    if (!child_done)
        waitpid(pid, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -1;
    return (int)total;
}

static int run_language_command(scraper_lang_t lang, const char *module,
                                char *buffer, size_t bufsz)
{
    if (!module || !*module)
        return -1;
    if (lang == SCRAPER_LANG_PYTHON) {
        char *const argv[] = { "python3", "-c", (char *)python_script,
                               (char *)module, NULL };
        return run_child_process("python3", argv, buffer, bufsz);
    }
    if (lang == SCRAPER_LANG_NODE) {
        char *const argv[] = { "node", "-e", (char *)node_script,
                               (char *)module, NULL };
        return run_child_process("node", argv, buffer, bufsz);
    }
    return -1;
}

typedef struct {
    char **items;
    int count;
    int capacity;
} symbol_list_t;

static void symbol_list_add(symbol_list_t *list, const char *symbol)
{
    if (!symbol || !*symbol)
        return;
    if (list->count >= SCRAPER_MAX_SYMBOLS)
        return;
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->items[i], symbol) == 0)
            return;
    }
    if (list->count == list->capacity) {
        int new_capacity = list->capacity ? list->capacity * 2 : 32;
        char **tmp = realloc(list->items, (size_t)new_capacity * sizeof(char *));
        if (!tmp)
            return;
        list->items = tmp;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = strdup(symbol);
}

static void symbol_list_free(symbol_list_t *list)
{
    if (!list)
        return;
    for (int i = 0; i < list->count; i++)
        free(list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void collect_symbols_from_buffer(char *buffer, symbol_list_t *list)
{
    char *line = buffer;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = '\0';
        /* Trim whitespace */
        while (*line == ' ' || *line == '\t' || *line == '\r')
            line++;
        size_t len = strlen(line);
        while (len > 0 &&
               (line[len - 1] == ' ' || line[len - 1] == '\t' ||
                line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len > 0)
            symbol_list_add(list, line);
        line = next;
    }
}

static void update_entry_symbols(runtime_entry_t *entry, symbol_list_t *list, int success)
{
    if (!entry)
        return;
    for (int i = 0; i < entry->count; i++)
        free(entry->symbols[i]);
    free(entry->symbols);
    entry->symbols = list->items;
    entry->count = list->count;
    entry->capacity = list->capacity;
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    entry->ready = 1;
    entry->failed = success ? 0 : 1;
    entry->in_progress = 0;
}

static void *scraper_worker(void *arg)
{
    (void)arg;
    for (;;) {
        scraper_job_t job;
        pthread_mutex_lock(&scraper_mutex);
        while (job_count == 0)
            pthread_cond_wait(&job_cond, &scraper_mutex);
        if (!dequeue_job(&job)) {
            pthread_mutex_unlock(&scraper_mutex);
            continue;
        }
        pthread_mutex_unlock(&scraper_mutex);

        char buffer[SCRAPER_READ_LIMIT];
        int rc = run_language_command(job.lang, job.entry->module,
                                      buffer, sizeof(buffer));
        symbol_list_t list = { 0 };
        if (rc > 0)
            collect_symbols_from_buffer(buffer, &list);

        pthread_mutex_lock(&scraper_mutex);
        update_entry_symbols(job.entry, &list, rc >= 0);
        pthread_mutex_unlock(&scraper_mutex);
        symbol_list_free(&list);
    }
    return NULL;
}

void scraper_init(void)
{
    pthread_mutex_lock(&scraper_mutex);
    if (!worker_started) {
        if (pthread_create(&worker_thread, NULL, scraper_worker, NULL) == 0)
            worker_started = 1;
    }
    pthread_mutex_unlock(&scraper_mutex);
}

void scraper_cleanup(void)
{
    pthread_mutex_lock(&scraper_mutex);
    for (int l = 0; l < SCRAPER_LANG_COUNT; l++) {
        runtime_cache_t *cache = &caches[l];
        if (cache->entries) {
            for (int i = 0; i < cache->count; i++) {
                free_entry(cache->entries[i]);
            }
            free(cache->entries);
            cache->entries = NULL;
        }
        cache->count = 0;
        cache->capacity = 0;
    }
    if (job_queue) {
        free(job_queue);
        job_queue = NULL;
    }
    job_count = 0;
    job_capacity = 0;
    pthread_mutex_unlock(&scraper_mutex);
}

int scraper_iterate_symbols(scraper_lang_t lang, const char *module,
                            scraper_symbol_cb cb, void *userdata)
{
    if (!module || !*module || lang < 0 || lang >= SCRAPER_LANG_COUNT)
        return 0;
    if (!worker_started)
        scraper_init();

    runtime_entry_t *entry = NULL;
    pthread_mutex_lock(&scraper_mutex);
    entry = find_entry(lang, module);
    if (!entry) {
        entry = create_entry(lang, module);
        if (entry)
            entry->in_progress = 1;
        enqueue_job(lang, entry);
    } else if (!entry->ready && !entry->in_progress) {
        entry->in_progress = 1;
        enqueue_job(lang, entry);
    }

    int ready = entry && entry->ready;
    char **items = NULL;
    int count = 0;
    if (ready && entry->count > 0) {
        items = entry->symbols;
        count = entry->count;
    }
    pthread_mutex_unlock(&scraper_mutex);

    if (ready && cb && items) {
        for (int i = 0; i < count; i++)
            cb(items[i], userdata);
    }
    return ready;
}
