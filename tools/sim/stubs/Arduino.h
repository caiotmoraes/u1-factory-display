#pragma once
/* Stub mínimo do Arduino para o simulador de host (só a UI). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <assert.h>
#include <algorithm>
uint32_t millis(void);
inline void delay(uint32_t) {}
struct SerialStub {
    void printf(const char *f, ...) { va_list a; va_start(a, f); vfprintf(stderr, f, a); va_end(a); }
    void println(const char *s) { fprintf(stderr, "%s\n", s); }
};
extern SerialStub Serial;
typedef void (*TaskFunction_t)(void *);
#define pdPASS 1
inline int xTaskCreate(TaskFunction_t, const char *, int, void *, int, void *) { return 0; }
inline void vTaskDelete(void *) {}
using std::min;
using std::max;
