#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define VERSION_INFO "pstree 0.1"

typedef struct {
    pid_t pid;
    pid_t ppid;
    char name[256];
    int *children;
    size_t child_count;
    size_t child_cap;
} Process;

typedef struct {
    Process *items;
    size_t len;
    size_t cap;
} ProcessList;

static void print_usage(FILE *stream);
static bool parse_args(int argc, char *argv[], bool *show_pids,
                       bool *numeric_sort, bool *show_version);
static bool is_pid_name(const char *name);
static bool read_process(const char *proc_root, pid_t pid, Process *process);
static bool read_status_file(const char *path, Process *process);
static bool add_process(ProcessList *list, Process process);
static int find_process_index(const ProcessList *list, pid_t pid);
static bool add_child(Process *parent, int child_index);
static bool build_tree(ProcessList *list);
static int compare_by_name(const void *lhs, const void *rhs);
static int compare_by_pid(const void *lhs, const void *rhs);
static void sort_tree(ProcessList *list, bool numeric_sort);
static bool print_forest(const ProcessList *list, bool show_pids,
                         bool numeric_sort);
static void print_tree(const ProcessList *list, int index, int depth,
                       bool show_pids);
static void free_process_list(ProcessList *list);

int main(int argc, char *argv[]) {
    bool show_pids = false;
    bool numeric_sort = false;
    bool show_version = false;

    if (!parse_args(argc, argv, &show_pids, &numeric_sort, &show_version)) {
        return EXIT_FAILURE;
    }

    if (show_version) {
        puts(VERSION_INFO);
        return EXIT_SUCCESS;
    }

    const char *proc_root = getenv("PSTREE_PROC_ROOT");
    if (proc_root == NULL || proc_root[0] == '\0') {
        proc_root = "/proc";
    }

    DIR *dir = opendir(proc_root);
    if (dir == NULL) {
        fprintf(stderr, "failed to open %s: %s\n", proc_root, strerror(errno));
        return EXIT_FAILURE;
    }

    ProcessList list = {0};
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!is_pid_name(entry->d_name)) {
            continue;
        }

        Process process = {0};
        process.pid = (pid_t)strtol(entry->d_name, NULL, 10);
        if (read_process(proc_root, process.pid, &process)) {
            if (!add_process(&list, process)) {
                closedir(dir);
                free_process_list(&list);
                return EXIT_FAILURE;
            }
        }
    }
    closedir(dir);

    if (!build_tree(&list)) {
        free_process_list(&list);
        return EXIT_FAILURE;
    }

    sort_tree(&list, numeric_sort);
    if (!print_forest(&list, show_pids, numeric_sort)) {
        free_process_list(&list);
        return EXIT_FAILURE;
    }

    free_process_list(&list);
    return EXIT_SUCCESS;
}

static void print_usage(FILE *stream) {
    fprintf(stream,
            "usage: pstree [-p|--show-pids] [-n|--numeric-sort] "
            "[-V|--version]\n");
}

static bool parse_args(int argc, char *argv[], bool *show_pids,
                       bool *numeric_sort, bool *show_version) {
    static const struct option long_options[] = {
        {"show-pids", no_argument, NULL, 'p'},
        {"numeric-sort", no_argument, NULL, 'n'},
        {"version", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0},
    };

    opterr = 0;
    int opt;
    while ((opt = getopt_long(argc, argv, "pnV", long_options, NULL)) != -1) {
        switch (opt) {
        case 'p':
            *show_pids = true;
            break;
        case 'n':
            *numeric_sort = true;
            break;
        case 'V':
            *show_version = true;
            break;
        default:
            print_usage(stderr);
            return false;
        }
    }

    if (optind != argc) {
        print_usage(stderr);
        return false;
    }

    return true;
}

static bool is_pid_name(const char *name) {
    if (name[0] == '\0') {
        return false;
    }

    for (size_t i = 0; name[i] != '\0'; i++) {
        if (!isdigit((unsigned char)name[i])) {
            return false;
        }
    }
    return true;
}

static bool read_process(const char *proc_root, pid_t pid, Process *process) {
    char path[512];
    int n = snprintf(path, sizeof(path), "%s/%ld/status", proc_root,
                     (long)pid);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return false;
    }
    return read_status_file(path, process);
}

static bool read_status_file(const char *path, Process *process) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }

    bool has_name = false;
    bool has_ppid = false;
    char line[512];
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, "Name:", 5) == 0) {
            char name[sizeof(process->name)];
            if (sscanf(line + 5, "%255s", name) == 1) {
                snprintf(process->name, sizeof(process->name), "%s", name);
                has_name = true;
            }
        } else if (strncmp(line, "PPid:", 5) == 0) {
            long ppid;
            if (sscanf(line + 5, "%ld", &ppid) == 1) {
                process->ppid = (pid_t)ppid;
                has_ppid = true;
            }
        }
    }

    fclose(file);
    return has_name && has_ppid;
}

