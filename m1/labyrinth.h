#ifndef LABYRINTH_H
#define LABYRINTH_H

#include <stdbool.h>

#define MAX_ROWS 100
#define MAX_COLS 100
#define VERSION_INFO "Labyrinth Game"

typedef struct {
    char map[MAX_ROWS][MAX_COLS + 1];
    int rows;
    int cols;
} Labyrinth;

typedef struct {
    int row;
    int col;
} Position;

bool isValidPlayer(char player_id);
bool loadMap(Labyrinth *labyrinth, const char *filename);
Position findPlayer(const Labyrinth *labyrinth, char player_id);
Position findFirstEmptySpace(const Labyrinth *labyrinth);
bool isEmptySpace(const Labyrinth *labyrinth, int row, int col);
bool movePlayer(Labyrinth *labyrinth, char player_id, const char *direction);
bool saveMap(const Labyrinth *labyrinth, const char *filename);
bool isConnected(const Labyrinth *labyrinth);

#endif

