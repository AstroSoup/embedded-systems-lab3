/*
 * Экран поражения: ввод имени из трёх латинских букв "барабаном" и сохранение
 * результата в таблицу рекордов.
 *
 * Клавиши (строка, столбец, нумерация с нуля):
 *   (1,0) / (1,2) - выбрать предыдущую / следующую букву имени
 *   (0,1) / (2,1) - листать выбранную букву вверх / вниз (A..Z по кругу)
 *   (1,1)         - сохранить score и выйти в меню
 *
 * Пока буква не выбрана, слот показывается как "_". Выбранный слот мигает,
 * над ним и под ним рисуются треугольники "можно листать".
 * Если нажать (1,1), когда имя заполнено не полностью, курсор переходит на
 * первую пустую букву, и ничего не сохраняется.
 */
#include "main.h"
#include "cmsis_os.h"
#include "oled.h"
#include "menu.h"
#include "scoreboard.h"

#define KEY_NAME_PREV       KEY_ID(1, 0)
#define KEY_NAME_NEXT       KEY_ID(1, 2)
#define KEY_LETTER_UP       KEY_ID(0, 1)
#define KEY_LETTER_DOWN     KEY_ID(2, 1)
#define KEY_CONFIRM         KEY_ID(1, 1)

#define NAME_LEN            SCOREBOARD_NAME_LEN
#define EMPTY_CHAR          '_'
#define BLINK_MS            400

#define GLYPH_W             16              /* Font_16x26 */
#define GLYPH_H             26
#define SLOT_PITCH          24
#define SLOT_X0             ((OLED_WIDTH - (NAME_LEN * SLOT_PITCH - (SLOT_PITCH - GLYPH_W))) / 2)
#define GLYPH_Y             8
#define TRI_HALF            4               /* треугольник: ширина 9, высота 5 */
#define TRI_UP_Y            3
#define TRI_DOWN_Y          34
#define ENTRY_X1            SLOT_X0
#define ENTRY_X2            (SLOT_X0 + NAME_LEN * SLOT_PITCH - (SLOT_PITCH - GLYPH_W) - 1)
#define ENTRY_Y2            (TRI_DOWN_Y + TRI_HALF)

#define SCORE_Y             50
#define FONT_W              7               /* Font_7x10 */
#define FONT_H              10

static void FillRect(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, OLED_COLOR color) {
	uint8_t y;

	for (y = y1; y <= y2; y++) {
		oled_DrawHLine(x1, x2, y, color);
	}
}

/* Треугольник вершиной вверх: cx - центр, y - верхняя строка. */
static void DrawTriangleUp(uint8_t cx, uint8_t y) {
	uint8_t i;

	for (i = 0; i <= TRI_HALF; i++) {
		oled_DrawHLine(cx - i, cx + i, y + i, White);
	}
}

/* Перевёрнутый треугольник (вершиной вниз). */
static void DrawTriangleDown(uint8_t cx, uint8_t y) {
	uint8_t i;

	for (i = 0; i <= TRI_HALF; i++) {
		oled_DrawHLine(cx - (TRI_HALF - i), cx + (TRI_HALF - i), y + i, White);
	}
}

static void DrawScoreLine(uint32_t score) {
	char buf[16] = "SCORE ";
	char digits[11];
	uint8_t len = 6, n = 0;

	do {
		digits[n++] = '0' + (score % 10);
		score /= 10;
	} while (score > 0);
	while (n > 0) {
		buf[len++] = digits[--n];
	}
	buf[len] = '\0';

	oled_SetCursor((OLED_WIDTH - len * FONT_W) / 2, SCORE_Y);
	oled_WriteString(buf, Font_7x10, White);
}

/* Область с буквами и треугольниками: x ENTRY_X1..ENTRY_X2. */
static void DrawEntry(const char *name, uint8_t sel, uint8_t blinkOn) {
	uint8_t i, x;

	FillRect(ENTRY_X1, 0, ENTRY_X2, ENTRY_Y2, Black);

	for (i = 0; i < NAME_LEN; i++) {
		x = SLOT_X0 + i * SLOT_PITCH;
		if (i == sel) {
			DrawTriangleUp(x + GLYPH_W / 2, TRI_UP_Y);
			DrawTriangleDown(x + GLYPH_W / 2, TRI_DOWN_Y);
			if (!blinkOn) {
				continue;           /* фаза "погашено": рисуем только треугольники */
			}
		}
		oled_SetCursor(x, GLYPH_Y);
		oled_WriteChar(name[i], Font_16x26, White);
	}
}

/* Следующая буква барабана: dir > 0 - вперёд (A->B), иначе назад. Пустой слот
 * при первом листании становится 'A' (вперёд) или 'Z' (назад). */
static char ScrollLetter(char c, int8_t dir) {
	if (c == EMPTY_CHAR) {
		return (dir > 0) ? 'A' : 'Z';
	}
	if (dir > 0) {
		return (c == 'Z') ? 'A' : (char)(c + 1);
	}
	return (c == 'A') ? 'Z' : (char)(c - 1);
}

/* Индекс первого пустого слота или -1, если имя заполнено. */
static int8_t FirstEmpty(const char *name) {
	uint8_t i;

	for (i = 0; i < NAME_LEN; i++) {
		if (name[i] == EMPTY_CHAR) {
			return (int8_t)i;
		}
	}
	return -1;
}

void Action_GameOver(uint32_t score) {
	char name[NAME_LEN + 1];
	uint8_t sel = 0, blinkOn = 1, i;
	int8_t key, empty;

	for (i = 0; i < NAME_LEN; i++) {
		name[i] = EMPTY_CHAR;
	}
	name[NAME_LEN] = '\0';

	/* Нажатия, сделанные в момент проигрыша, не должны попасть в этот экран. */
	Keypad_WaitRelease(1000);
	Keypad_Flush();

	oled_Fill(Black);
	DrawScoreLine(score);
	DrawEntry(name, sel, blinkOn);
	Display_Flush(0, OLED_WIDTH - 1);

	for (;;) {
		key = Keypad_WaitKey(BLINK_MS);

		if (key < 0) {
			blinkOn = !blinkOn;             /* таймаут: переключаем мигание */
		} else {
			blinkOn = 1;                    /* при действии буква сразу видна */
			switch (key) {
			case KEY_NAME_PREV:
				sel = (sel + NAME_LEN - 1) % NAME_LEN;
				break;
			case KEY_NAME_NEXT:
				sel = (sel + 1) % NAME_LEN;
				break;
			case KEY_LETTER_UP:
				name[sel] = ScrollLetter(name[sel], 1);
				break;
			case KEY_LETTER_DOWN:
				name[sel] = ScrollLetter(name[sel], -1);
				break;
			case KEY_CONFIRM:
				empty = FirstEmpty(name);
				if (empty < 0) {
					Scoreboard_Submit(name, score);
					return;
				}
				sel = (uint8_t)empty;
				break;
			default:
				break;
			}
		}

		DrawEntry(name, sel, blinkOn);
		Display_Flush(ENTRY_X1, ENTRY_X2);
	}
}
