#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "ctr9_common.h"

#define TYPE_CTR9_PXI "ctr9-pxi"
#define CTR9_PXI(obj) \
    OBJECT_CHECK(ctr9_pxi_state, (obj), TYPE_CTR9_PXI)

#define PXI_SYNC			0x00
#define PXI_CNT				0x04
#define PXI_SEND			0x08
#define PXI_RECV			0x0C

//https://www.3dbrew.org/wiki/PXI_Registers
typedef struct ctr9_pxi_state {
	SysBusDevice parent_obj;
	MemoryRegion iomem;
	
	qemu_irq irq[3];
	
	// PXI_SYNC
	uint8_t recv_count;
	uint8_t send_count;
	bool sync_irq_enable;
	
	// PXI_CNT
	bool send_fifo_empty;
	bool send_fifo_full;
	bool send_fifo_empty_irq_enable;
	
	bool recv_fifo_empty;
	bool recv_fifo_full;
	bool recv_fifo_notempty_irq_enable;
	
	bool error; // Error, Read Empty/Send Full (0=No Error, 1=Error/Acknowledge)
	bool fifo_enable;
	
	ctr9_iofifo recv_fifo;
	ctr9_iofifo send_fifo;
} ctr9_pxi_state;

static uint64_t ctr9_pxi_read(void* opaque, hwaddr offset, unsigned size)
{
	ctr9_pxi_state* s = (ctr9_pxi_state*)opaque;
	
	uint64_t res = 0;
	switch(offset)
	{
	case PXI_SYNC:
		res = s->recv_count | (s->send_count << 8) | (s->sync_irq_enable << 31);
		break;
	case PXI_SYNC + 1:
		res = s->send_count;
		break;
	case PXI_SYNC + 3:
		res = s->sync_irq_enable << 7;
		break;
	case PXI_CNT:
		res = (ctr9_fifo_len(&s->send_fifo) == 0) | (s->send_fifo.full << 1);
		res |= (s->send_fifo_empty_irq_enable << 2) | (s->recv_fifo_notempty_irq_enable << 10);
		res |= ((ctr9_fifo_len(&s->recv_fifo) == 0) | (s->recv_fifo.full << 1)) << 8;
		res |= (s->error << 14) | (s->fifo_enable << 15);
		break;
	case PXI_RECV:
		res = ctr9_fifo_pop(&s->recv_fifo);
		break;
	default:
		break;
	}
	
	printf("ctr9_pxi_read  0x%03X %X %08X\n", (uint32_t)offset, size, res);
	
	return res;
}

static void ctr9_pxi_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
	ctr9_pxi_state* s = (ctr9_pxi_state*)opaque;
	
	printf("ctr9_pxi_write 0x%03X %X %08X\n", (uint32_t)offset, size, (uint32_t)value);
	
	switch(offset)
	{
	case PXI_SYNC:
		s->send_count = (value >> 8) & 0xFF;
		s->sync_irq_enable = (value >> 31) & 1;
		break;
	case PXI_SYNC + 1:
		s->send_count = value & 0xFF;
		break;
	case PXI_SYNC + 3:
		s->sync_irq_enable = value >> 7;
		break;
	case PXI_CNT:
		s->send_fifo_empty_irq_enable = (value >> 2) & 1;
		s->recv_fifo_notempty_irq_enable = (value >> 10) & 1;
		s->error = ~((value >> 14) & 1);
		s->fifo_enable = (value >> 15) & 1;
		
		if((value >> 3) & 1)
		{
			// flush send fifo
			ctr9_fifo_reset(&s->recv_fifo);
		}
		break;
	case PXI_SEND:
		ctr9_fifo_push(&s->send_fifo, value, 4);
		break;
	default:
		break;
	}
}

static const MemoryRegionOps ctr9_pxi_ops =
{
	.read = ctr9_pxi_read,
	.write = ctr9_pxi_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static int ctr9_pxi_init(SysBusDevice *sbd)
{
	DeviceState *dev = DEVICE(sbd);
	ctr9_pxi_state *s = CTR9_PXI(dev);
	
	int i = 0;
	for(i = 0; i < 3; ++i)
		sysbus_init_irq(sbd, &s->irq[i]);

	memory_region_init_io(&s->iomem, OBJECT(s), &ctr9_pxi_ops, s, "ctr9-pxi", 0x200);
	sysbus_init_mmio(sbd, &s->iomem);
	
	ctr9_fifo_init(&s->recv_fifo, 64);
	ctr9_fifo_init(&s->send_fifo, 64);

	return 0;
}

static const VMStateDescription ctr9_pxi_vmsd = {
	.name = "ctr9-pxi",
	.version_id = 1,
	.minimum_version_id = 1,
	.fields = (VMStateField[]) {
		VMSTATE_END_OF_LIST()
	}
};

static void ctr9_pxi_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

	k->init = ctr9_pxi_init;
	dc->vmsd = &ctr9_pxi_vmsd;
}

static const TypeInfo ctr9_pxi_info = {
	.name          = TYPE_CTR9_PXI,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(ctr9_pxi_state),
	.class_init    = ctr9_pxi_class_init,
};

static void ctr9_pxi_register_types(void)
{
	type_register_static(&ctr9_pxi_info);
}

type_init(ctr9_pxi_register_types)