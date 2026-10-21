/*
 * Тетрис для экрана Play.
 *
 * Экран 128x64: слева стакан 10x20 (спрайты 3x3), справа окно со следующей
 * фигурой (спрайты 6x6) и под ним текущий score.
 *
 * Правила - по Tetris Guideline: вращение SRS с wall kick, 7-bag, появление
 * фигур в скрытых строках над стаканом, призрак, lock delay 0.5 c со сбросом
 * при движении (до 15 раз), гравитация по уровням с ускорением по времени партии,
 * очки за линии и за дропы.
 *
 * Клавиши (строка, столбец, нумерация с нуля):
 *   (1,0) - поворот против часовой     (1,2) - поворот по часовой
 *   (1,1) - мгновенная установка (hard drop)
 *   (2,1) - ускоренное падение (soft drop), пока удерживается
 *   (2,0) - влево                      (2,2) - вправо (с автоповтором)
 */
#include <string.h>
#include "main.h"
#include "cmsis_os.h"
#include "oled.h"
#include "sprites.h"
#include "menu.h"
#include "tetris.h"

/* ---------- Раскладка клавиш ---------- */
#define KEY_ROTATE_CCW      KEY_ID(1, 0)
#define KEY_HARD_DROP       KEY_ID(1, 1)
#define KEY_ROTATE_CW       KEY_ID(1, 2)
#define KEY_SOFT_DROP       KEY_ID(2, 1)
#define KEY_MOVE_LEFT       KEY_ID(2, 0)
#define KEY_MOVE_RIGHT      KEY_ID(2, 2)

#define KEYBIT(id)          ((uint16_t)(1u << (id)))

/* ---------- Параметры игры ---------- */
#define BOARD_COLS          10
#define BOARD_VISIBLE       20
#define BOARD_HIDDEN        2                       /* строки над стаканом */
#define BOARD_ROWS          (BOARD_VISIBLE + BOARD_HIDDEN)

#define SPAWN_X             3                       /* левый край рамки 4x4 */
#define SPAWN_Y             0

#define TETRIS_FRAME_MS         10
#define TETRIS_DAS_MS           170                 /* задержка до автоповтора */
#define TETRIS_ARR_MS           50                  /* период автоповтора */
#define TETRIS_LOCK_DELAY_MS    500
#define TETRIS_LOCK_MAX_RESETS  15
#define TETRIS_SOFT_FACTOR      20                  /* soft drop быстрее гравитации в 20 раз */
#define TETRIS_LINES_PER_LEVEL  10
#define TETRIS_SPEEDUP_PERIOD_MS 20000              /* ступень ускорения раз в 20 с партии */
#define TETRIS_SPEEDUP_PCT      8                   /* каждая ступень: +8% к скорости падения */
#define TETRIS_SPEEDUP_MAX_STEPS 30                 /* предел: скорость x3.4 к базовой */
#define TETRIS_MAX_LEVEL        15
#define TETRIS_MAX_SCORE        999999UL

/* ---------- Раскладка на экране ---------- */
#define WELL_CELL_PX        3                       /* размер Sprite_3x3 */
#define WELL_X              4                       /* левая стенка стакана */
#define WELL_Y              0
#define WELL_CELLS_X        (WELL_X + WELL_CELL_PX)
#define WELL_RIGHT_X        (WELL_CELLS_X + BOARD_COLS * WELL_CELL_PX)
#define WELL_FLOOR_Y        (WELL_Y + BOARD_VISIBLE * WELL_CELL_PX)
#define WELL_FLUSH_X1       WELL_CELLS_X
#define WELL_FLUSH_X2       (WELL_CELLS_X + BOARD_COLS * WELL_CELL_PX - 1)

#define PANEL_X1            44                      /* правая панель: 44..127 */
#define PANEL_CX            86
#define NEXT_FRAME_X1       (PANEL_CX - 20)
#define NEXT_FRAME_X2       (PANEL_CX + 19)
#define NEXT_FRAME_Y1       11
#define NEXT_FRAME_Y2       36
#define NEXT_IN_X1          (NEXT_FRAME_X1 + 1)
#define NEXT_IN_X2          (NEXT_FRAME_X2 - 1)
#define NEXT_IN_Y1          (NEXT_FRAME_Y1 + 1)
#define NEXT_IN_Y2          (NEXT_FRAME_Y2 - 1)
#define NEXT_CELL_PX        6                       /* размер Sprite_6x6 */
#define SCORE_LABEL_Y       41
#define SCORE_VALUE_Y       52
#define FONT_W              7                       /* Font_7x10 */
#define FONT_H              10

