#include "main.h"
#include "cmsis_os.h"
#include "oled.h"
#include "sprites.h"
#include "kb.h"
#include "menu.h"
#include "tetris.h"
#include "scoreboard.h"
#include "fonts.h"

#define KEYPAD_SCAN_MS        10
#define KEYPAD_DEBOUNCE       2

#define KEY_QUEUE_LEN         16
#define MENU_TITLE_X          10
#define MENU_TITLE_Y          2

/* Кнопки меню: центральный столбец. */
#define MENU_KEY_UP           KEY_ID(0, 1)
#define MENU_KEY_OK           KEY_ID(1, 1)
#define MENU_KEY_DOWN         KEY_ID(2, 1)

typedef struct {
	BitmapDef *bitmap;
	uint8_t x;
	uint8_t y;
	void (*action)(void);
} MenuItem;

static const MenuItem MenuItems[] = {
	{ &Sprite_BtnPlay,      22, 24, Action_Play      },
	{ &Sprite_BtnHighScore, 22, 44, Action_HighScore }
};

#define MENU_ITEM_COUNT  (sizeof(MenuItems) / sizeof(MenuItems[0]))

static const uint8_t KeypadRowMask[KEYPAD_ROWS] = { ROW1, ROW2, ROW3 };
static const uint8_t KeypadColBit[KEYPAD_COLS]  = { 0x04, 0x02, 0x01 };

static volatile uint16_t KeypadHeldMask;

static osMutexId   I2cMutexHandle;
static osMessageQId KeyQueueHandle;
static osThreadId  TaskKeypadHandle;
static osThreadId  TaskMenuHandle;

void StartTaskKeypad(void const * argument);
void StartTaskMenu(void const * argument);

int8_t Keypad_WaitKey(uint32_t timeout_ms) {
	osEvent event = osMessageGet(KeyQueueHandle, timeout_ms);

	if (event.status != osEventMessage) {
		return -1;
	}
	return (int8_t)(event.value.v - 1);
}

uint16_t Keypad_Held(void) {
	return KeypadHeldMask;
}

void Keypad_Flush(void) {
	while (Keypad_WaitKey(0) >= 0) {
	}
}

void Keypad_WaitRelease(uint32_t timeout_ms) {
	while (KeypadHeldMask != 0 && timeout_ms > 0) {
		osDelay(KEYPAD_SCAN_MS);
		timeout_ms = (timeout_ms > KEYPAD_SCAN_MS) ? timeout_ms - KEYPAD_SCAN_MS : 0;
	}
}

void Display_Flush(uint8_t x1, uint8_t x2) {
	osMutexWait(I2cMutexHandle, osWaitForever);
	oled_UpdateColumns(x1, x2);
	osMutexRelease(I2cMutexHandle);
}

void Action_Play(void) {
	uint32_t score = Tetris_Play();

	Action_GameOver(score);

	Keypad_WaitRelease(1000);
	Keypad_Flush();
}

/* Action_GameOver реализована в gameover.c. */

/* ---------- Экран рекордов ----------
 *
 * Список по убыванию score: номер, имя, score. Видно HS_VISIBLE строк.
 *   (0,1) - листать вверх, (2,1) - листать вниз, (1,1) - назад в меню.
 */
#define HS_KEY_UP           KEY_ID(0, 1)
#define HS_KEY_DOWN         KEY_ID(2, 1)
#define HS_KEY_BACK         KEY_ID(1, 1)

#define HS_FONT_W           7                   /* Font_7x10 */
#define HS_FONT_H           10
#define HS_TITLE_Y          0
#define HS_LINE_Y           11
#define HS_ROW_Y0           14
#define HS_ROW_PITCH        12
#define HS_VISIBLE          4
#define HS_RANK_X           0
#define HS_NAME_X           30
#define HS_SCORE_RIGHT_X    118                 /* правый край поля score */
#define HS_SCORE_DIGITS     6
#define HS_ARROW_CX         124
#define HS_ARROW_HALF       2
#define HS_ARROW_UP_Y       14
#define HS_ARROW_DOWN_Y     57

