#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static uint64_t g_checks_run = 0;
static uint64_t g_failures = 0;

#define CHECK_TRUE(cond, msg) do { \
    g_checks_run++; \
    if (!(cond)) { \
        g_failures++; \
        fprintf(stderr, "[FAIL] %s:%d -> %s\n", __FILE__, __LINE__, (msg)); \
    } \
} while (0)

#define CHECK_FALSE(cond, msg) CHECK_TRUE(!(cond), (msg))

static const char *get_tmp_base(void) {
    const char *t = getenv("TMPDIR");
    if (t && t[0] != '\0') return t;
    return ".";
}

static int run_cmd_expect_exit(const char *cmd, int expected_code) {
    int status = system(cmd);
    if (status == -1) return -1;
    if (!WIFEXITED(status)) return -1;
    int code = WEXITSTATUS(status);
    return (code == expected_code) ? 0 : 1;
}

static int write_exact_bytes(const char *path, const unsigned char *data, size_t len) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    size_t w = (len > 0) ? fwrite(data, 1, len, fp) : 0;
    int err = ferror(fp);
    fclose(fp);
    if (err != 0 || w != len) return -1;
    return 0;
}

static void stress_alloc_and_budget_binaries(const char *tools_dir) {
    char alloc_bin[PATH_MAX];
    char budget_bin[PATH_MAX];
    snprintf(alloc_bin, sizeof(alloc_bin), "%s/stress_alloc_guard", tools_dir);
    snprintf(budget_bin, sizeof(budget_bin), "%s/stress_job_budget_planner", tools_dir);

    char cmd[PATH_MAX * 3];
    snprintf(cmd, sizeof(cmd),
             "gcc -std=c11 -O2 -Wall -Wextra -Werror \"%s/safe_alloc_guard.c\" -o \"%s\"",
             tools_dir, alloc_bin);
    CHECK_TRUE(system(cmd) == 0, "Compile safe_alloc_guard for stress test");

    snprintf(cmd, sizeof(cmd),
             "gcc -std=c11 -O2 -Wall -Wextra -Werror \"%s/job_budget_planner.c\" -o \"%s\"",
             tools_dir, budget_bin);
    CHECK_TRUE(system(cmd) == 0, "Compile job_budget_planner for stress test");

    /* 1. Stress safe_alloc_guard across 500 iterations */
    for (uint64_t i = 1; i <= 500; i++) {
        uint64_t elem = (i % 64) + 1;
        uint64_t cap = i * elem + 1024;
        snprintf(cmd, sizeof(cmd), "\"%s\" %" PRIu64 " %" PRIu64 " %" PRIu64 " >/dev/null",
                 alloc_bin, i, elem, cap);
        CHECK_TRUE(run_cmd_expect_exit(cmd, 0) == 0, "Valid allocation within cap should pass");

        /* Exceed cap by 1 byte -> must block (exit 1) */
        snprintf(cmd, sizeof(cmd), "\"%s\" 1 %" PRIu64 " %" PRIu64 " >/dev/null",
                 alloc_bin, cap + 1, cap);
        CHECK_TRUE(run_cmd_expect_exit(cmd, 1) == 0, "Allocation exceeding cap by 1 byte must block");
    }

    /* Extreme overflow attempt via huge count */
    snprintf(cmd, sizeof(cmd), "\"%s\" 18446744073709551615 16 18446744073709551615 >/dev/null",
             alloc_bin);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 1) == 0, "UINT64_MAX * 16 must be blocked cleanly");

    /* 2. Stress job_budget_planner across clamping axes (jobs, RAM, time) */
    snprintf(cmd, sizeof(cmd), "\"%s\" 100 10 2048 128 3600 60 >/dev/null", budget_bin);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 0) == 0, "Batch clamped to max_jobs=10 must exit 0");

    snprintf(cmd, sizeof(cmd), "\"%s\" 100 50 512 256 3600 60 >/dev/null", budget_bin);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 0) == 0, "Batch clamped to 2 jobs by RAM must exit 0");

    snprintf(cmd, sizeof(cmd), "\"%s\" 100 50 4096 128 180 60 >/dev/null", budget_bin);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 0) == 0, "Batch clamped to 3 jobs by time must exit 0");

    snprintf(cmd, sizeof(cmd), "\"%s\" 5 10 256 512 600 60 >/dev/null", budget_bin);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 1) == 0, "Single job RAM > budget must be REJECTED (exit 1)");

    snprintf(cmd, sizeof(cmd), "\"%s\" 5 10 1024 128 30 60 >/dev/null", budget_bin);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 1) == 0, "Single job sec > max_sec must be REJECTED (exit 1)");

    remove(alloc_bin);
    remove(budget_bin);
}