/* ---------- Состояние игры ---------- */
typedef struct {
	uint8_t type;           /* TetrisPiece */
	uint8_t rot;            /* 0, R, 2, L */
	int8_t  x;              /* левый верхний угол рамки 4x4 в координатах поля */
	int8_t  y;
} Piece;

typedef struct {
	int8_t x;
	int8_t y;
} Kick;

static uint8_t  Board[BOARD_ROWS][BOARD_COLS];      /* 0 - пусто, иначе TetrisCell */
static Piece    Cur;
static uint8_t  NextType;
static uint8_t  Bag[7];
static uint8_t  BagPos;
static uint32_t Rng;

static uint32_t Score;
static uint32_t Lines;
static uint32_t Level;
static uint32_t StartAt;            /* начало партии, мс */
static uint32_t SpeedSteps;         /* ступени ускорения по времени */
static uint8_t  GameOver;

static uint32_t NextFallAt;
static uint32_t LockAt;
static uint8_t  LockActive;
static uint8_t  LockResets;
static int8_t   LowestY;
static int8_t   RepeatDir;
static uint32_t RepeatAt;

static uint8_t  DirtyWell;
static uint8_t  DirtyPanel;

/* Время до падения на одну клетку, мс, по уровням 1..15 (формула Guideline). */
static const uint16_t GravityMs[TETRIS_MAX_LEVEL] = {
	1000, 793, 618, 473, 355, 262, 190, 135, 94, 64, 43, 28, 18, 12, 8
};

static const uint16_t LineScore[5] = { 0, 100, 300, 500, 800 };

/* SRS wall kick. Индексы: [состояние до поворота][0 - по часовой, 1 - против][тест].
 * Смещения даны как в стандарте (ось y направлена вверх), при применении
 * y инвертируется. */
static const Kick KicksJLSTZ[4][2][5] = {
	{ /* 0 */
		{ {0,0}, {-1,0}, {-1, 1}, {0,-2}, {-1,-2} },   /* 0->R */
		{ {0,0}, { 1,0}, { 1, 1}, {0,-2}, { 1,-2} }    /* 0->L */
	},
	{ /* R */
		{ {0,0}, { 1,0}, { 1,-1}, {0, 2}, { 1, 2} },   /* R->2 */
		{ {0,0}, { 1,0}, { 1,-1}, {0, 2}, { 1, 2} }    /* R->0 */
	},
	{ /* 2 */
		{ {0,0}, { 1,0}, { 1, 1}, {0,-2}, { 1,-2} },   /* 2->L */
		{ {0,0}, {-1,0}, {-1, 1}, {0,-2}, {-1,-2} }    /* 2->R */
	},
	{ /* L */
		{ {0,0}, {-1,0}, {-1,-1}, {0, 2}, {-1, 2} },   /* L->0 */
		{ {0,0}, {-1,0}, {-1,-1}, {0, 2}, {-1, 2} }    /* L->2 */
	}
};

static const Kick KicksI[4][2][5] = {
	{ /* 0 */
		{ {0,0}, {-2,0}, { 1,0}, {-2,-1}, { 1, 2} },   /* 0->R */
		{ {0,0}, {-1,0}, { 2,0}, {-1, 2}, { 2,-1} }    /* 0->L */
	},
	{ /* R */
		{ {0,0}, {-1,0}, { 2,0}, {-1, 2}, { 2,-1} },   /* R->2 */
		{ {0,0}, { 2,0}, {-1,0}, { 2, 1}, {-1,-2} }    /* R->0 */
	},
	{ /* 2 */
		{ {0,0}, { 2,0}, {-1,0}, { 2, 1}, {-1,-2} },   /* 2->L */
		{ {0,0}, { 1,0}, {-2,0}, { 1,-2}, {-2, 1} }    /* 2->R */
	},
	{ /* L */
		{ {0,0}, { 1,0}, {-2,0}, { 1,-2}, {-2, 1} },   /* L->0 */
		{ {0,0}, {-2,0}, { 1,0}, {-2,-1}, { 1, 2} }    /* L->2 */
	}
};

/* Текущее время в мс по системному тику FreeRTOS (тик ядра = 1 мс,
 * configTICK_RATE_HZ = 1000). */
#define NOW_MS()    ((uint32_t)(osKernelSysTick() / (osKernelSysTickFrequency / 1000UL)))

