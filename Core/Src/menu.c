#include "main.h"
#include "cmsis_os.h"
#include "oled.h"
#include "sprites.h"
#include "kb.h"
#include "menu.h"
#include "tetris.h"

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

/* Заглушка: экран поражения будет реализован отдельно. */
void Action_GameOver(uint32_t score) {
	(void)score;
}

void Action_HighScore(void) {
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
