#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "common.h"
#include "platform.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0777)
#endif

static int is_sep(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

void make_dirs(const char *path) {
    char buf[PATH_MAX_LEN];
    size_t len = strlen(path), i;
    if (len == 0 || len >= sizeof(buf)) return;
    memcpy(buf, path, len + 1);
    for (i = 1; i < len; ++i) {
        if (is_sep(buf[i])) {
            char c = buf[i];
            buf[i] = '\0';
#ifdef _WIN32
            /* skip drive roots like "C:" */
            if (!(i == 2 && buf[1] == ':'))
#endif
            if (MKDIR(buf) != 0 && errno != EEXIST)
                die("Couldn't create directory %s: %s", buf, strerror(errno));
            buf[i] = c;
        }
    }
    if (MKDIR(buf) != 0 && errno != EEXIST)
        die("Couldn't create directory %s: %s", buf, strerror(errno));
}

void path_join(char *out, size_t n, const char *dir, const char *name) {
    size_t dl = strlen(dir);
    if (dl == 0)
        snprintf(out, n, "%s", name);
    else if (is_sep(dir[dl - 1]))
        snprintf(out, n, "%s%s", dir, name);
    else
        snprintf(out, n, "%s/%s", dir, name);
}

void path_dirname(char *out, size_t n, const char *path) {
    size_t len = strlen(path);
    while (len > 0 && !is_sep(path[len - 1])) --len;
    while (len > 1 && is_sep(path[len - 1])) --len;
    if (len == 0) { snprintf(out, n, "."); return; }
    if (len >= n) len = n - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}
