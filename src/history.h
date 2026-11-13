#pragma once
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

#ifndef HISTORY_MAX
#define HISTORY_MAX 60
#endif
#define HISTORY_LINE 160

void history_init(void);
int  history_count(void);
/* 0 = mais recente. */
bool history_get(int i, char *out, size_t n);
void history_add(const char *text);
/* Filtra (query + tipo: "", "problem", "association", "read", "other").
 * Escreve índices (0=mais recente) em idx. */
int  history_filter(const char *query, const char *type, int *idx, int max);

#ifdef __cplusplus
}
#endif
