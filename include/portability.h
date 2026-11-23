#ifndef AI_PORTABILITY_H
#define AI_PORTABILITY_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

/*
 * Cross-platform shims so ai_space compiles unchanged on macOS/Linux (POSIX)
 * and Windows (MinGW). Call sites use the AI_* macros and ai_* helpers below
 * instead of POSIX-only APIs; POSIX platforms keep the original behavior.
 *
 * On Windows we deliberately do NOT include <unistd.h>: its POSIX declarations
 * (usleep etc.) clash with the CRT, so every unistd-only symbol goes through
 * this header instead.
 */

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <direct.h> /* _mkdir, _rmdir, _getcwd */
#include <io.h>     /* _access, _unlink */
#include <process.h> /* _getpid */

/* stat(2)-style helpers: check a path's readability/writability/existence */
#define AI_ACCESS(path, mode) _access((path), (mode))
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef F_OK
#define F_OK 0
#endif

/* POSIX 2-arg mkdir -> Win32 1-arg _mkdir */
#define AI_MKDIR(path) _mkdir((path))

/* mkdir -p: create path and missing parents (accepts / and \ separators) */
static inline void ai_mkdir_p(const char *path) {
    if (!path || !path[0]) return;
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            _mkdir(tmp);
            *p = saved;
        }
    }
    _mkdir(tmp);
}

/* popen/pclose -> _popen/_pclose (executed via cmd.exe; git/curl.exe ship with Windows) */
#define AI_POPEN(cmd, mode) _popen((cmd), (mode))
#define AI_PCLOSE(f)        _pclose((f))

/* microseconds -> Sleep(milliseconds) */
static inline void ai_usleep(long usec) { Sleep((DWORD)(usec / 1000)); }
#define usleep(usec) ai_usleep((long)(usec))

/* POSIX dirname(3): mutates the buffer, returns pointer inside it */
static inline char *ai_dirname(char *path) {
    if (!path || path[0] == '\0') return ".";
    char *last = NULL;
    for (char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') last = p;
    }
    if (!last) { path[0] = '.'; path[1] = '\0'; return path; }
    if (last == path) { path[1] = '\0'; return path; } /* "/usr" -> "/" */
    *last = '\0';
    return path;
}

/* USERPROFILE is the Windows user home; HOME kept as a manual override */
static inline const char *ai_home_dir(void) {
    const char *h = getenv("USERPROFILE");
    if (h && h[0]) return h;
    h = getenv("HOME");
    if (h && h[0]) return h;
    return "";
}

/* POSIX setenv(3) -> CRT _putenv */
static inline int ai_setenv(const char *name, const char *value) {
    char kv[1024];
    snprintf(kv, sizeof(kv), "%s=%s", name ? name : "", value ? value : "");
    return _putenv(kv);
}

/* null device of the shell (cmd.exe: NUL, POSIX: /dev/null) */
#define AI_DEVNULL "NUL"

static inline int ai_getpid(void) { return _getpid(); }
#define getpid() ai_getpid()

/* POSIX gethostname(2) via kernel32, no winsock init required */
static inline int ai_gethostname(char *name, size_t len) {
    char comp[MAX_COMPUTERNAME_LENGTH + 1] = "";
    DWORD n = (DWORD)(sizeof(comp));
    if (!GetComputerNameA(comp, &n)) return -1;
    strncpy(name, comp, len - 1);
    name[len - 1] = '\0';
    return 0;
}
#define gethostname(name, l) ai_gethostname((name), (l))

/* directory containing the running executable (no trailing slash), 0 on success */
static inline int ai_exe_dir(char *out, size_t cap) {
    char exe[1024];
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
    if (n == 0 || n >= sizeof(exe)) return -1;
    exe[n] = '\0';
    char *dir = ai_dirname(exe);
    if (!dir) return -1;
    snprintf(out, cap, "%s", dir);
    return 0;
}

/* localtime_r into a caller buffer; the app is single-threaded (CUI event loop),
 * so the CRT's static struct tm is safe here */
static inline struct tm *ai_localtime(const time_t *t, struct tm *out) {
    (void)out;
    return localtime(t);
}

#else /* POSIX */

#include <unistd.h>
#include <pwd.h>
#include <libgen.h> /* dirname */
#include <sys/stat.h>
#ifdef __APPLE__
#include <mach-o/dyld.h> /* _NSGetExecutablePath */
#endif

#define AI_MKDIR(path)       mkdir((path), 0755)

/* mkdir -p: create path and missing parents (accepts / and \ separators) */
static inline void ai_mkdir_p(const char *path) {
    if (!path || !path[0]) return;
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            mkdir(tmp, 0755);
            *p = saved;
        }
    }
    mkdir(tmp, 0755);
}

#define AI_POPEN(cmd, mode)  popen((cmd), (mode))
#define AI_PCLOSE(f)         pclose((f))
#define ai_dirname(path)     dirname((path))
#define AI_ACCESS(path, mode) access((path), (mode))

static inline const char *ai_home_dir(void) {
    const char *h = getenv("HOME");
    if (h && h[0]) return h;
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir && pw->pw_dir[0]) return pw->pw_dir;
    return "";
}

static inline int ai_setenv(const char *name, const char *value) {
    return setenv(name, value ? value : "", 1);
}

static inline struct tm *ai_localtime(const time_t *t, struct tm *out) {
    return localtime_r(t, out);
}

/* directory containing the running executable (no trailing slash), 0 on success */
static inline int ai_exe_dir(char *out, size_t cap) {
    char exe[1024];
    char resolved[1024];
#ifdef __APPLE__
    uint32_t n = sizeof(exe);
    if (_NSGetExecutablePath(exe, &n) != 0) return -1;
    if (!realpath(exe, resolved)) return -1;
#else /* Linux/BSD procfs */
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return -1;
    exe[n] = '\0';
    snprintf(resolved, sizeof(resolved), "%s", exe);
#endif
    char *dir = ai_dirname(resolved);
    if (!dir) return -1;
    snprintf(out, cap, "%s", dir);
    return 0;
}

#define AI_DEVNULL "/dev/null"

#endif /* _WIN32 */

#endif /* AI_PORTABILITY_H */
