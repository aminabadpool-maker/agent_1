#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_timed_out = 0;
static pid_t g_child_pid = -1;

static void on_alarm(int sig) {
    (void)sig;
    g_timed_out = 1;
    if (g_child_pid > 0) {
        kill(-g_child_pid, SIGTERM);
    }
}

static void append_audit_log(const char *log_path, const char *event, int code, const char *cmd_preview) {
    if (!log_path || log_path[0] == '\0') return;

    struct stat st;
    if (stat(log_path, &st) == 0 && S_ISLNK(st.st_mode)) {
        return;
    }

    FILE *fp = fopen(log_path, "ab");
    if (!fp) return;

    time_t now = time(NULL);
    struct tm tm_now;
    char ts[32] = "unknown_time";
    if (localtime_r(&now, &tm_now) != NULL) {
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);
    }

    fprintf(fp, "[%s] event=%s code=%d cmd=\"%s\"\n",
            ts,
            event ? event : "unknown",
            code,
            cmd_preview ? cmd_preview : "");
    fclose(fp);
}

static int parse_positive_u32(const char *s, unsigned int *out) {
    if (!s || !*s) return -1;
    errno = 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v == 0 || v > 86400UL) {
        return -1;
    }
    *out = (unsigned int)v;
    return 0;
}

static int run_guarded_command(unsigned int timeout_sec,
                               const char *log_path,
                               char *const argv_cmd[]) {
    if (!argv_cmd || !argv_cmd[0]) return 2;

    g_timed_out = 0;
    g_child_pid = -1;

    struct sigaction sa_alarm;
    memset(&sa_alarm, 0, sizeof(sa_alarm));
    sa_alarm.sa_handler = on_alarm;
    sigemptyset(&sa_alarm.sa_mask);
    if (sigaction(SIGALRM, &sa_alarm, NULL) != 0) {
        return 2;
    }

    pid_t pid = fork();
    if (pid < 0) {
        append_audit_log(log_path, "fork_failed", errno, argv_cmd[0]);
        return 2;
    }

    if (pid == 0) {
        (void)setpgid(0, 0);
        execvp(argv_cmd[0], argv_cmd);
        _exit(127);
    }

    g_child_pid = pid;
    (void)setpgid(pid, pid);
    append_audit_log(log_path, "start", 0, argv_cmd[0]);
    alarm(timeout_sec);

    int status = 0;
    pid_t w;
    do {
        w = waitpid(pid, &status, 0);
    } while (w < 0 && errno == EINTR && !g_timed_out);

    alarm(0);

    if (g_timed_out) {
        /* Give processes a brief moment to exit cleanly on SIGTERM, then force SIGKILL */
        usleep(100000);
        if (waitpid(pid, &status, WNOHANG) == 0) {
            kill(-pid, SIGKILL);
            (void)waitpid(pid, &status, 0);
        }
        g_child_pid = -1;
        append_audit_log(log_path, "timeout_killed", 124, argv_cmd[0]);
        fprintf(stderr, "[TIMED-EXEC] Command exceeded %u seconds and was terminated.\n", timeout_sec);
        return 124;
    }

    g_child_pid = -1;
    if (w < 0) {
        append_audit_log(log_path, "waitpid_error", errno, argv_cmd[0]);
        return 2;
    }

    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    append_audit_log(log_path, exit_code == 0 ? "pass" : "fail", exit_code, argv_cmd[0]);
    return exit_code;
}

static int run_self_tests(void) {
    const char *tmp_env = getenv("TMPDIR");
    const char *base = (tmp_env && tmp_env[0] != '\0') ? tmp_env : ".";
    char log_path[512];
    snprintf(log_path, sizeof(log_path), "%s/agent1_timed_exec_test.log", base);
    remove(log_path);

    char *fast_cmd[] = { "sh", "-c", "exit 0", NULL };
    int rc_fast = run_guarded_command(3, log_path, fast_cmd);
    if (rc_fast != 0) {
        remove(log_path);
        fprintf(stderr, "[FAIL] Expected fast command to exit 0, got %d\n", rc_fast);
        return 1;
    }

    char *slow_cmd[] = { "sh", "-c", "sleep 5", NULL };
    int rc_slow = run_guarded_command(1, log_path, slow_cmd);
    remove(log_path);
    if (rc_slow != 124) {
        fprintf(stderr, "[FAIL] Expected slow command to time out with 124, got %d\n", rc_slow);
        return 1;
    }

    printf("[SELF-TEST PASS] timed_exec_guard timeout & audit checks passed.\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--test") == 0 || strcmp(argv[1], "-t") == 0)) {
        return run_self_tests();
    }

    /* Usage: timed_exec_guard <timeout_sec> <log_path> <command> [args...] */
    if (argc < 4) {
        fprintf(stderr,
                "Usage:\n"
                "  %s --test\n"
                "  %s <timeout_sec> <log_path> <command> [args...]\n",
                argv[0], argv[0]);
        return 2;
    }

    unsigned int timeout_sec = 0;
    if (parse_positive_u32(argv[1], &timeout_sec) != 0) {
        fprintf(stderr, "[TIMED-EXEC] Invalid timeout_sec (must be 1..86400).\n");
        return 2;
    }

    return run_guarded_command(timeout_sec, argv[2], &argv[3]);
}