/* Сравнение моментов времени с учётом переполнения счётчика. */
#define TIME_REACHED(now, t)    ((int32_t)((now) - (t)) >= 0)

/* ---------- Вспомогательное ---------- */

static uint32_t Rand(void) {
	Rng ^= Rng << 13;
	Rng ^= Rng >> 17;
	Rng ^= Rng << 5;
	return Rng;
}

/* 7-bag: фигуры выдаются случайными семёрками без повторов внутри семёрки. */
static uint8_t BagNext(void) {
	uint8_t i, j, t;

	if (BagPos >= 7) {
		for (i = 0; i < 7; i++) {
			Bag[i] = i;
		}
		for (i = 6; i > 0; i--) {
			j = Rand() % (i + 1);
			t = Bag[i];
			Bag[i] = Bag[j];
			Bag[j] = t;
		}
		BagPos = 0;
	}
	return Bag[BagPos++];
}

/* Интервал падения: базовый по уровню, сокращённый ступенями ускорения по времени. */
static uint16_t GravityInterval(void) {
	uint32_t lvl = (Level > TETRIS_MAX_LEVEL) ? TETRIS_MAX_LEVEL : Level;
	uint32_t ms = (uint32_t)GravityMs[lvl - 1] * 100
	              / (100 + TETRIS_SPEEDUP_PCT * SpeedSteps);

	return (ms < 1) ? 1 : (uint16_t)ms;
}

/* Пересчитывает ступень ускорения по времени, прошедшему с начала партии. */
static void UpdateSpeedup(uint32_t now) {
	uint32_t steps = (now - StartAt) / TETRIS_SPEEDUP_PERIOD_MS;

	SpeedSteps = (steps > TETRIS_SPEEDUP_MAX_STEPS) ? TETRIS_SPEEDUP_MAX_STEPS : steps;
}

static uint16_t SoftInterval(void) {
	uint16_t ms = GravityInterval() / TETRIS_SOFT_FACTOR;
	return (ms < 1) ? 1 : ms;
}

static void AddScore(uint32_t points) {
	Score += points;
	if (Score > TETRIS_MAX_SCORE) {
		Score = TETRIS_MAX_SCORE;
	}
	DirtyPanel = 1;
}

/* Пересекается ли фигура с границами стакана или занятыми клетками.
 * Клетки выше строки 0 считаются свободными. */
static uint8_t Collides(uint8_t type, uint8_t rot, int8_t px, int8_t py) {
	uint16_t shape = Tetromino_Shapes[type][rot & 3];
	uint8_t row, col;
	int8_t x, y;

	for (row = 0; row < 4; row++) {
		for (col = 0; col < 4; col++) {
			if (!(shape & (0x8000 >> (row * 4 + col)))) {
				continue;
			}
			x = px + col;
			y = py + row;
			if (x < 0 || x >= BOARD_COLS || y >= BOARD_ROWS) {
				return 1;
			}
			if (y >= 0 && Board[y][x]) {
				return 1;
			}
		}
	}
	return 0;
}

/* Стоит ли текущая фигура на опоре. Запускает/сбрасывает таймер lock delay.
 * manual - изменение вызвано игроком (сдвиг, поворот): тогда таймер
 * перезапускается, но не более TETRIS_LOCK_MAX_RESETS раз. */
static void UpdateGrounded(uint32_t now, uint8_t manual) {
	if (Collides(Cur.type, Cur.rot, Cur.x, Cur.y + 1)) {
		if (!LockActive) {
			LockActive = 1;
			LockAt = now + TETRIS_LOCK_DELAY_MS;
		} else if (manual && LockResets < TETRIS_LOCK_MAX_RESETS) {
			LockAt = now + TETRIS_LOCK_DELAY_MS;
			LockResets++;
		}
	} else {
		LockActive = 0;
	}
}

/* Достаёт следующую фигуру из очереди и ставит её наверх стакана.
 * Возвращает 0, если место занято (block out - поражение). */
static uint8_t Spawn(uint32_t now) {
	Cur.type = NextType;
	Cur.rot = 0;
	Cur.x = SPAWN_X;
	Cur.y = SPAWN_Y;
	NextType = BagNext();
	DirtyPanel = 1;
	DirtyWell = 1;

	if (Collides(Cur.type, Cur.rot, Cur.x, Cur.y)) {
		return 0;
	}
	/* По Guideline сразу после появления фигура опускается на одну клетку. */
	if (!Collides(Cur.type, Cur.rot, Cur.x, Cur.y + 1)) {
		Cur.y++;
	}

	LowestY = Cur.y;
	LockResets = 0;
	LockActive = 0;
	NextFallAt = now + GravityInterval();
	UpdateGrounded(now, 0);
	return 1;
}

