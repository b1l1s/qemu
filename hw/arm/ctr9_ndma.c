#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "sysemu/dma.h"
#include "ctr9_common.h"

#define TYPE_CTR9_NDMA "ctr9-ndma"
#define CTR9_NDMA(obj) \
    OBJECT_CHECK(ctr9_ndma_state, (obj), TYPE_CTR9_NDMA)

// https://www.3dbrew.org/wiki/NDMA_Registers
#define NDMA_GLOBAL_CNT		0x00
#define NDMA_SRC_ADDR		0x04
#define NDMA_DST_ADDR		0x08
#define NDMA_TRANSFER_CNT	0x0C
#define NDMA_WRITE_CNT		0x10
#define NDMA_BLOCK_CNT		0x14
#define NDMA_FILL_DATA		0x18
#define NDMA_CNT			0x1C

#define NDMA_ARB_FIXED		(0)
#define NDMA_ARB_RNDROBIN	(1)

#define NDMA_UPDATE_INC		(0)
#define NDMA_UPDATE_DEC		(1)
#define NDMA_UPDATE_FIXED	(2)
#define NDMA_UPDATE_FILL	(3)

typedef struct ctr9_ndma_channel_state
{
	qemu_irq irq;
	
	// NDMA_SRC_ADDR
	uint32_t src_addr;
	// NDMA_DST_ADDR
	uint32_t dst_addr;
	
	// NDMA_TRANSFER_CNT
	uint32_t total;
	// NDMA_WRITE_CNT
	uint32_t block_count;
	
	// NDMA_BLOCK_CNT
	uint16_t interval;
	uint16_t prescaler;
	
	// NDMA_FILL_DATA
	uint32_t fill;
	
	// NDMA_CNT
	uint8_t dst_update;
	uint8_t dst_reload;
	uint8_t src_update;
	uint8_t src_reload;
	uint32_t block_size;
	uint8_t startup;
	uint8_t immediate;
	uint8_t repeating;
	uint8_t irq_enable;
	uint8_t enable;
} ctr9_ndma_channel_state;

#define NDMA_EVENT_SIZE (512)

typedef struct ctr9_ndma_event
{
	int id[NDMA_EVENT_SIZE];
	
	bool full;
	uint32_t rd;
	uint32_t wr;
} ctr9_ndma_event;

typedef struct ctr9_ndma_state
{
	SysBusDevice parent_obj;
	MemoryRegion iomem;
	
	// NDMA_GLOBAL_CNT
	uint8_t enable;
	uint8_t cycle_sel;
	uint8_t arb;
	
	ctr9_ndma_channel_state channels[8];
	
	ctr9_iofifo fifo;
	
	ctr9_ndma_event events;
	bool processing;
} ctr9_ndma_state;

static void ctr9_ndma_trigger(ctr9_ndma_state* s, ctr9_ndma_channel_state* c)
{
	if(c->src_update == NDMA_UPDATE_FILL)
	{
		// TODO
	}
	else
	{
		int src_stride = 1, dst_stride = 1;
		if(c->src_update == NDMA_UPDATE_DEC)
			src_stride = -1;
		else if(c->src_update == NDMA_UPDATE_FIXED)
			src_stride = 0;
		
		if(c->dst_update == NDMA_UPDATE_DEC)
			dst_stride = -1;
		else if(c->dst_update == NDMA_UPDATE_FIXED)
			dst_stride = 0;
		
		uint32_t temp, i; // TODO block_size limited to 4
		for(i = 0; i < c->block_count; ++i)
		{
			dma_memory_read(&address_space_memory, c->src_addr + i * c->block_size * src_stride, &temp, c->block_size);
			dma_memory_write(&address_space_memory, c->dst_addr + i * c->block_size * dst_stride, &temp, c->block_size);
		}
		
		c->total -= c->block_count;
		if(c->total == 0 || c->immediate)
			c->enable = 0;
		
		c->src_addr += i * c->block_size * src_stride;
		c->dst_addr += i * c->block_size * dst_stride;
	}
}

static bool ctr9_ndma_event_empty(ctr9_ndma_event* e)
{
	if(e->full)
		return false;
	return e->rd == e->wr;
}

