#ifndef INC_MENU_H_
#define INC_MENU_H_

#include "main.h"

/* Матричная клавиатура: используются 3 строки и 3 столбца (нумерация с нуля). */
#define KEYPAD_ROWS       3
#define KEYPAD_COLS       3
#define KEYPAD_KEYS       (KEYPAD_ROWS * KEYPAD_COLS)
#define KEY_ID(row, col)  ((row) * KEYPAD_COLS + (col))

void Menu_Init(void);

void Action_Play(void);
void Action_HighScore(void);
void Action_GameOver(uint32_t score);

/* Ввод. Задача опроса клавиатуры кладёт в очередь событие на каждое нажатие
 * (без автоповтора) и ведёт маску клавиш, удерживаемых в данный момент. */

/* Ждёт нажатие до timeout_ms. Возвращает KEY_ID(row, col) или -1 по таймауту. */
int8_t   Keypad_WaitKey(uint32_t timeout_ms);
/* Маска удерживаемых клавиш: бит KEY_ID(row, col). */
uint16_t Keypad_Held(void);
/* Выбрасывает накопившиеся события нажатий. */
void     Keypad_Flush(void);
/* Ждёт, пока все клавиши будут отпущены (не дольше timeout_ms). */
void     Keypad_WaitRelease(uint32_t timeout_ms);

/* Выводит на экран столбцы x1..x2 буфера OLED (под мьютексом I2C). */
void     Display_Flush(uint8_t x1, uint8_t x2);

#endif /* INC_MENU_H_ */