/* Один шаг вниз. Возвращает 0, если мешает опора. */
static uint8_t StepDown(uint32_t now) {
	if (Collides(Cur.type, Cur.rot, Cur.x, Cur.y + 1)) {
		return 0;
	}
	Cur.y++;
	if (Cur.y > LowestY) {
		LowestY = Cur.y;
		LockResets = 0;     /* новая нижняя точка: счётчик сбросов заново */
	}
	DirtyWell = 1;
	UpdateGrounded(now, 0);
	return 1;
}

static uint8_t TryShift(int8_t dx, uint32_t now) {
	if (Collides(Cur.type, Cur.rot, Cur.x + dx, Cur.y)) {
		return 0;
	}
	Cur.x += dx;
	DirtyWell = 1;
	UpdateGrounded(now, 1);
	return 1;
}

/* Поворот по SRS: dir > 0 - по часовой, иначе против. */
static uint8_t TryRotate(int8_t dir, uint32_t now) {
	const Kick *kicks;
	uint8_t newRot, k;
	int8_t nx, ny;

	if (Cur.type == PIECE_O) {
		return 0;           /* квадрат при повороте не меняется */
	}

	newRot = (Cur.rot + ((dir > 0) ? 1 : 3)) & 3;
	kicks = ((Cur.type == PIECE_I) ? KicksI : KicksJLSTZ)[Cur.rot][(dir > 0) ? 0 : 1];

	for (k = 0; k < 5; k++) {
		nx = Cur.x + kicks[k].x;
		ny = Cur.y - kicks[k].y;
		if (!Collides(Cur.type, newRot, nx, ny)) {
			Cur.rot = newRot;
			Cur.x = nx;
			Cur.y = ny;
			DirtyWell = 1;
			UpdateGrounded(now, 1);
			return 1;
		}
	}
	return 0;
}

static uint8_t ClearLines(void) {
	int8_t r, k;
	uint8_t c, full, cleared = 0;

	for (r = BOARD_ROWS - 1; r >= 0; r--) {
		full = 1;
		for (c = 0; c < BOARD_COLS; c++) {
			if (!Board[r][c]) {
				full = 0;
				break;
			}
		}
		if (!full) {
			continue;
		}
		for (k = r; k > 0; k--) {
			memcpy(Board[k], Board[k - 1], BOARD_COLS);
		}
		memset(Board[0], 0, BOARD_COLS);
		cleared++;
		r++;                /* на этом месте теперь новая строка - проверить её снова */
	}
	return cleared;
}

static void LockPiece(uint32_t now) {
	uint16_t shape = Tetromino_Shapes[Cur.type][Cur.rot & 3];
	uint8_t row, col, cleared, allHidden = 1;
	int8_t x, y;

	for (row = 0; row < 4; row++) {
		for (col = 0; col < 4; col++) {
			if (!(shape & (0x8000 >> (row * 4 + col)))) {
				continue;
			}
			x = Cur.x + col;
			y = Cur.y + row;
			if (y >= 0 && y < BOARD_ROWS && x >= 0 && x < BOARD_COLS) {
				Board[y][x] = Cur.type + 1;
			}
			if (y >= BOARD_HIDDEN) {
				allHidden = 0;
			}
		}
	}
	DirtyWell = 1;

	if (allHidden) {
		GameOver = 1;
		return;
	}

	cleared = ClearLines();
	if (cleared) {
		AddScore((uint32_t)LineScore[cleared] * Level);
		Lines += cleared;
		Level = 1 + Lines / TETRIS_LINES_PER_LEVEL;
	}

	if (!Spawn(now)) {
		GameOver = 1;
	}
}

static void HardDrop(uint32_t now) {
	while (StepDown(now)) {
		AddScore(2);
	}
	LockPiece(now);
}

static void HandleKey(int8_t key, uint32_t now) {
	switch (key) {
	case KEY_ROTATE_CW:
		TryRotate(1, now);
		break;
	case KEY_ROTATE_CCW:
		TryRotate(-1, now);
		break;
	case KEY_HARD_DROP:
		HardDrop(now);
		break;
	case KEY_MOVE_LEFT:
		TryShift(-1, now);
		RepeatDir = -1;
		RepeatAt = now + TETRIS_DAS_MS;
		break;
	case KEY_MOVE_RIGHT:
		TryShift(1, now);
		RepeatDir = 1;
		RepeatAt = now + TETRIS_DAS_MS;
		break;
	case KEY_SOFT_DROP:
		NextFallAt = now;   /* первый шаг вниз - сразу, даже при коротком нажатии */
		break;
	default:
		break;
	}
}