/* Пишет число в buf справа налево в поле width, лишние слева - пробелы. */
static void HighScore_FormatNumber(char *buf, uint8_t width, uint32_t value) {
	uint8_t i = width;

	buf[width] = '\0';
	while (i > 0) {
		i--;
		if (value > 0 || i == width - 1) {
			buf[i] = (char)('0' + (value % 10));
			value /= 10;
		} else {
			buf[i] = ' ';
		}
	}
}

/* Копия таблицы, отсортированная по убыванию score (при равенстве - по порядку
 * добавления). Возвращает число записей. */
static uint8_t HighScore_Load(ScoreEntry *list) {
	uint8_t count = 0, i, j;
	ScoreEntry tmp;

	while (count < SCOREBOARD_CAPACITY && Scoreboard_At(count, &list[count])) {
		count++;
	}

	/* сортировка вставками: устойчивая, записей немного */
	for (i = 1; i < count; i++) {
		tmp = list[i];
		for (j = i; j > 0 && list[j - 1].score < tmp.score; j--) {
			list[j] = list[j - 1];
		}
		list[j] = tmp;
	}
	return count;
}

static void HighScore_Draw(const ScoreEntry *list, uint8_t count, uint8_t top) {
	char buf[HS_SCORE_DIGITS + 2];
	uint8_t row, idx, y, i;

	oled_Fill(Black);

	oled_SetCursor((OLED_WIDTH - 11 * HS_FONT_W) / 2, HS_TITLE_Y);
	oled_WriteString("HIGH SCORES", Font_7x10, White);
	oled_DrawHLine(0, OLED_WIDTH - 1, HS_LINE_Y, White);

	if (count == 0) {
		oled_SetCursor((OLED_WIDTH - 10 * HS_FONT_W) / 2, 32);
		oled_WriteString("NO RECORDS", Font_7x10, White);
		return;
	}

	for (row = 0; row < HS_VISIBLE; row++) {
		idx = top + row;
		if (idx >= count) {
			break;
		}
		y = HS_ROW_Y0 + row * HS_ROW_PITCH;

		HighScore_FormatNumber(buf, 2, (uint32_t)idx + 1);
		buf[2] = '.';
		buf[3] = '\0';
		oled_SetCursor(HS_RANK_X, y);
		oled_WriteString(buf, Font_7x10, White);

		oled_SetCursor(HS_NAME_X, y);
		oled_WriteString((char *)list[idx].name, Font_7x10, White);

		HighScore_FormatNumber(buf, HS_SCORE_DIGITS, list[idx].score);
		oled_SetCursor(HS_SCORE_RIGHT_X - HS_SCORE_DIGITS * HS_FONT_W + 1, y);
		oled_WriteString(buf, Font_7x10, White);
	}

	/* стрелки "есть ещё записи" сверху и снизу */
	if (top > 0) {
		for (i = 0; i <= HS_ARROW_HALF; i++) {
			oled_DrawHLine(HS_ARROW_CX - i, HS_ARROW_CX + i, HS_ARROW_UP_Y + i, White);
		}
	}
	if (top + HS_VISIBLE < count) {
		for (i = 0; i <= HS_ARROW_HALF; i++) {
			oled_DrawHLine(HS_ARROW_CX - (HS_ARROW_HALF - i),
					HS_ARROW_CX + (HS_ARROW_HALF - i), HS_ARROW_DOWN_Y + i, White);
		}
	}
}