static uint32_t ctr9_ndma_event_len(ctr9_ndma_event* e)
{
	if(e->full)
		return NDMA_EVENT_SIZE;

	return (e->wr - e->rd) & (NDMA_EVENT_SIZE - 1);
}

static void ctr9_ndma_event_push(ctr9_ndma_event* e, int startup_id)
{
	if(!e->full)
	{
		e->id[e->wr] = startup_id;
		e->wr = (e->wr + 1) & (NDMA_EVENT_SIZE - 1);
		
		if(ctr9_ndma_event_len(e) == 0)
		{
			e->full = true;
		}
	}
}

static int ctr9_ndma_event_pop(ctr9_ndma_event* e)
{
	int res = 0;
	if(!ctr9_ndma_event_empty(e))
	{
		res = e->id[e->rd];
		e->rd = (e->rd + 1) & (NDMA_EVENT_SIZE - 1);
		
		e->full = false;
	}
	
	return res;
}

static void ctr9_ndma_event_process(ctr9_ndma_state *s, int startup_id)
{
	int i;
	for(i = 0; i < 8; ++i)
	{
		ctr9_ndma_channel_state* c = &s->channels[i];
		if(c->startup == startup_id && c->enable)
		{
			ctr9_ndma_trigger(s, c);
		}
	}
}

static void ctr9_ndma_set_gpio(void *opaque, int startup_id, int level)
{
	ctr9_ndma_state *s = opaque;
	
	if(level && startup_id < 16)
	{
		if(!s->processing)
		{
			s->processing = 1;
			ctr9_ndma_event_process(s, startup_id);
			
			// process other pending events
			while(!ctr9_ndma_event_empty(&s->events))
				ctr9_ndma_event_process(s, ctr9_ndma_event_pop(&s->events));
			
			s->processing = 0;
		}
		else
		{
			ctr9_ndma_event_push(&s->events, startup_id);
		}
	}
}

static uint64_t ctr9_ndma_read(void* opaque, hwaddr offset, unsigned size)
{
	ctr9_ndma_state* s = (ctr9_ndma_state*)opaque;
	uint64_t res = 0;
	
	if(offset < 4)
	{
		// NDMA_GLOBAL_CNT
		res = s->enable | (s->cycle_sel << 16) | (s->arb << 31);
	}
	else
	{
		uint32_t ndma_id = ((offset & 0xFF) - 4) / 0x1C;
		uint32_t ndma_offset = (((offset & 0xFF) - 4) % 0x1C) + 4;
		
		ctr9_ndma_channel_state* c = &s->channels[ndma_id];
		
		switch(ndma_offset)
		{
		case NDMA_SRC_ADDR:
			res = c->src_addr;
			break;
		case NDMA_DST_ADDR:
			res = c->dst_addr;
			break;
		case NDMA_TRANSFER_CNT:
			res = c->total;
			break;
		case NDMA_WRITE_CNT:
			res = c->block_count;
			break;
		case NDMA_BLOCK_CNT:
			res = c->interval | (c->prescaler << 16);
			break;
		case NDMA_FILL_DATA:
			res = c->fill;
			break;
		case NDMA_CNT:
			res = 0;
			res |= (c->dst_update << 10) | (c->dst_reload << 12);
			res |= (c->src_update << 13) | (c->src_reload << 15);
			res |= (c->block_size << 16);
			res |= (c->startup << 24);
			res |= (c->immediate << 28) | (c->repeating << 29);
			res |= (c->irq_enable << 30);
			res |= (c->enable << 31);
			break;
		default:
			break;
		}
	}
	printf("ctr9_ndma_read  0x%03X %X %08X\n", (uint32_t)offset, size, res);
	
	return res;
}

