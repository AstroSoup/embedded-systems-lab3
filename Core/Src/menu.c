#include "main.h"
#include "cmsis_os.h"
#include "oled.h"
#include "sprites.h"
#include "kb.h"
#include "menu.h"

#define KEYPAD_COL_CENTER     0x02
#define KEYPAD_ROWS           3
#define KEYPAD_SCAN_MS        10
#define KEYPAD_DEBOUNCE       2

#define MENU_KEY_QUEUE_LEN    8
#define MENU_TITLE_X          10
#define MENU_TITLE_Y          2

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
static const MenuKey KeypadRowKey[KEYPAD_ROWS]  = { MENU_KEY_UP, MENU_KEY_OK, MENU_KEY_DOWN };

static osMutexId   I2cMutexHandle;
static osMessageQId MenuKeyQueueHandle;
static osThreadId  TaskKeypadHandle;
static osThreadId  TaskMenuHandle;

void StartTaskKeypad(void const * argument);
void StartTaskMenu(void const * argument);

void Action_Play(void) {
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

	osMessageQDef(MenuKeyQueue, MENU_KEY_QUEUE_LEN, uint32_t);
	MenuKeyQueueHandle = osMessageCreate(osMessageQ(MenuKeyQueue), NULL);

	osThreadDef(Task_Keypad, StartTaskKeypad, osPriorityNormal, 0, 256);
	TaskKeypadHandle = osThreadCreate(osThread(Task_Keypad), NULL);

	osThreadDef(Task_Menu, StartTaskMenu, osPriorityNormal, 0, 256);
	TaskMenuHandle = osThreadCreate(osThread(Task_Menu), NULL);
}

void StartTaskKeypad(void const * argument) {
	uint8_t count[KEYPAD_ROWS] = {0};
	uint8_t latched[KEYPAD_ROWS] = {0};
	uint8_t i, key;

	for (;;) {
		for (i = 0; i < KEYPAD_ROWS; i++) {
			osMutexWait(I2cMutexHandle, osWaitForever);
			key = Check_Row(KeypadRowMask[i]);
			osMutexRelease(I2cMutexHandle);

			if (key == KEYPAD_COL_CENTER) {
				if (count[i] < KEYPAD_DEBOUNCE) {
					count[i]++;
				}
				if (count[i] == KEYPAD_DEBOUNCE && !latched[i]) {
					latched[i] = 1;
					osMessagePut(MenuKeyQueueHandle, KeypadRowKey[i], 0);
				}
			} else {
				count[i] = 0;
				latched[i] = 0;
			}
		}
		osDelay(KEYPAD_SCAN_MS);
	}
}

void StartTaskMenu(void const * argument) {
	uint8_t focus = 0;
	osEvent event;

	Menu_Draw(focus);

	for (;;) {
		event = osMessageGet(MenuKeyQueueHandle, osWaitForever);
		if (event.status != osEventMessage) {
			continue;
		}

		switch (event.value.v) {
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
