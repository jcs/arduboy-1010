/*
 * 1010 for Arduboy
 * Copyright (c) 2018 joshua stein <jcs@jcs.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <EEPROM.h>
#include <SPI.h>

#include <Arduboy2.h>

Arduboy2 arduboy;

/* this many tiles wide and tall */
const byte BOARD_SIZE = 10;

/* pixel position of board */
const byte BOARD_X = 66;
const byte BOARD_Y = 1;

/* first two bytes of eeprom set to this to indicate a saved game */
const byte SAVEFLAG = 254;

/* pieces to choose from in each round */
const byte ONDECK = 3;

/* wait this many milliseconds between auto-repeat of d-pad buttons */
const unsigned int AUTOREPEAT1 = 300;
const unsigned int AUTOREPEATN = 75;

/* screen backlight levels */
const byte SCREEN_MAX = 255;
const byte SCREEN_MIN = 10;

/* game state, written out to eeprom as saved game */
struct __attribute__((packed)) game_data {
	byte saveflag0, saveflag1;
	unsigned long hiscore, score;
	byte ondeck[ONDECK];
	byte ondecksel;
	byte curshape;
	byte x, y;
	byte over;
	byte board[BOARD_SIZE][BOARD_SIZE];
} game;

/* possible tile modes */
enum {
	EMPTY,
	TAKEN,
	PLACING,
	SWEEPING,
};

/* tile pixmaps */
const byte TILE_SIZE = 7;
const byte BLOCK_SIZE = TILE_SIZE - 1;
const byte colors[][TILE_SIZE] PROGMEM = {
	{	/* EMPTY */
		B1111111,
		B1000001,
		B1000001,
		B1000001,
		B1000001,
		B1000001,
		B1111111,
	},
	{	/* TAKEN */
		B1111111,
		B1010101,
		B1101011,
		B1010101,
		B1101011,
		B1010101,
		B1111111,
	},
	{
		/* PLACING */
		B1111111,
		B1111111,
		B1111111,
		B1111111,
		B1111111,
		B1111111,
		B1111111,
	},
};

/* tiny tile pixmap to use for ondeck pieces */
const byte PREVIEW_SIZE = 4;
const byte preview_color[PREVIEW_SIZE] PROGMEM = {
	B1111,
	B1001,
	B1001,
	B1111,
};

/* tile shapes */
typedef struct {
	const byte width, height, weight;
	const byte tiles[9];
} shape;

const shape shapes[] = {
	{ 0, 0, 0, { // empty
		0,
		}
	},
	{ 1, 1, 10, { // dot
		1,
		}
	},
	{ 2, 2, 10, { // 2x2 square
		1, 1,
		1, 1,
		}
	},
	{ 3, 3, 1, { // 3x3 square
		1, 1, 1,
		1, 1, 1,
		1, 1, 1,
		}
	},
	{ 2, 1, 10, { // 2x1 horizontal
		1, 1,
		}
	},
	{ 3, 1, 10, { // 3x1 horizontal
		1, 1, 1,
		}
	},
	{ 4, 1, 10, { // 4x1 horizontal
		1, 1, 1, 1,
		}
	},
	{ 5, 1, 1, { // 5x1 horizontal
		1, 1, 1, 1, 1,
		}
	},
	{ 1, 2, 10, { // 1x2 vertical
		1,
		1,
		}
	},
	{ 1, 3, 10, { // 1x3 vertical
		1,
		1,
		1,
		}
	},
	{ 1, 4, 10, { // 1x4 vertical
		1,
		1,
		1,
		1,
		}
	},
	{ 1, 5, 1, { // 1x5 vertical
		1,
		1,
		1,
		1,
		1,
		}
	},
	{ 2, 2, 10, { // 2x2 elbow
		1, 1,
		0, 1,
		}
	},
	{ 2, 2, 10, { // 2x2 elbow
		1, 0,
		1, 1,
		}
	},
	{ 2, 2, 10, { // 2x2 elbow
		1, 1,
		1, 0,
		}
	},
	{ 2, 2, 10, { // 2x2 elbow
		0, 1,
		1, 1,
		}
	},
	{ 3, 3, 10, { // 3x3 elbow
		1, 1, 1,
		0, 0, 1,
		0, 0, 1,
		}
	},
	{ 3, 3, 10, { // 3x3 elbow
		1, 0, 0,
		1, 0, 0,
		1, 1, 1,
		}
	},
	{ 3, 3, 10, { // 3x3 elbow
		1, 1, 1,
		1, 0, 0,
		1, 0, 0,
		}
	},
	{ 3, 3, 10, { // 3x3 elbow
		0, 0, 1,
		0, 0, 1,
		1, 1, 1,
		}
	},
};