static void stress_line_and_xml_guards(const char *tools_dir) {
    const char *base = get_tmp_base();
    char line_bin[PATH_MAX], xml_bin[PATH_MAX];
    char f_exact[PATH_MAX], f_over[PATH_MAX], f_empty[PATH_MAX];
    char x_valid[PATH_MAX], x_self_slash[PATH_MAX], x_bad_nest[PATH_MAX], x_nul[PATH_MAX], x_trunc[PATH_MAX];

    snprintf(line_bin, sizeof(line_bin), "%s/stress_safe_line_reader", tools_dir);
    snprintf(xml_bin, sizeof(xml_bin), "%s/stress_xml_scene_guard", tools_dir);

    snprintf(f_exact, sizeof(f_exact), "%s/agent1_stress_exact.txt", base);
    snprintf(f_over, sizeof(f_over), "%s/agent1_stress_over.txt", base);
    snprintf(f_empty, sizeof(f_empty), "%s/agent1_stress_empty.txt", base);

    snprintf(x_valid, sizeof(x_valid), "%s/agent1_stress_valid.xml", base);
    snprintf(x_self_slash, sizeof(x_self_slash), "%s/agent1_stress_slash.xml", base);
    snprintf(x_bad_nest, sizeof(x_bad_nest), "%s/agent1_stress_bad_nest.xml", base);
    snprintf(x_nul, sizeof(x_nul), "%s/agent1_stress_nul.xml", base);
    snprintf(x_trunc, sizeof(x_trunc), "%s/agent1_stress_trunc.xml", base);

    char cmd[PATH_MAX * 4];
    snprintf(cmd, sizeof(cmd),
             "gcc -std=c11 -O2 -Wall -Wextra -Werror \"%s/safe_line_reader.c\" -o \"%s\"",
             tools_dir, line_bin);
    CHECK_TRUE(system(cmd) == 0, "Compile safe_line_reader for stress test");

    snprintf(cmd, sizeof(cmd),
             "gcc -std=c11 -O2 -Wall -Wextra -Werror \"%s/xml_scene_guard.c\" -o \"%s\"",
             tools_dir, xml_bin);
    CHECK_TRUE(system(cmd) == 0, "Compile xml_scene_guard for stress test");

    /* Line reader tests: exact 15 chars + '\n' (16 bytes total) with max_line_bytes=17 -> fits in fgets(17) */
    unsigned char exact_buf[32];
    memset(exact_buf, 'A', 15);
    exact_buf[15] = '\n';
    CHECK_TRUE(write_exact_bytes(f_exact, exact_buf, 16) == 0, "Write exact boundary line file");

    /* Line with 20 'B's before '\n' with max_line_bytes=16 -> must block (exit 1) */
    unsigned char over_buf[32];
    memset(over_buf, 'B', 20);
    over_buf[20] = '\n';
    CHECK_TRUE(write_exact_bytes(f_over, over_buf, 21) == 0, "Write overlong line file");

    CHECK_TRUE(write_exact_bytes(f_empty, (const unsigned char *)"", 0) == 0, "Write empty file");

    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" 1024 17 >/dev/null", line_bin, f_exact);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 0) == 0, "Line fitting within max_line_bytes must pass");

    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" 1024 16 >/dev/null", line_bin, f_over);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 1) == 0, "Line exceeding max_line_bytes must block");

    /* XML scene guard tests */
    const char *xml_ok =
        "<?xml version=\"1.0\"?>\n"
        "<!-- render scene comment -->\n"
        "<scene id=\"1\">\n"
        "  <camera fov=\"45\"/>\n"
        "  <mesh name=\"cube\">\n"
        "    <material shader=\"matte\"/>\n"
        "  </mesh>\n"
        "</scene>\n";
    CHECK_TRUE(write_exact_bytes(x_valid, (const unsigned char *)xml_ok, strlen(xml_ok)) == 0,
               "Write valid XML scene");

    /* Self-closing tag containing slash inside attribute value */
    const char *xml_slash_attr = "<scene><path val=\"a/b/c\"/></scene>\n";
    CHECK_TRUE(write_exact_bytes(x_self_slash, (const unsigned char *)xml_slash_attr, strlen(xml_slash_attr)) == 0,
               "Write XML with slash inside attribute value");

    const char *xml_bad = "<scene><mesh><light></mesh></scene>\n";
    CHECK_TRUE(write_exact_bytes(x_bad_nest, (const unsigned char *)xml_bad, strlen(xml_bad)) == 0,
               "Write mismatched XML tags");

    const unsigned char xml_with_nul[] = "<scene><x/></scene>\0<tail/>";
    CHECK_TRUE(write_exact_bytes(x_nul, xml_with_nul, sizeof(xml_with_nul) - 1) == 0,
               "Write XML containing NUL byte");

    const char *xml_eof_lt = "<scene><mesh/></scene><";
    CHECK_TRUE(write_exact_bytes(x_trunc, (const unsigned char *)xml_eof_lt, strlen(xml_eof_lt)) == 0,
               "Write XML truncated with trailing '<'");

    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" 65536 >/dev/null", xml_bin, x_valid);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 0) == 0, "Valid XML scene must pass");

    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" 65536 >/dev/null", xml_bin, x_self_slash);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 0) == 0, "Self-closing XML with slash in attribute must pass");

    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" 65536 >/dev/null", xml_bin, x_bad_nest);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 1) == 0, "Mismatched XML tags must block");

    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" 65536 >/dev/null", xml_bin, x_nul);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 1) == 0, "NUL byte inside XML must block");

    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" 65536 >/dev/null", xml_bin, x_trunc);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 1) == 0, "Trailing '<' at EOF must block");

    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" 65536 >/dev/null", xml_bin, f_empty);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 1) == 0, "Empty XML file must block");

    remove(f_exact);
    remove(f_over);
    remove(f_empty);
    remove(x_valid);
    remove(x_self_slash);
    remove(x_bad_nest);
    remove(x_nul);
    remove(x_trunc);
    remove(line_bin);
    remove(xml_bin);
}