void Action_HighScore(void) {
	ScoreEntry list[SCOREBOARD_CAPACITY];
	uint8_t count = HighScore_Load(list);
	uint8_t top = 0;
	uint8_t maxTop = (count > HS_VISIBLE) ? (uint8_t)(count - HS_VISIBLE) : 0;
	int8_t key;

	Keypad_Flush();                             /* нажатие OK в меню сюда не попадает */
	HighScore_Draw(list, count, top);
	Display_Flush(0, OLED_WIDTH - 1);

	for (;;) {
		key = Keypad_WaitKey(osWaitForever);

		if (key == HS_KEY_BACK) {
			break;
		}
		if (key == HS_KEY_UP && top > 0) {
			top--;
		} else if (key == HS_KEY_DOWN && top < maxTop) {
			top++;
		} else {
			continue;                           /* ничего не изменилось - не перерисовываем */
		}
		HighScore_Draw(list, count, top);
		Display_Flush(0, OLED_WIDTH - 1);
	}

	Keypad_WaitRelease(1000);
	Keypad_Flush();
}

static void Menu_Draw(uint8_t focus) {
	uint8_t i;

	oled_Fill(Black);
	oled_DrawBitmap(MENU_TITLE_X, MENU_TITLE_Y, Sprite_Title, White);
	for (i = 0; i < MENU_ITEM_COUNT; i++) {
		oled_DrawBitmap(MenuItems[i].x, MenuItems[i].y, *MenuItems[i].bitmap,
				(i == focus) ? Black : White);
	}

	osMutexWait(I2cMutexHandle, osWaitForever);
	oled_UpdateScreen();
	osMutexRelease(I2cMutexHandle);
}

void Menu_Init(void) {
	Scoreboard_Init();

	osMutexDef(I2cMutex);
	I2cMutexHandle = osMutexCreate(osMutex(I2cMutex));

	osMessageQDef(KeyQueue, KEY_QUEUE_LEN, uint32_t);
	KeyQueueHandle = osMessageCreate(osMessageQ(KeyQueue), NULL);

	osThreadDef(Task_Keypad, StartTaskKeypad, osPriorityNormal, 0, 256);
	TaskKeypadHandle = osThreadCreate(osThread(Task_Keypad), NULL);

	/* Игра выполняется в контексте этой задачи (Action_Play), стек с запасом. */
	osThreadDef(Task_Menu, StartTaskMenu, osPriorityNormal, 0, 512);
	TaskMenuHandle = osThreadCreate(osThread(Task_Menu), NULL);
}

void StartTaskKeypad(void const * argument) {
	uint8_t count[KEYPAD_KEYS] = {0};
	uint8_t latched[KEYPAD_KEYS] = {0};
	uint8_t row, col, id, mask;

	for (;;) {
		for (row = 0; row < KEYPAD_ROWS; row++) {
			osMutexWait(I2cMutexHandle, osWaitForever);
			mask = Check_Row_Mask(KeypadRowMask[row]);
			osMutexRelease(I2cMutexHandle);

			for (col = 0; col < KEYPAD_COLS; col++) {
				id = KEY_ID(row, col);

				if (mask & KeypadColBit[col]) {
					if (count[id] < KEYPAD_DEBOUNCE) {
						count[id]++;
					}
					if (count[id] == KEYPAD_DEBOUNCE && !latched[id]) {
						latched[id] = 1;
						KeypadHeldMask |= (uint16_t)(1u << id);
						/* значение 0 зарезервировано под "нет события" */
						osMessagePut(KeyQueueHandle, id + 1, 0);
					}
				} else {
					count[id] = 0;
					if (latched[id]) {
						latched[id] = 0;
						KeypadHeldMask &= (uint16_t)~(1u << id);
					}
				}
			}
		}
		osDelay(KEYPAD_SCAN_MS);
	}
}

void StartTaskMenu(void const * argument) {
	uint8_t focus = 0;
	int8_t key;

	Menu_Draw(focus);

	for (;;) {
		key = Keypad_WaitKey(osWaitForever);

		switch (key) {
		case MENU_KEY_UP:
			focus = (focus + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
			Menu_Draw(focus);
			break;
		case MENU_KEY_DOWN:
			focus = (focus + 1) % MENU_ITEM_COUNT;
			Menu_Draw(focus);
			break;
		case MENU_KEY_OK:
			MenuItems[focus].action();
			Menu_Draw(focus);
			break;
		default:
			break;
		}
	}
}
