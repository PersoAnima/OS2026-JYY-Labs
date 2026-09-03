#include "labyrinth.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(FILE *stream);
static bool print_map(const Labyrinth *labyrinth);
static bool is_floor(char ch);
static void dfs(const Labyrinth *labyrinth, int row, int col,
                bool visited[MAX_ROWS][MAX_COLS]);

int main(int argc, char *argv[]) {
    const char *map_file = NULL;
    const char *move_direction = NULL;
    char player_id = '\0';
    bool has_player = false;
    bool show_version = false;

    static const struct option long_options[] = {
        {"map", required_argument, NULL, 'm'},
        {"player", required_argument, NULL, 'p'},
        {"move", required_argument, NULL, 'd'},
        {"version", no_argument, NULL, 'v'},
        {NULL, 0, NULL, 0},
    };

    opterr = 0;
    int opt;
    while ((opt = getopt_long(argc, argv, "m:p:", long_options, NULL)) != -1) {
        switch (opt) {
        case 'm':
            map_file = optarg;
            break;
        case 'p':
            if (strlen(optarg) != 1 || !isValidPlayer(optarg[0])) {
                fprintf(stderr, "invalid player id: %s\n", optarg);
                return EXIT_FAILURE;
            }
            player_id = optarg[0];
            has_player = true;
            break;
        case 'd':
            move_direction = optarg;
            break;
        case 'v':
            show_version = true;
            break;
        default:
            print_usage(stderr);
            return EXIT_FAILURE;
        }
    }

    if (optind != argc) {
        print_usage(stderr);
        return EXIT_FAILURE;
    }

    if (show_version) {
        if (argc == 2) {
            puts(VERSION_INFO);
            return EXIT_SUCCESS;
        }
        fprintf(stderr, "--version cannot be combined with other arguments\n");
        return EXIT_FAILURE;
    }

    if (map_file == NULL || !has_player) {
        print_usage(stderr);
        return EXIT_FAILURE;
    }

    Labyrinth labyrinth;
    if (!loadMap(&labyrinth, map_file)) {
        return EXIT_FAILURE;
    }

    if (move_direction != NULL) {
        if (!movePlayer(&labyrinth, player_id, move_direction)) {
            return EXIT_FAILURE;
        }
        if (!saveMap(&labyrinth, map_file)) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    return print_map(&labyrinth) ? EXIT_SUCCESS : EXIT_FAILURE;
}

bool isValidPlayer(char player_id) {
    return player_id >= '0' && player_id <= '9';
}

bool loadMap(Labyrinth *labyrinth, const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        fprintf(stderr, "failed to open map file '%s': %s\n", filename,
                strerror(errno));
        return false;
    }

    labyrinth->rows = 0;
    labyrinth->cols = -1;

    char line[MAX_COLS + 3];
    while (fgets(line, sizeof(line), file) != NULL) {
        if (labyrinth->rows >= MAX_ROWS) {
            fprintf(stderr, "map has more than %d rows\n", MAX_ROWS);
            fclose(file);
            return false;
        }

        size_t len = strcspn(line, "\n");
        bool has_newline = line[len] == '\n';
        if (has_newline) {
            line[len] = '\0';
        } else if (!feof(file)) {
            fprintf(stderr, "map row exceeds %d columns\n", MAX_COLS);
            fclose(file);
            return false;
        }

        if (len == 0 || len > MAX_COLS) {
            fprintf(stderr, "invalid row length: %zu\n", len);
            fclose(file);
            return false;
        }

        if (labyrinth->cols == -1) {
            labyrinth->cols = (int)len;
        } else if (labyrinth->cols != (int)len) {
            fprintf(stderr, "map rows have inconsistent lengths\n");
            fclose(file);
            return false;
        }

        for (size_t col = 0; col < len; col++) {
            char ch = line[col];
            if (ch != '#' && ch != '.' && !isValidPlayer(ch)) {
                fprintf(stderr, "invalid map character: %c\n", ch);
                fclose(file);
                return false;
            }
        }

        memcpy(labyrinth->map[labyrinth->rows], line, len + 1);
        labyrinth->rows++;
    }

    if (ferror(file)) {
        fprintf(stderr, "failed to read map file '%s'\n", filename);
        fclose(file);
        return false;
    }
    fclose(file);

    if (labyrinth->rows == 0 || labyrinth->cols <= 0) {
        fprintf(stderr, "map is empty\n");
        return false;
    }

    if (!isConnected(labyrinth)) {
        fprintf(stderr, "map empty spaces are not connected\n");
        return false;
    }

    return true;
}

Position findPlayer(const Labyrinth *labyrinth, char player_id) {
    for (int row = 0; row < labyrinth->rows; row++) {
        for (int col = 0; col < labyrinth->cols; col++) {
            if (labyrinth->map[row][col] == player_id) {
                return (Position){row, col};
            }
        }
    }
    return (Position){-1, -1};
}

