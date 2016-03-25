#pragma once
#include <inttypes.h>
#include <stdbool.h>

typedef struct ctr9_iofifo
{
	uint8_t* buffer;
	uint32_t r_ptr;
	uint32_t w_ptr;

	uint32_t size;
	bool full;
} ctr9_iofifo;

// WARNING The fifo functions below only work with buffers with power of two sizes
void ctr9_fifo_init(ctr9_iofifo* fifo, const uint32_t size);
void ctr9_fifo_reset(ctr9_iofifo* fifo);
uint32_t ctr9_fifo_len(const ctr9_iofifo* fifo);
void ctr9_fifo_push(ctr9_iofifo* fifo, uint32_t val, uint32_t size);
uint32_t ctr9_fifo_pop(ctr9_iofifo* fifo);
