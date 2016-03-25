#include "ctr9_common.h"
#include "qemu-common.h"

// WARNING The fifo functions below only work with buffers with power of two sizes
void ctr9_fifo_init(ctr9_iofifo* fifo, const uint32_t size)
{
	fifo->buffer = g_malloc(size);
	fifo->size = size;
	ctr9_fifo_reset(fifo);
}

void ctr9_fifo_reset(ctr9_iofifo* fifo)
{
	fifo->w_ptr = fifo->r_ptr = 0;
	fifo->full = false;
}

uint32_t ctr9_fifo_len(const ctr9_iofifo* fifo)
{
	if(fifo->full)
		return fifo->size;

	return (fifo->w_ptr - fifo->r_ptr) & (fifo->size - 1);
}

void ctr9_fifo_push(ctr9_iofifo* fifo, uint32_t val, uint32_t size)
{
	if(fifo->full)
	{
		return;
	}

	if(size == 1)
		fifo->buffer[fifo->w_ptr] = val;
	else if(size == 2)
		*(uint16_t*)&fifo->buffer[fifo->w_ptr] = val;
	else if(size == 4)
		*(uint32_t*)&fifo->buffer[fifo->w_ptr] = val;

	fifo->w_ptr = (fifo->w_ptr + size) & (fifo->size - 1);
	
	if(ctr9_fifo_len(fifo) == 0)
	{
		fifo->full = true;
	}
}

uint32_t ctr9_fifo_pop(ctr9_iofifo* fifo)
{
	uint32_t res = 0;
	if(ctr9_fifo_len(fifo) != 0)
	{
		// TODO in reality the hardware returns the last word in the buffer if it's empty
		res = *(uint32_t*)&fifo->buffer[fifo->r_ptr];
		fifo->r_ptr = (fifo->r_ptr + 4) & (fifo->size - 1);
		
		fifo->full = false;
	}
	
	return res;
}