static void stress_path_and_resource_guards(const char *tools_dir) {
    const char *base = get_tmp_base();
    char path_bin[PATH_MAX], res_bin[PATH_MAX];
    char root_dir[PATH_MAX], child_dir[PATH_MAX];

    snprintf(path_bin, sizeof(path_bin), "%s/stress_safe_path_guard", tools_dir);
    snprintf(res_bin, sizeof(res_bin), "%s/stress_resource_gate", tools_dir);
    snprintf(root_dir, sizeof(root_dir), "%s/agent1_stress_root", base);
    snprintf(child_dir, sizeof(child_dir), "%s/agent1_stress_root/worker_01", base);

    char cmd[PATH_MAX * 4];
    snprintf(cmd, sizeof(cmd),
             "gcc -std=c11 -O2 -Wall -Wextra -Werror \"%s/safe_path_guard.c\" -o \"%s\"",
             tools_dir, path_bin);
    CHECK_TRUE(system(cmd) == 0, "Compile safe_path_guard for stress test");

    snprintf(cmd, sizeof(cmd),
             "gcc -std=c11 -O2 -Wall -Wextra -Werror \"%s/resource_gate.c\" -o \"%s\"",
             tools_dir, res_bin);
    CHECK_TRUE(system(cmd) == 0, "Compile resource_gate for stress test");

    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", child_dir);
    CHECK_TRUE(system(cmd) == 0, "Create temporary test directory hierarchy");

    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" \"%s\" >/dev/null", path_bin, child_dir, root_dir);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 0) == 0, "Child subpath inside root must pass");

    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" \"%s\" >/dev/null 2>&1", path_bin, root_dir, root_dir);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 1) == 0, "Root directory itself must be blocked");

    snprintf(cmd, sizeof(cmd), "\"%s\" \"/\" \"%s\" >/dev/null 2>&1", path_bin, root_dir);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 1) == 0, "Filesystem root '/' must be blocked");

    /* Dry-run check on existing directory with impossible RAM requirement -> must exit 1 cleanly */
    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" 999999999 0 1 999999 --dry-run >/dev/null",
             res_bin, child_dir);
    CHECK_TRUE(run_cmd_expect_exit(cmd, 1) == 0, "Impossible RAM threshold in dry-run must block cleanly");

    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", root_dir);
    (void)system(cmd);
    remove(path_bin);
    remove(res_bin);
}

int main(void) {
    const char *tools_dir = getenv("AGENT1_TOOLS_DIR");
    if (!tools_dir || tools_dir[0] == '\0') {
        tools_dir = ".";
    }

    stress_alloc_and_budget_binaries(tools_dir);
    stress_line_and_xml_guards(tools_dir);
    stress_path_and_resource_guards(tools_dir);

    printf("[HEAVY STRESS SUMMARY] checks=%" PRIu64 " failures=%" PRIu64 "\n",
           g_checks_run, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
