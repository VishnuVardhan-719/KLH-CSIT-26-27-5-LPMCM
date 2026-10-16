#include "run_tests.h"
#include "process_tree.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>

static ProcessInfo make_proc(int pid, int ppid, const char *name)
{
    ProcessInfo p;
    memset(&p, 0, sizeof(p));
    p.pid = pid;
    p.ppid = ppid;
    p.state = 'S';
    utils_str_copy(p.name, sizeof(p.name), name);
    return p;
}

static void test_basic_tree(void)
{
    printf("tree: basic hierarchy\n");

    ProcessInfo procs[] = {
        make_proc(1, 0, "systemd"),
        make_proc(500, 1, "sshd"),
        make_proc(600, 500, "bash"),
        make_proc(700, 600, "python"),
        make_proc(800, 1, "firefox"),
    };
    ProcessList list;
    list.procs = procs;
    list.count = 5;
    list.capacity = 5;

    ProcessTree tree;
    char err[256];
    t_check_int(ptree_build(&list, &tree, err, sizeof(err)), 0, "tree builds");
    t_check_int((long long)tree.count, 5, "tree node count");

    /* systemd (pid 1) has children 500 and 800 (in PID order) */
    t_check_int(tree.nodes[0].child_count, 2, "systemd has 2 children");
    t_check_int(tree.nodes[0].children[0], 1, "first child is sshd");
    t_check_int(tree.nodes[0].children[1], 4, "second child is firefox");

    /* bash's child is python */
    t_check_int(tree.nodes[2].child_count, 1, "bash has 1 child");
    t_check_int(tree.nodes[2].children[0], 3, "bash child is python");

    char buf[8192];
    ptree_render(&tree, &list, "", true, false, 0, buf, sizeof(buf));
    t_check(strstr(buf, "systemd (1)") != NULL, "render contains systemd");
    t_check(strstr(buf, "python (700)") != NULL, "render contains python");
    t_check(strstr(buf, "└──") != NULL || strstr(buf, "\\--") != NULL,
            "render has branch glyphs");

    ptree_free(&tree);
}

static void test_orphan_adopted(void)
{
    printf("tree: orphans go under the orphan root\n");

    /* parent 999 does not exist in the snapshot */
    ProcessInfo procs[] = {
        make_proc(10, 999, "lonely"),
        make_proc(20, 10, "child"),
    };
    ProcessList list;
    list.procs = procs;
    list.count = 2;
    list.capacity = 2;

    ProcessTree tree;
    char err[256];
    t_check_int(ptree_build(&list, &tree, err, sizeof(err)), 0, "builds");
    t_check(tree.orphan_root >= 0, "orphan root exists");
    t_check_int(tree.nodes[tree.orphan_root].child_count, 1,
                "orphan root holds the lonely process");
    t_check_int(tree.nodes[0].child_count, 1, "lonely still has its child");

    ptree_free(&tree);
}

static void test_cycle_safe(void)
{
    printf("tree: cycles do not hang the renderer\n");

    /* a -> b -> a is impossible in /proc but must not hang if it happens */
    ProcessInfo procs[] = {
        make_proc(1, 2, "a"),
        make_proc(2, 1, "b"),
    };
    ProcessList list;
    list.procs = procs;
    list.count = 2;
    list.capacity = 2;

    ProcessTree tree;
    char err[256];
    t_check_int(ptree_build(&list, &tree, err, sizeof(err)), 0, "builds");

    char buf[8192];
    ptree_render(&tree, &list, "", true, false, 0, buf, sizeof(buf));
    t_check(strlen(buf) > 0, "render produced output without hanging");

    ptree_free(&tree);
}

void run_tree_tests(void)
{
    test_basic_tree();
    test_orphan_adopted();
    test_cycle_safe();
}