/* Автоповтор движения, пока клавиша удерживается. */
static void UpdateRepeat(uint32_t now, uint16_t held) {
	uint16_t bit;

	if (RepeatDir == 0) {
		return;
	}
	bit = KEYBIT((RepeatDir < 0) ? KEY_MOVE_LEFT : KEY_MOVE_RIGHT);
	if (!(held & bit)) {
		RepeatDir = 0;
		return;
	}
	while (TIME_REACHED(now, RepeatAt)) {
		if (!TryShift(RepeatDir, now)) {
			RepeatAt = now + TETRIS_ARR_MS;
			break;
		}
		RepeatAt += TETRIS_ARR_MS;
	}
}

static void UpdateGravity(uint32_t now, uint16_t held) {
	uint8_t soft = (held & KEYBIT(KEY_SOFT_DROP)) != 0;
	uint32_t interval = soft ? SoftInterval() : GravityInterval();

	while (TIME_REACHED(now, NextFallAt)) {
		if (StepDown(now)) {
			if (soft) {
				AddScore(1);
			}
			NextFallAt += interval;
		} else {
			NextFallAt = now + interval;
			break;
		}
	}
}

static void UpdateLock(uint32_t now) {
	if (!LockActive || !TIME_REACHED(now, LockAt)) {
		return;
	}
	if (Collides(Cur.type, Cur.rot, Cur.x, Cur.y + 1)) {
		LockPiece(now);
	} else {
		LockActive = 0;
	}
}

/* ---------- Отрисовка ---------- */

static void FillRect(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, OLED_COLOR color) {
	uint8_t y;

	for (y = y1; y <= y2; y++) {
		oled_DrawHLine(x1, x2, y, color);
	}
}

/* col 0..9, row 0..19 - видимые клетки стакана. */
static void DrawWellCell(uint8_t col, uint8_t row, uint8_t sprite) {
	oled_DrawSprite(WELL_CELLS_X + col * WELL_CELL_PX,
	                WELL_Y + row * WELL_CELL_PX,
	                Sprite_3x3, sprite, White);
}

static void DrawPieceCells(uint8_t type, uint8_t rot, int8_t px, int8_t py, uint8_t sprite) {
	uint16_t shape = Tetromino_Shapes[type][rot & 3];
	uint8_t row, col;
	int8_t y;

	for (row = 0; row < 4; row++) {
		for (col = 0; col < 4; col++) {
			if (!(shape & (0x8000 >> (row * 4 + col)))) {
				continue;
			}
			y = py + row - BOARD_HIDDEN;
			if (y >= 0 && y < BOARD_VISIBLE) {
				DrawWellCell(px + col, y, sprite);
			}
		}
	}
}

static void DrawWell(void) {
	uint8_t r, c;
	int8_t ghostY = Cur.y;

	for (r = 0; r < BOARD_VISIBLE; r++) {
		for (c = 0; c < BOARD_COLS; c++) {
			DrawWellCell(c, r, Board[r + BOARD_HIDDEN][c]);
		}
	}

	while (!Collides(Cur.type, Cur.rot, Cur.x, ghostY + 1)) {
		ghostY++;
	}
	if (ghostY != Cur.y) {
		DrawPieceCells(Cur.type, Cur.rot, Cur.x, ghostY, CELL_GHOST);
	}
	DrawPieceCells(Cur.type, Cur.rot, Cur.x, Cur.y, Cur.type + 1);
}

static void DrawNext(void) {
	uint16_t shape = Tetromino_Shapes[NextType][0];
	int minR = 4, maxR = -1, minC = 4, maxC = -1;
	int row, col, ox, oy;

	FillRect(NEXT_IN_X1, NEXT_IN_Y1, NEXT_IN_X2, NEXT_IN_Y2, Black);

	for (row = 0; row < 4; row++) {
		for (col = 0; col < 4; col++) {
			if (shape & (0x8000 >> (row * 4 + col))) {
				if (row < minR) minR = row;
				if (row > maxR) maxR = row;
				if (col < minC) minC = col;
				if (col > maxC) maxC = col;
			}
		}
	}

	ox = NEXT_IN_X1 + ((NEXT_IN_X2 - NEXT_IN_X1 + 1) - (maxC - minC + 1) * NEXT_CELL_PX) / 2
	     - minC * NEXT_CELL_PX;
	oy = NEXT_IN_Y1 + ((NEXT_IN_Y2 - NEXT_IN_Y1 + 1) - (maxR - minR + 1) * NEXT_CELL_PX) / 2
	     - minR * NEXT_CELL_PX;

	oled_DrawTetromino((uint8_t)ox, (uint8_t)oy, Sprite_6x6, (TetrisPiece)NextType, 0);
}

