#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <idt.h>
void kpanic(char *msg, struct StackFrame *frame);