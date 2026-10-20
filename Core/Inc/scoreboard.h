#ifndef INC_SCOREBOARD_H_
#define INC_SCOREBOARD_H_

#include <stdint.h>

/* Таблица рекордов: словарь "имя -> лучший score". Данные хранятся в RAM,
 * доступ защищён мьютексом FreeRTOS. */

#define SCOREBOARD_NAME_LEN   3
#define SCOREBOARD_CAPACITY   16

typedef struct {
	char     name[SCOREBOARD_NAME_LEN + 1];
	uint32_t score;
} ScoreEntry;

void Scoreboard_Init(void);

/* Записывает результат. Если имя уже есть и сохранённый score больше нового -
 * запись игнорируется, иначе значение заменяется. Если таблица заполнена,
 * а имени в ней нет, новая запись вытесняет худшую, но только если её score
 * больше. Возвращает 1, если таблица изменилась (или значение осталось тем же
 * при равных score), 0 - если запись проигнорирована. */
uint8_t Scoreboard_Submit(const char *name, uint32_t score);

/* Ищет score по имени. Возвращает 1 и заполняет *score, если имя найдено. */
uint8_t Scoreboard_Get(const char *name, uint32_t *score);

/* Число записей и доступ к i-й записи (в порядке добавления). */
uint8_t Scoreboard_Count(void);
uint8_t Scoreboard_At(uint8_t index, ScoreEntry *out);

#endif /* INC_SCOREBOARD_H_ */