static void DrawScore(void) {
	char buf[8];
	uint32_t v = (Score > TETRIS_MAX_SCORE) ? TETRIS_MAX_SCORE : Score;
	uint8_t len = 0, i;
	char tmp[8];

	do {
		tmp[len++] = '0' + (v % 10);
		v /= 10;
	} while (v > 0);
	for (i = 0; i < len; i++) {
		buf[i] = tmp[len - 1 - i];
	}
	buf[len] = '\0';

	FillRect(PANEL_X1, SCORE_VALUE_Y, OLED_WIDTH - 1, SCORE_VALUE_Y + FONT_H - 1, Black);
	oled_SetCursor(PANEL_CX - (len * FONT_W) / 2, SCORE_VALUE_Y);
	oled_WriteString(buf, Font_7x10, White);
}

static void DrawStatic(void) {
	uint8_t i;

	oled_Fill(Black);

	for (i = 0; i < BOARD_VISIBLE; i++) {
		oled_DrawSprite(WELL_X, WELL_Y + i * WELL_CELL_PX, Sprite_3x3, CELL_WALL, White);
		oled_DrawSprite(WELL_RIGHT_X, WELL_Y + i * WELL_CELL_PX, Sprite_3x3, CELL_WALL, White);
	}
	for (i = 0; i < BOARD_COLS + 2; i++) {
		oled_DrawSprite(WELL_X + i * WELL_CELL_PX, WELL_FLOOR_Y, Sprite_3x3, CELL_WALL, White);
	}

	oled_SetCursor(PANEL_CX - (4 * FONT_W) / 2, 0);
	oled_WriteString((char *)"NEXT", Font_7x10, White);
	oled_DrawSquare(NEXT_FRAME_X1, NEXT_FRAME_X2, NEXT_FRAME_Y1, NEXT_FRAME_Y2, White);

	oled_SetCursor(PANEL_CX - (5 * FONT_W) / 2, SCORE_LABEL_Y);
	oled_WriteString((char *)"SCORE", Font_7x10, White);
}

static void Render(void) {
	if (DirtyWell) {
		DrawWell();
		Display_Flush(WELL_FLUSH_X1, WELL_FLUSH_X2);
		DirtyWell = 0;
	}
	if (DirtyPanel) {
		DrawNext();
		DrawScore();
		Display_Flush(PANEL_X1, OLED_WIDTH - 1);
		DirtyPanel = 0;
	}
}

/* ---------- Партия ---------- */

static void GameReset(uint32_t now) {
	memset(Board, 0, sizeof(Board));
	Score = 0;
	Lines = 0;
	Level = 1;
	StartAt = now;
	SpeedSteps = 0;
	GameOver = 0;
	RepeatDir = 0;
	BagPos = 7;

	Rng = now * 2654435761UL + 0x9E3779B9UL;
	if (Rng == 0) {
		Rng = 1;
	}

	NextType = BagNext();
	Spawn(now);
}

uint32_t Tetris_Play(void) {
	uint32_t now = NOW_MS();
	int8_t key;
	uint16_t held;

	Keypad_Flush();
	GameReset(now);

	DrawStatic();
	DrawWell();
	DrawNext();
	DrawScore();
	Display_Flush(0, OLED_WIDTH - 1);
	DirtyWell = 0;
	DirtyPanel = 0;

	while (!GameOver) {
		key = Keypad_WaitKey(TETRIS_FRAME_MS);
		now = NOW_MS();

		while (key >= 0 && !GameOver) {
			HandleKey(key, now);
			key = Keypad_WaitKey(0);
		}
		if (GameOver) {
			break;
		}

		held = Keypad_Held();
		UpdateSpeedup(now);
		UpdateRepeat(now, held);
		UpdateGravity(now, held);
		UpdateLock(now);
		Render();
	}

	Render();
	return Score;
}