static bool add_process(ProcessList *list, Process process) {
    if (list->len == list->cap) {
        size_t new_cap = list->cap == 0 ? 64 : list->cap * 2;
        Process *new_items = realloc(list->items, new_cap * sizeof(*new_items));
        if (new_items == NULL) {
            fprintf(stderr, "out of memory\n");
            return false;
        }
        list->items = new_items;
        list->cap = new_cap;
    }

    list->items[list->len++] = process;
    return true;
}

static int find_process_index(const ProcessList *list, pid_t pid) {
    for (size_t i = 0; i < list->len; i++) {
        if (list->items[i].pid == pid) {
            return (int)i;
        }
    }
    return -1;
}

static bool add_child(Process *parent, int child_index) {
    if (parent->child_count == parent->child_cap) {
        size_t new_cap = parent->child_cap == 0 ? 4 : parent->child_cap * 2;
        int *new_children =
            realloc(parent->children, new_cap * sizeof(*new_children));
        if (new_children == NULL) {
            fprintf(stderr, "out of memory\n");
            return false;
        }
        parent->children = new_children;
        parent->child_cap = new_cap;
    }

    parent->children[parent->child_count++] = child_index;
    return true;
}

static bool build_tree(ProcessList *list) {
    for (size_t i = 0; i < list->len; i++) {
        int parent = find_process_index(list, list->items[i].ppid);
        if (parent != -1) {
            if (!add_child(&list->items[parent], (int)i)) {
                return false;
            }
        }
    }
    return true;
}

static int compare_by_name(const void *lhs, const void *rhs) {
    const Process *const *a = lhs;
    const Process *const *b = rhs;
    int name_cmp = strcmp((*a)->name, (*b)->name);
    if (name_cmp != 0) {
        return name_cmp;
    }
    return ((*a)->pid > (*b)->pid) - ((*a)->pid < (*b)->pid);
}

static int compare_by_pid(const void *lhs, const void *rhs) {
    const Process *const *a = lhs;
    const Process *const *b = rhs;
    return ((*a)->pid > (*b)->pid) - ((*a)->pid < (*b)->pid);
}

static void sort_tree(ProcessList *list, bool numeric_sort) {
    int (*compare)(const void *, const void *) =
        numeric_sort ? compare_by_pid : compare_by_name;

    for (size_t i = 0; i < list->len; i++) {
        Process *parent = &list->items[i];
        if (parent->child_count < 2) {
            continue;
        }

        Process **children = malloc(parent->child_count * sizeof(*children));
        if (children == NULL) {
            continue;
        }

        for (size_t j = 0; j < parent->child_count; j++) {
            children[j] = &list->items[parent->children[j]];
        }
        qsort(children, parent->child_count, sizeof(*children), compare);
        for (size_t j = 0; j < parent->child_count; j++) {
            parent->children[j] = (int)(children[j] - list->items);
        }

        free(children);
    }
}

static bool print_forest(const ProcessList *list, bool show_pids,
                         bool numeric_sort) {
    Process **roots = malloc(list->len * sizeof(*roots));
    if (roots == NULL && list->len > 0) {
        fprintf(stderr, "out of memory\n");
        return false;
    }

    size_t root_count = 0;
    for (size_t i = 0; i < list->len; i++) {
        if (list->items[i].ppid == 0 ||
            find_process_index(list, list->items[i].ppid) == -1) {
            roots[root_count++] = &list->items[i];
        }
    }

    qsort(roots, root_count, sizeof(*roots),
          numeric_sort ? compare_by_pid : compare_by_name);
    for (size_t i = 0; i < root_count; i++) {
        print_tree(list, (int)(roots[i] - list->items), 0, show_pids);
    }

    free(roots);
    return true;
}

static void print_tree(const ProcessList *list, int index, int depth,
                       bool show_pids) {
    const Process *process = &list->items[index];
    for (int i = 0; i < depth; i++) {
        printf("  ");
    }
    if (show_pids) {
        printf("%s(%ld)\n", process->name, (long)process->pid);
    } else {
        printf("%s\n", process->name);
    }

    for (size_t i = 0; i < process->child_count; i++) {
        print_tree(list, process->children[i], depth + 1, show_pids);
    }
}

static void free_process_list(ProcessList *list) {
    for (size_t i = 0; i < list->len; i++) {
        free(list->items[i].children);
    }
    free(list->items);
}
