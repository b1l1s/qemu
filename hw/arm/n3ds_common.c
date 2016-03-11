#include "n3ds_common.h"

// WARNING The fifo functions below only work with buffers with power of two sizes
uint32_t n3ds_fifo_len(const n3ds_iofifo* fifo)
{
	if(fifo->full)
		return 128;

	return (fifo->w_ptr - fifo->r_ptr) & (128 - 1);
}

void n3ds_fifo_push(n3ds_iofifo* fifo, uint32_t val)
{
	if(fifo->full)
	{
		return;
	}

	*(uint32_t*)&fifo->buffer[fifo->w_ptr] = val;
	fifo->w_ptr = (fifo->w_ptr + 4) & (128 - 1);
	
	if(n3ds_fifo_len(fifo) == 0)
	{
		fifo->full = 1;
	}
}

uint32_t n3ds_fifo_pop(n3ds_iofifo* fifo)
{
	uint32_t res = 0;
	if(n3ds_fifo_len(fifo) != 0)
	{
		// TODO in reality the hardware returns the last word in the buffer if it's empty
		res = *(uint32_t*)&fifo->buffer[fifo->r_ptr];
		fifo->r_ptr = (fifo->r_ptr + 4) & (128 - 1);
		
		fifo->full = 0;
	}
	
	return res;
}