Position findFirstEmptySpace(const Labyrinth *labyrinth) {
    for (int row = 0; row < labyrinth->rows; row++) {
        for (int col = 0; col < labyrinth->cols; col++) {
            if (labyrinth->map[row][col] == '.') {
                return (Position){row, col};
            }
        }
    }
    return (Position){-1, -1};
}

bool isEmptySpace(const Labyrinth *labyrinth, int row, int col) {
    if (row < 0 || row >= labyrinth->rows || col < 0 || col >= labyrinth->cols) {
        return false;
    }
    return labyrinth->map[row][col] == '.';
}

bool movePlayer(Labyrinth *labyrinth, char player_id, const char *direction) {
    int dr = 0;
    int dc = 0;

    if (strcmp(direction, "up") == 0) {
        dr = -1;
    } else if (strcmp(direction, "down") == 0) {
        dr = 1;
    } else if (strcmp(direction, "left") == 0) {
        dc = -1;
    } else if (strcmp(direction, "right") == 0) {
        dc = 1;
    } else {
        fprintf(stderr, "invalid move direction: %s\n", direction);
        return false;
    }

    Position from = findPlayer(labyrinth, player_id);
    bool spawned = false;
    if (from.row == -1) {
        from = findFirstEmptySpace(labyrinth);
        if (from.row == -1) {
            fprintf(stderr, "player %c is missing and no empty space exists\n",
                    player_id);
            return false;
        }
        spawned = true;
    }

    int to_row = from.row + dr;
    int to_col = from.col + dc;
    if (!isEmptySpace(labyrinth, to_row, to_col)) {
        fprintf(stderr, "target cell is blocked\n");
        return false;
    }

    if (!spawned) {
        labyrinth->map[from.row][from.col] = '.';
    }
    labyrinth->map[to_row][to_col] = player_id;
    return true;
}

bool saveMap(const Labyrinth *labyrinth, const char *filename) {
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        fprintf(stderr, "failed to write map file '%s': %s\n", filename,
                strerror(errno));
        return false;
    }

    for (int row = 0; row < labyrinth->rows; row++) {
        if (fprintf(file, "%s\n", labyrinth->map[row]) < 0) {
            fprintf(stderr, "failed to write map row\n");
            fclose(file);
            return false;
        }
    }

    if (fclose(file) != 0) {
        fprintf(stderr, "failed to close map file '%s': %s\n", filename,
                strerror(errno));
        return false;
    }
    return true;
}

bool isConnected(const Labyrinth *labyrinth) {
    bool visited[MAX_ROWS][MAX_COLS] = {{false}};
    Position start = {-1, -1};
    int total_floor = 0;

    for (int row = 0; row < labyrinth->rows; row++) {
        for (int col = 0; col < labyrinth->cols; col++) {
            if (is_floor(labyrinth->map[row][col])) {
                total_floor++;
                if (start.row == -1) {
                    start = (Position){row, col};
                }
            }
        }
    }

    if (total_floor == 0) {
        return true;
    }

    dfs(labyrinth, start.row, start.col, visited);

    int visited_floor = 0;
    for (int row = 0; row < labyrinth->rows; row++) {
        for (int col = 0; col < labyrinth->cols; col++) {
            if (is_floor(labyrinth->map[row][col]) && visited[row][col]) {
                visited_floor++;
            }
        }
    }

    return visited_floor == total_floor;
}

static void print_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  labyrinth --map map.txt --player id\n");
    fprintf(stream, "  labyrinth -m map.txt -p id\n");
    fprintf(stream, "  labyrinth --map map.txt --player id --move direction\n");
    fprintf(stream, "  labyrinth --version\n");
}

static bool print_map(const Labyrinth *labyrinth) {
    for (int row = 0; row < labyrinth->rows; row++) {
        if (printf("%s\n", labyrinth->map[row]) < 0) {
            return false;
        }
    }
    return true;
}

static bool is_floor(char ch) {
    return ch == '.' || isValidPlayer(ch);
}

static void dfs(const Labyrinth *labyrinth, int row, int col,
                bool visited[MAX_ROWS][MAX_COLS]) {
    if (row < 0 || row >= labyrinth->rows || col < 0 || col >= labyrinth->cols) {
        return;
    }
    if (visited[row][col] || !is_floor(labyrinth->map[row][col])) {
        return;
    }

    visited[row][col] = true;
    dfs(labyrinth, row - 1, col, visited);
    dfs(labyrinth, row + 1, col, visited);
    dfs(labyrinth, row, col - 1, visited);
    dfs(labyrinth, row, col + 1, visited);
}

