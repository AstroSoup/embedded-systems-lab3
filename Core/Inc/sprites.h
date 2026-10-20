#include "stm32f4xx_hal.h"
#include "oled.h"

#ifndef Sprites
#define Sprites


typedef struct {
	const uint8_t SpriteWidth;
	const uint8_t SpriteHeight;
	const uint16_t *data;
} SpriteDef;

typedef enum {
	CELL_EMPTY = 0,
	CELL_I,
	CELL_O,
	CELL_T,
	CELL_S,
	CELL_Z,
	CELL_J,
	CELL_L,
	CELL_WALL,
	CELL_GHOST
} TetrisCell;

typedef enum {
	PIECE_I = 0,
	PIECE_O,
	PIECE_T,
	PIECE_S,
	PIECE_Z,
	PIECE_J,
	PIECE_L
} TetrisPiece;

extern SpriteDef Sprite_6x6;
extern SpriteDef Sprite_3x3;


extern const uint16_t Tetromino_Shapes[7][4];


typedef struct {
	const uint8_t Width;
	const uint8_t Height;
	const uint8_t *data;
} BitmapDef;

extern BitmapDef Sprite_Title;         /* ASTROTETRIS                         */
extern BitmapDef Sprite_BtnPlay;       /* кнопка Play                         */
extern BitmapDef Sprite_BtnHighScore;  /* кнопка High score                   */

void oled_DrawBitmap(uint8_t x, uint8_t y, BitmapDef bmp, OLED_COLOR color);

void oled_DrawSprite(uint8_t x, uint8_t y, SpriteDef sprite, uint8_t index, OLED_COLOR color);

void oled_DrawTetromino(uint8_t x, uint8_t y, SpriteDef sprite, TetrisPiece piece, uint8_t rotation);

#endif
