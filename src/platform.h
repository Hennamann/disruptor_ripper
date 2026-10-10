#ifndef PLATFORM_H
#define PLATFORM_H

/* Create a directory (and all missing parents). Succeeds silently if it already exists. */
void make_dirs(const char *path);

/* Join directory and file name into out (size n), using '/' which works on every platform we target. */
void path_join(char *out, size_t n, const char *dir, const char *name);

/* Directory part of a path (without trailing separator). Returns "." when none. */
void path_dirname(char *out, size_t n, const char *path);

#endif