void	backlight(int);
void	new_game(void);
void	save_game(void);
uint8_t	pressed_dpad_autorepeat(void);
bool	place_shape(void);
bool	any_moves_left(void);
void	draw_screen(void);
void	draw_board(void);
void	draw_shape(const shape *s, const byte, const byte, const byte,
	    const byte[]);
byte	left_ondeck(void);
void	new_shapes(void);
bool	shape_clear(const shape *s, const byte, const byte);
bool	move_to_clear(const shape *s);
void	game_over(void);

/* these don't need to be preserved in game state */
static byte leds[3] = { 0 };
static int8_t ledsweep[3] = { 0 };
static unsigned long dpad_millis_next = 0;
static bool dpad_repeatn = false;
static int screen_backlight = SCREEN_MAX;
static int shape_weights = 0;

void
setup(void)
{
	/* do arduboy.begin() without bootLogo() and LED flashing */
	arduboy.boot();
	arduboy.display();
	arduboy.flashlight();
	arduboy.systemButtons();
	arduboy.waitNoButtons();

	arduboy.initRandomSeed();

	/*
	 * setup screen backlight adjustment
	 * https://community.arduboy.com/t/screen-brightness/2662/22
	 */
	arduboy.LCDCommandMode();
	SPI.transfer(0xd9);
	SPI.transfer(0x2f);
	SPI.transfer(0xdb);
	SPI.transfer(0x00);
	SPI.transfer(0x81);
	SPI.transfer(screen_backlight);
	arduboy.LCDDataMode();

	if (EEPROM.read(0) == SAVEFLAG && EEPROM.read(1) == SAVEFLAG) {
		EEPROM.get(0, game);

		if (game.over)
			new_game();
		else if (!any_moves_left())
			game_over();
	}
	else
		new_game();

	arduboy.fillRect(0, 0, BOARD_X - 1, 64, BLACK);
	draw_screen();
}

void
backlight(int level)
{
	if (level < SCREEN_MIN)
		level = SCREEN_MIN;
	else if (level > SCREEN_MAX)
		level = SCREEN_MAX;

	arduboy.LCDCommandMode();
	SPI.transfer(0x81);
	SPI.transfer(level);
	arduboy.LCDDataMode();

	screen_backlight = level;
}

void
new_game(void)
{
	unsigned long oldhi = game.hiscore;

	memset(&game, 0, sizeof(game_data));
	game.hiscore = oldhi;

	memset(&leds, 0, sizeof(leds));
	memset(&ledsweep, 0, sizeof(ledsweep));
	game.saveflag0 = SAVEFLAG;
	game.saveflag1 = SAVEFLAG;

	arduboy.setRGBled(0, 0, 0);

	new_shapes();

	save_game();
}

void
save_game(void)
{
	EEPROM.put(0, game);
}

uint8_t
pressed_dpad_autorepeat(void)
{
	bool left = false;
	bool right = false;
	bool up = false;
	bool down = false;
	uint8_t ret = 0;

	/*
	 * arduboy.pressed() checks for all passed buttons at once, so check
	 * each individually
	 */
	left = arduboy.pressed(LEFT_BUTTON);
	right = arduboy.pressed(RIGHT_BUTTON);
	up = arduboy.pressed(UP_BUTTON);
	down = arduboy.pressed(DOWN_BUTTON);

	ret = (left ? LEFT_BUTTON : 0) | (right ? RIGHT_BUTTON : 0) |
	    (up ? UP_BUTTON : 0) | (down ? DOWN_BUTTON : 0);

	if (ret == 0) {
		dpad_millis_next = 0;
		dpad_repeatn = false;
		return 0;
	}

	if (dpad_millis_next == 0) {
		/* first pressing of buttons, return immediately */
		dpad_millis_next = millis() + AUTOREPEAT1;
		dpad_repeatn = true;
		return ret;
	}

	if (millis() >= dpad_millis_next) {
		dpad_millis_next = millis() + (dpad_repeatn ? AUTOREPEATN :
		    AUTOREPEAT1);
		return ret;
	}

	/* otherwise we're waiting until dpad_millis_next */
	return 0;
}

