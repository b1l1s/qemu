#pragma once
#include <inttypes.h>

typedef struct n3ds_iofifo
{
	uint8_t buffer[128];
	uint32_t r_ptr;
	uint32_t w_ptr;
	uint32_t full;
} n3ds_iofifo;

// WARNING The fifo functions below only work with buffers with power of two sizes
uint32_t n3ds_fifo_len(const n3ds_iofifo* fifo);
void n3ds_fifo_push(n3ds_iofifo* fifo, uint32_t val);
uint32_t n3ds_fifo_pop(n3ds_iofifo* fifo);
