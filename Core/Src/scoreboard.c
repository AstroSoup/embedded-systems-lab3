#include <string.h>
#include "main.h"
#include "cmsis_os.h"
#include "scoreboard.h"

static ScoreEntry Entries[SCOREBOARD_CAPACITY];
static uint8_t    EntryCount;
static osMutexId  ScoreMutexHandle;

static void Lock(void) {
	if (ScoreMutexHandle != NULL) {
		osMutexWait(ScoreMutexHandle, osWaitForever);
	}
}

static void Unlock(void) {
	if (ScoreMutexHandle != NULL) {
		osMutexRelease(ScoreMutexHandle);
	}
}

/* Индекс записи с таким именем или -1. Вызывать под мьютексом. */
static int8_t Find(const char *name) {
	uint8_t i;

	for (i = 0; i < EntryCount; i++) {
		if (strncmp(Entries[i].name, name, SCOREBOARD_NAME_LEN) == 0) {
			return (int8_t)i;
		}
	}
	return -1;
}

void Scoreboard_Init(void) {
	osMutexDef(ScoreMutex);
	ScoreMutexHandle = osMutexCreate(osMutex(ScoreMutex));
	EntryCount = 0;
}

uint8_t Scoreboard_Submit(const char *name, uint32_t score) {
	int8_t idx;
	uint8_t i, worst, stored = 0;

	Lock();

	idx = Find(name);
	if (idx >= 0) {
		/* имя уже есть: старый результат лучше - игнорируем, иначе заменяем */
		if (Entries[idx].score <= score) {
			Entries[idx].score = score;
			stored = 1;
		}
	} else if (EntryCount < SCOREBOARD_CAPACITY) {
		idx = (int8_t)EntryCount++;
		strncpy(Entries[idx].name, name, SCOREBOARD_NAME_LEN);
		Entries[idx].name[SCOREBOARD_NAME_LEN] = '\0';
		Entries[idx].score = score;
		stored = 1;
	} else {
		/* таблица полна: вытесняем худшую запись, если новый результат лучше */
		worst = 0;
		for (i = 1; i < EntryCount; i++) {
			if (Entries[i].score < Entries[worst].score) {
				worst = i;
			}
		}
		if (score > Entries[worst].score) {
			strncpy(Entries[worst].name, name, SCOREBOARD_NAME_LEN);
			Entries[worst].name[SCOREBOARD_NAME_LEN] = '\0';
			Entries[worst].score = score;
			stored = 1;
		}
	}

	Unlock();
	return stored;
}

uint8_t Scoreboard_Get(const char *name, uint32_t *score) {
	int8_t idx;
	uint8_t found = 0;

	Lock();
	idx = Find(name);
	if (idx >= 0) {
		*score = Entries[idx].score;
		found = 1;
	}
	Unlock();
	return found;
}

uint8_t Scoreboard_Count(void) {
	uint8_t n;

	Lock();
	n = EntryCount;
	Unlock();
	return n;
}

uint8_t Scoreboard_At(uint8_t index, ScoreEntry *out) {
	uint8_t ok = 0;

	Lock();
	if (index < EntryCount) {
		*out = Entries[index];
		ok = 1;
	}
	Unlock();
	return ok;
}