void
game_over(void)
{
	game.over = true;
	ledsweep[0] = 1;

	arduboy.fillRect(0, 38, BOARD_X - 1, 66, BLACK);

	arduboy.setCursor(0, 50);
	arduboy.print(F("Game Over"));

	arduboy.display();
	save_game();
}

void
loop(void)
{
	const shape *s;
	bool redraw = false;
	int8_t x;
	uint8_t dpad;

	if (!arduboy.nextFrame())
		return;

	arduboy.pollButtons();

	/* command mode */
	if (arduboy.pressed(A_BUTTON + B_BUTTON)) {
		if (arduboy.justPressed(DOWN_BUTTON))
			backlight(screen_backlight - 20);
		else if (arduboy.justPressed(UP_BUTTON))
			backlight(screen_backlight + 20);
		else if (arduboy.justPressed(LEFT_BUTTON) ||
		    arduboy.justPressed(RIGHT_BUTTON)) {
			new_game();
			draw_screen();
		}

		return;
	}

	if (game.over) {
		/* if we achieved the high score, pulse green, else red */
		x = (game.score == game.hiscore ? 1 : 0);

		if (ledsweep[x] == 1) {
			if (leds[x] >= 50)
				ledsweep[x] = -1;
			else
				leds[x]++;
		} else {
			if (leds[x] <= 10)
				ledsweep[x] = 1;
			else
				leds[x]--;
		}

		arduboy.setRGBled(leds[0], leds[1], leds[2]);

		if (arduboy.justPressed(A_BUTTON) ||
		    arduboy.justPressed(B_BUTTON)) {
			new_game();
			draw_screen();
			return;
		}
	}

	if (game.curshape == 0) {
		/* selecting an ondeck piece */
		if (arduboy.justPressed(LEFT_BUTTON)) {
			for (x = game.ondecksel - 1; x >= 0; x--) {
				if (game.ondeck[x]) {
					game.ondecksel = x;
					redraw = true;
					break;
				}
			}
		} else if (arduboy.justPressed(RIGHT_BUTTON)) {
			for (x = game.ondecksel + 1; x < ONDECK; x++) {
				if (game.ondeck[x]) {
					game.ondecksel = x;
					redraw = true;
					break;
				}
			}
		} else if (arduboy.justPressed(A_BUTTON) ||
		    arduboy.justPressed(B_BUTTON)) {
			game.curshape = game.ondeck[game.ondecksel];
			game.ondeck[game.ondecksel] = 0;

			if (!move_to_clear(&shapes[game.curshape]) &&
			    left_ondeck() == 0) {
				game_over();
				return;
			}

			redraw = true;
		}
	} else {
		/* moving the selected piece around on the board */
		s = &shapes[game.curshape];

		if (arduboy.justPressed(A_BUTTON)) {
			if (place_shape())
				redraw = true;
			else {
				arduboy.setRGBled(50, 0, 0);
				delay(50);
				arduboy.setRGBled(leds[0], leds[1], leds[2]);
			}
		} else if (arduboy.justPressed(B_BUTTON)) {
			/* put the piece back on deck */
			game.ondeck[game.ondecksel] = game.curshape;
			game.curshape = 0;
			redraw = true;
		} else {
			dpad = pressed_dpad_autorepeat();
			redraw = true;

			if ((dpad & UP_BUTTON) && (game.y > 0))
				game.y--;
			else if ((dpad & DOWN_BUTTON) &&
			    (game.y < BOARD_SIZE - s->height))
				game.y++;
			else if ((dpad & LEFT_BUTTON) && (game.x > 0))
				game.x--;
			else if ((dpad & RIGHT_BUTTON) &&
			    (game.x < BOARD_SIZE - s->width))
				game.x++;
			else
				redraw = false;
		}
	}

	if (redraw)
		draw_screen();
}

