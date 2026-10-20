#ifndef INC_MENU_H_
#define INC_MENU_H_

#include "main.h"

typedef enum {
	MENU_KEY_UP = 1,
	MENU_KEY_DOWN,
	MENU_KEY_OK
} MenuKey;

void Menu_Init(void);

void Action_Play(void);
void Action_HighScore(void);

#endif /* INC_MENU_H_ */