static void ctr9_ndma_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
	ctr9_ndma_state* s = (ctr9_ndma_state*)opaque;
	printf("ctr9_ndma_write 0x%03X %X %08X\n", (uint32_t)offset, size, (uint32_t)value);
	
	if(offset < 4)
	{
		// NDMA_GLOBAL_CNT
	}
	else
	{
		uint32_t ndma_id = ((offset & 0xFF) - 4) / 0x1C;
		uint32_t ndma_offset = (((offset & 0xFF) - 4) % 0x1C) + 4;
		
		ctr9_ndma_channel_state* c = &s->channels[ndma_id];
		
		switch(ndma_offset)
		{
		case NDMA_SRC_ADDR:
			c->src_addr = value;
			break;
		case NDMA_DST_ADDR:
			c->dst_addr = value;
			break;
		case NDMA_TRANSFER_CNT:
			c->total = value;
			break;
		case NDMA_WRITE_CNT:
			c->block_count = value;
			break;
		case NDMA_BLOCK_CNT:
			c->interval = value & 0xFFFF;
			c->prescaler = (value >> 16) & 3;
			break;
		case NDMA_FILL_DATA:
			c->fill = value;
			break;
		case NDMA_CNT:
			c->dst_update = (value >> 10) & 3;
			c->dst_reload = (value >> 12) & 1;
			c->src_update = (value >> 13) & 3;
			c->src_reload = (value >> 15) & 1;
			c->block_size = (value >> 16) & 0xF;
			c->startup = (value >> 24) & 0xF;
			c->immediate = (value >> 28) & 1;
			c->repeating = (value >> 29) & 1;
			c->irq_enable = (value >> 30) & 1;
			c->enable = (value >> 31) & 1;
			
			if(c->enable)
			{
				printf("NDMA *****\n");
				printf(" src_addr 0x%08X\n", c->src_addr);
				printf(" dst_addr 0x%08X\n", c->dst_addr);
				printf(" total 0x%08X\n", c->total);
				printf(" block_count 0x%08X\n", c->block_count);
				printf(" interval 0x%04X\n", c->interval);
				printf(" prescaler 0x%02X\n", c->prescaler);
				printf(" fill 0x%08X\n", c->fill);
				printf(" dst_update 0x%02X\n", c->dst_update);
				printf(" dst_reload 0x%02X\n", c->dst_reload);
				printf(" src_update 0x%02X\n", c->src_update);
				printf(" src_reload 0x%02X\n", c->src_reload);
				printf(" block_size 0x%02X\n", c->block_size);
				printf(" startup 0x%02X\n", c->startup);
				printf(" immediate 0x%02X\n", c->immediate);
				printf(" repeating 0x%02X\n", c->repeating);
				printf(" irq_enable 0x%02X\n", c->irq_enable);
				
				if(c->immediate)
					ctr9_ndma_trigger(s, c);
			}
			
			break;
		default:
			break;
		}
	}
}

static const MemoryRegionOps ctr9_ndma_ops =
{
	.read = ctr9_ndma_read,
	.write = ctr9_ndma_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static int ctr9_ndma_init(SysBusDevice *sbd)
{
	DeviceState *dev = DEVICE(sbd);
	ctr9_ndma_state *s = CTR9_NDMA(dev);
	
	int i;
	for(i = 0; i < 8; ++i)
		sysbus_init_irq(sbd, &s->channels[i].irq);

	qdev_init_gpio_in(DEVICE(dev), ctr9_ndma_set_gpio, 15);
	memory_region_init_io(&s->iomem, OBJECT(s), &ctr9_ndma_ops, s, "ctr9-ndma", 0x1000);
	sysbus_init_mmio(sbd, &s->iomem);
	
	memset(&s->fifo, 0, sizeof(s->fifo));

	return 0;
}

static const VMStateDescription ctr9_ndma_vmsd = {
	.name = "ctr9-ndma",
	.version_id = 1,
	.minimum_version_id = 1,
	.fields = (VMStateField[]) {
		VMSTATE_END_OF_LIST()
	}
};

static void ctr9_ndma_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

	k->init = ctr9_ndma_init;
	dc->vmsd = &ctr9_ndma_vmsd;
}

static const TypeInfo ctr9_ndma_info = {
	.name          = TYPE_CTR9_NDMA,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(ctr9_ndma_state),
	.class_init    = ctr9_ndma_class_init,
};

static void ctr9_ndma_register_types(void)
{
	type_register_static(&ctr9_ndma_info);
}

type_init(ctr9_ndma_register_types)