void
draw_screen(void)
{
	const shape *s = NULL;
	const shape *ts = NULL;
	int i;
	byte x;

	draw_board();

	if (game.curshape > 0 && !game.over) {
		s = &shapes[game.curshape];

		draw_shape(s, BOARD_X + (game.x * BLOCK_SIZE),
		    BOARD_Y + (game.y * BLOCK_SIZE), BLOCK_SIZE,
		    colors[PLACING]);
	}

	arduboy.setCursor(0, 0);
	arduboy.print(F("Score\n"));
	arduboy.print(game.score);

	arduboy.setCursor(0, 20);
	arduboy.print(F("Hi-Score\n"));
	arduboy.print(game.hiscore);

	/* previews */
	for (x = 0; x < sizeof(game.ondeck); x++) {
		ts = &shapes[game.ondeck[x]];
		if (ts == 0)
			continue;

		i = (((BOARD_X - 4) / ONDECK) * (x + 1)) -
		    ((BOARD_X - 4) / (ONDECK * 2)) + x;

		draw_shape(ts,
		    i - ((ts->width * PREVIEW_SIZE) / 2),
		    48 - ((ts->height * PREVIEW_SIZE) / 2),
		    PREVIEW_SIZE, preview_color);

		if (x == game.ondecksel && !game.curshape) {
			arduboy.drawPixel(i, 60, WHITE);
			arduboy.drawLine(i - 1, 61, i + 1, 61, WHITE);
			arduboy.drawLine(i - 2, 62, i + 2, 62, WHITE);
		}
	}

	arduboy.display();
}

void
draw_board(void)
{
	byte x, y;

	arduboy.clear();

	for (y = 0; y < BOARD_SIZE; y++)
		for (x = 0; x < BOARD_SIZE; x++)
			arduboy.drawBitmap(BOARD_X + (x * BLOCK_SIZE),
			    BOARD_Y + (y * BLOCK_SIZE),
			    colors[game.board[x][y]],
			    TILE_SIZE, TILE_SIZE, WHITE);

	arduboy.drawLine(BOARD_X + 1,
	    BOARD_Y + (BOARD_SIZE * BLOCK_SIZE) + 1,
	    BOARD_X + (BOARD_SIZE * BLOCK_SIZE) + 1,
	    BOARD_Y + (BOARD_SIZE * BLOCK_SIZE) + 1,
	    WHITE);
	arduboy.drawLine(BOARD_X + (BOARD_SIZE * BLOCK_SIZE) + 1,
	    BOARD_Y + 1,
	    BOARD_X + (BOARD_SIZE * BLOCK_SIZE) + 1,
	    BOARD_Y + (BOARD_SIZE * BLOCK_SIZE) + 1,
	    WHITE);
}

byte
left_ondeck(void)
{
	byte x, od = 0;

	for (x = 0; x < ONDECK; x++) {
		if (game.ondeck[x] != 0)
			od++;
	}

	return od;
}

void
new_shapes(void)
{
	byte x, j, r;

	if (left_ondeck())
		return;

	if (!shape_weights) {
		for (x = 0; x < sizeof(shapes) / sizeof(shape); x++)
			shape_weights += shapes[x].weight;
	}

	for (j = 0; j < ONDECK; j++) {
		r = random(1, shape_weights);

		for (x = 0; x < sizeof(shapes) / sizeof(shape); x++) {
			if (r < shapes[x].weight) {
				game.ondeck[j] = x;
				break;
			}
			r -= shapes[x].weight;
		}
	}
}

bool
shape_clear(const shape *s, const byte x, const byte y)
{
	byte tx, ty;

	if (x + s->width > BOARD_SIZE || y + s->height > BOARD_SIZE)
		return false;

	for (ty = 0; ty < s->height; ty++)
		for (tx = 0; tx < s->width; tx++)
			if (s->tiles[tx + (ty * s->width)] != EMPTY &&
			    game.board[x + tx][y + ty] != EMPTY)
				return false;

	return true;
}

