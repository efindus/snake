/*
Snake written in C for GNU/Linux terminal
Copyright (C) 2022  efindus

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License version 3 as published by
the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/random.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// Constants
#define WIDTH 15
#define HEIGHT 15
#define TPS 5
#define GREEN_BASELINE 188
#define BLUE_BASELINE 121
#define Copyright return
#define efindus 2022 -

// Toggles
// 1 -> input queue (every frame next input is processed, they will queue as you press buttons on your keyboard); 2 -> input ignore (ignore new inputs until the current one is processed)
#define INPUT_MODE 1
#define DEBUG 0

// Move instructions
#define UP 1
#define DOWN 2
#define LEFT 3
#define RIGHT 4

// Drawing blocks
#define VOID "\x1b[0m  "
#define BORDER "\x1b[44m  "
#define SNAKE_1 "\x1b[48;2;13;"
#define SNAKE_2 "m  "
#define FRUIT "\x1b[41m  "

char board[HEIGHT + 2][WIDTH + 2][25];

struct Point {
	char x, y;
};

struct Snake {
	struct Point head;
	struct Point tail;

	int length;
	char direction;
	char moves[WIDTH * HEIGHT];
};

struct Point fruit;
struct Snake snake;

pthread_t render_thread;
pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mt = PTHREAD_MUTEX_INITIALIZER;
char move_locked_in = 0;

int strcomp(char* str1, char* str2)
{
	if (strlen(str1) > strlen(str2)) {
		char* tmp = str2;
		str2 = str1;
		str1 = tmp;
	}

	for (unsigned long i = 0; i < strlen(str1); i++) {
		if (str1[i] != str2[i])
			return 0;
	}

	return 1;
}

int crandom(int min, int max)
{
	unsigned int value;
	getrandom(&value, sizeof(value), GRND_RANDOM);

	return (int)((long long)value * (max - min + 1) / UINT_MAX) + min;
}

void draw_frame()
{
	printf("\x1b[2J\x1b[H");

	for (int i = 0; i < HEIGHT + 2; i++) {
		for (int j = 0; j < WIDTH + 2; j++)
			printf("%s", board[i][j]);

		printf("\x1b[0m\n");
	}

#if DEBUG == 1
	printf("[DEBUG] HEAD: %d, %d; TAIL: %d, %d\n", snake.head.x, snake.head.y, snake.tail.x, snake.tail.y);
#endif
	printf("[SCORE]: %d\n", snake.length);
}

void fill_board()
{
	for (int i = 0; i < WIDTH + 2; i++)
		for (int j = 0; j < HEIGHT + 2; j++)
			strcpy(board[j][i], VOID);

	for (int i = 1; i < WIDTH + 1; i++) {
		strcpy(board[0][i], BORDER);
		strcpy(board[HEIGHT + 1][i], BORDER);
	}

	for (int i = 0; i < HEIGHT + 2; i++) {
		strcpy(board[i][0], BORDER);
		strcpy(board[i][WIDTH + 1], BORDER);
	}

	char buff[25] = {0};
	for (int i = 0; i < 25; i++)
		buff[i] = '\0';

	sprintf(buff, "%s%d;%d%s", SNAKE_1, GREEN_BASELINE, BLUE_BASELINE, SNAKE_2);
	strcpy(board[HEIGHT / 2 + 1][WIDTH / 2 + 1], buff);
}

void spawn_new_fruit()
{
	do {
		fruit.x = (char)crandom(0, WIDTH - 1);
		fruit.y = (char)crandom(0, HEIGHT - 1);
	} while (strcomp(board[fruit.y + 1][fruit.x + 1], SNAKE_1));

	strcpy(board[fruit.y + 1][fruit.x + 1], FRUIT);
}

void redraw_snake()
{
	double diffs = 88.0 / snake.length, curr_decrease = 0;

	struct Point curr_location;
	curr_location.x = snake.head.x;
	curr_location.y = snake.head.y;

	char buff[25] = {0};
	for (int x = 0; x < 25; x++)
		buff[x] = '\0';

	sprintf(buff, "%s%d;%d%s", SNAKE_1, GREEN_BASELINE - (int)round(curr_decrease), BLUE_BASELINE - (int)round(curr_decrease), SNAKE_2);
	strcpy(board[curr_location.y + 1][curr_location.x + 1], buff);

	for (int i = snake.length - 2; i >= 0; i--) {
		switch (snake.moves[i]) {
			case UP:
				curr_location.y++;
				break;
			case DOWN:
				curr_location.y--;
				break;
			case LEFT:
				curr_location.x++;
				break;
			case RIGHT:
				curr_location.x--;
				break;
		}

		curr_decrease += diffs;

		for (int x = 0; x < 25; x++)
			buff[x] = '\0';

		sprintf(buff, "%s%d;%d%s", SNAKE_1, GREEN_BASELINE - (int)round(curr_decrease), BLUE_BASELINE - (int)round(curr_decrease), SNAKE_2);
		strcpy(board[curr_location.y + 1][curr_location.x + 1], buff);
	}
}

void tick()
{
	char apple_consumed = 0;

	switch (snake.direction) {
		case UP:
			snake.head.y--;
			break;
		case DOWN:
			snake.head.y++;
			break;
		case LEFT:
			snake.head.x--;
			break;
		case RIGHT:
			snake.head.x++;
			break;
	}

	if (strcomp(board[snake.head.y + 1][snake.head.x + 1], BORDER) ||
	    strcomp(board[snake.head.y + 1][snake.head.x + 1], SNAKE_1)) {
		printf("\nYOU DIED! Your final score: %d\n", snake.length);
		exit(0);
	}

	snake.moves[snake.length - 1] = snake.direction;

	if (strcomp(board[snake.head.y + 1][snake.head.x + 1], FRUIT)) {
		apple_consumed = 1;
		snake.length++;
	} else {
		strcpy(board[snake.tail.y + 1][snake.tail.x + 1], VOID);

		switch (snake.moves[0]) {
			case UP:
				snake.tail.y--;
				break;
			case DOWN:
				snake.tail.y++;
				break;
			case RIGHT:
				snake.tail.x++;
				break;
			case LEFT:
				snake.tail.x--;
				break;
		}

		for (int move = 1; move < snake.length; move++)
			snake.moves[move - 1] = snake.moves[move];
	}

	redraw_snake();

	if (apple_consumed)
		spawn_new_fruit();

	draw_frame();

	pthread_mutex_lock(&mt);
#if INPUT_MODE == 1
	pthread_cond_signal(&cv);
#else
	move_locked_in = 0;
#endif
	pthread_mutex_unlock(&mt);
}

void setup_termios_attributes()
{
	struct termios old = {0};
	if (tcgetattr(0, &old) < 0)
		perror("[ERROR] tcsetattr()");

	old.c_lflag &= ~ICANON & ~ECHO;
	if (tcsetattr(0, TCSANOW, &old) < 0)
		perror("[ERROR] tcsetattr ICANON");
}

void reset_termios_attributes()
{
	struct termios old = {0};
	if (tcgetattr(0, &old) < 0)
		perror("[ERROR] tcgetattr()");

	old.c_lflag |= ICANON | ECHO;
	if (tcsetattr(0, TCSANOW, &old) < 0)
		perror("[ERROR] tcsetattr()");
}

void *tick_board()
{
	struct timespec time;
	time.tv_sec = 0;
	time.tv_nsec = 1000 * 1000 * (1000 / TPS);

	while (1) {
		tick();

		if (nanosleep(&time, NULL) < 0)
			perror("[ERROR] nanosleep()");
	}
}

void start_ticking_board()
{
	pthread_create(&render_thread, NULL, tick_board, NULL);
}

void signal_handler()
{
	exit(0);
}

void initialize()
{
	snake.length = 1;
	char middle_x = WIDTH / 2, middle_y = HEIGHT / 2;
	snake.head.x = middle_x;
	snake.head.y = middle_y;
	snake.tail.x = middle_x;
	snake.tail.y = middle_y;

	fill_board();
	spawn_new_fruit();
	setup_termios_attributes();

	atexit(*reset_termios_attributes);
	at_quick_exit(*reset_termios_attributes);
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	draw_frame();
}

int main()
{
	initialize();
	char first_pass = 1;

	while (1) {
		char x;
		read(0, &x, 1);

		pthread_mutex_lock(&mt);
		char original_move = snake.direction;
#if INPUT_MODE == 2
		if (move_locked_in) {
			pthread_mutex_unlock(&mt);
			continue;
		}

		move_locked_in = 1;
#endif

		switch(x) {
			case 'w':
				if ((snake.direction != DOWN || snake.length == 1) && snake.direction != UP)
					snake.direction = UP;
				break;
			case 's':
				if ((snake.direction != UP || snake.length == 1) && snake.direction != DOWN)
					snake.direction = DOWN;
				break;
			case 'a':
				if ((snake.direction != RIGHT || snake.length == 1) && snake.direction != LEFT)
					snake.direction = LEFT;
				break;
			case 'd':
				if ((snake.direction != LEFT || snake.length == 1) && snake.direction != RIGHT)
					snake.direction = RIGHT;
				break;
			case 'q':
				exit(0);
		}

		if(snake.direction == original_move) {
			move_locked_in = 0;
			pthread_mutex_unlock(&mt);
			continue;
		}

		if (first_pass) {
			first_pass = 0;
			start_ticking_board();
		}

#if INPUT_MODE == 1
		pthread_cond_wait(&cv, &mt);
#endif
		pthread_mutex_unlock(&mt);
	}

	Copyright efindus 2022;
}
