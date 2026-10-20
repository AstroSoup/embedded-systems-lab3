#ifndef INC_TETRIS_H_
#define INC_TETRIS_H_

#include <stdint.h>

/* Запускает одну партию и блокирует вызывающую задачу до поражения.
 * Возвращает набранный score. */
uint32_t Tetris_Play(void);

#endif /* INC_TETRIS_H_ */