bool
move_to_clear(const shape *s)
{
	for (game.y = 0; game.y <= BOARD_SIZE - s->height; game.y++)
		for (game.x = 0; game.x <= BOARD_SIZE - s->width; game.x++)
			if (shape_clear(s, game.x, game.y))
				return true;

	/* make sure we're not hanging off the bottom corner of the board */
	game.x = game.y = 0;

	return false;
}

void
draw_shape(const shape *s, const byte x, const byte y, const byte size,
    const byte color[])
{
	byte tx, ty;

	/* walk the shape, stamping its tile in each non-zero spot */
	for (ty = 0; ty < s->height; ty++)
		for (tx = 0; tx < s->width; tx++)
			if (s->tiles[tx + (ty * s->width)])
				arduboy.drawBitmap(x + (tx * size),
				    y + (ty * size), color, size, size,
				    WHITE);
}

void
explode_swept(void)
{
	byte i, x, y;

	for (i = 1; i < TILE_SIZE; i++) {
		for (x = 0; x < BOARD_SIZE; x++)
			for (y = 0; y < BOARD_SIZE; y++) {
				if (game.board[x][y] != SWEEPING)
					continue;

				arduboy.drawRect(BOARD_X + (x * BLOCK_SIZE) + i,
				    BOARD_Y + (y * BLOCK_SIZE) + i,
				    BLOCK_SIZE - i, BLOCK_SIZE - i,
				    BLACK);
			}

		arduboy.display();
		delay(40 + (i * 10));
	}

	for (x = 0; x < BOARD_SIZE; x++)
		for (y = 0; y < BOARD_SIZE; y++)
			if (game.board[x][y] == SWEEPING)
				game.board[x][y] = EMPTY;
}

bool
any_moves_left(void)
{
	int x;

	for (x = 0; x < ONDECK; x++) {
		if (game.ondeck[x] != 0 &&
		    move_to_clear(&shapes[game.ondeck[x]])) {
			return true;
		}
	}

	return false;
}

bool
place_shape(void)
{
	const shape *s = &shapes[game.curshape];
	byte x, y;
	int c;
	bool swept = false;

	if (!shape_clear(s, game.x, game.y))
		return false;

	for (y = 0; y < s->height; y++) {
		for (x = 0; x < s->width; x++) {
			if (s->tiles[x + (y * s->width)] != EMPTY) {
				game.board[game.x + x][game.y + y] =
				    s->tiles[x + (y * s->width)];

				game.score++;
			}
		}
	}

	/*
	 * Sweep through each row, then each column, and mark any completed
	 * rows or columns as such.  Then sweep through again and clear them
	 * out.
	 *
	 * This must be done twice because otherwise when a column and row are
	 * both complete, clearing out one would break the completion of the
	 * other one.
	 */
	for (y = 0; y < BOARD_SIZE; y++) {
		for (x = 0, c = 0; x < BOARD_SIZE; x++)
			if (game.board[x][y] != EMPTY)
				c++;

		if (c == BOARD_SIZE) {
			/* complete row, empty */
			for (x = 0; x < BOARD_SIZE; x++) {
				game.board[x][y] = SWEEPING;
				game.score++;
				swept = true;
			}
		}
	}

	for (x = 0; x < BOARD_SIZE; x++) {
		for (y = 0, c = 0; y < BOARD_SIZE; y++)
			if (game.board[x][y] != EMPTY)
				c++;

		if (c == BOARD_SIZE) {
			/* complete column, empty */
			for (y = 0; y < BOARD_SIZE; y++) {
				game.board[x][y] = SWEEPING;
				game.score++;
				swept = true;
			}
		}
	}

	if (swept)
		explode_swept();

	if (game.score > game.hiscore)
		game.hiscore = game.score;

	game.curshape = 0;
	new_shapes();

	/* auto-highlight the next available on-deck piece */
	for (x = 0; x < ONDECK; x++)
		if (game.ondeck[x] != 0) {
			game.ondecksel = x;
			break;
		}

	save_game();

	if (!any_moves_left()) {
		game_over();
		return false;
	}

	return true;
}
