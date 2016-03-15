#pragma once
#include <inttypes.h>

typedef struct ctr9_iofifo
{
	uint8_t buffer[128];
	uint32_t r_ptr;
	uint32_t w_ptr;
	uint32_t full;
} ctr9_iofifo;

// WARNING The fifo functions below only work with buffers with power of two sizes
uint32_t ctr9_fifo_len(const ctr9_iofifo* fifo);
void ctr9_fifo_push(ctr9_iofifo* fifo, uint32_t val);
uint32_t ctr9_fifo_pop(ctr9_iofifo* fifo);
