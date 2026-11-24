#include "sync_git_process.h"
#include "portability.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#ifndef _WIN32
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif

#ifdef _WIN32
/* CreateProcess does not invoke cmd.exe. Quote arguments for the child's CRT. */
static bool windows_argument(char *line, size_t cap, size_t *used, const char *arg) {
    size_t n = *used;
    if (n && n < cap) line[n++] = ' ';
    if (n >= cap) return false;
    line[n++] = '"';
    for (;;) {
        size_t slashes = 0;
        while (*arg == '\\') { slashes++; arg++; }
        size_t escaped = (*arg == '"' || !*arg) ? slashes * 2 + (*arg == '"') : slashes;
        if (escaped + 2 >= cap - n) return false;
        while (escaped--) line[n++] = '\\';
        if (!*arg) break;
        line[n++] = *arg++;
    }
    line[n++] = '"';
    line[n] = '\0';
    *used = n;
    return true;
}

static int spawn_git(char *const *args, FILE *output) {
    char command[4096] = "";
    size_t used = 0;
    for (int i = 0; args[i]; i++)
        if (!windows_argument(command, sizeof(command), &used, args[i])) return -1;

    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), NULL, TRUE};
    HANDLE null_file = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (null_file == INVALID_HANDLE_VALUE) return -1;

    HANDLE child_output = null_file;
    if (output && !DuplicateHandle(GetCurrentProcess(), (HANDLE)_get_osfhandle(_fileno(output)),
                                   GetCurrentProcess(), &child_output, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
        CloseHandle(null_file);
        return -1;
    }
    STARTUPINFOA startup = {0};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = null_file;
    startup.hStdError = null_file;
    startup.hStdOutput = child_output;
    PROCESS_INFORMATION process;
    BOOL started = CreateProcessA(NULL, command, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                                  NULL, NULL, &startup, &process);
    if (output) CloseHandle(child_output);
    CloseHandle(null_file);
    if (!started) return -1;
    DWORD code;
    if (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(process.hProcess, &code)) code = 255;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return (int)code;
}
#else
static int spawn_git(char *const *args, FILE *output) {
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) return -1;
    int rc = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, AI_DEVNULL, O_RDONLY, 0);
    if (rc == 0) rc = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, AI_DEVNULL, O_WRONLY, 0);
    if (rc == 0) {
        rc = output ? posix_spawn_file_actions_adddup2(&actions, fileno(output), STDOUT_FILENO)
                    : posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, AI_DEVNULL, O_WRONLY, 0);
    }
    pid_t pid;
    if (rc == 0) rc = posix_spawnp(&pid, args[0], &actions, NULL, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) return -1;

    int status;
    pid_t waited;
    do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
    return waited == pid && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
#endif

int sync_git_run(const char *cwd, const char *const *options, char *result, size_t cap) {
    char *args[24] = {"git", "-c", "credential.interactive=false",
                     "-c", "core.sshCommand=ssh -o BatchMode=yes -o ConnectTimeout=8", NULL};
    size_t argc = 5;
    if (cwd) {
        args[argc++] = "-C";
        args[argc++] = (char *)cwd;
    }
    for (size_t i = 0; options[i]; i++) {
        if (argc + 1 >= sizeof(args) / sizeof(args[0])) return -1;
        args[argc++] = (char *)options[i];
    }
    args[argc] = NULL;
    FILE *output = result ? tmpfile() : NULL;
    if (result && !output) return -1;
    int rc = spawn_git(args, output);
    if (output) {
        result[0] = '\0';
        if (rc == 0) {
            if (fseek(output, 0, SEEK_SET) != 0) rc = -1;
            else if (fgets(result, (int)cap, output)) result[strcspn(result, "\r\n")] = '\0';
            else if (ferror(output)) rc = -1;
        }
        fclose(output);
    }
    return rc;
}
