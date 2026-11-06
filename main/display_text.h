#pragma once
#include <stddef.h>
// Uppercase ASCII labels for the LCD's small built-in font.
void display_ascii(const char *utf8, char *output, size_t capacity);
