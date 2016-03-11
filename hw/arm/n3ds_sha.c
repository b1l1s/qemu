#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"

#define TYPE_N3DS_SHA "n3ds-sha"
#define N3DS_SHA(obj) \
    OBJECT_CHECK(n3ds_sha_state, (obj), TYPE_N3DS_SHA)

typedef struct n3ds_sha_state {
	SysBusDevice parent_obj;

	MemoryRegion iomem;
	
	uint8_t start;
	uint8_t final;
	uint8_t output_endian;
	uint8_t mode;
	
	uint32_t block_count;
	
	uint8_t hash[0x20];
	
	n3ds_iofifo in_fifo;
} n3ds_sha_state;

static uint64_t n3ds_sha_read(void* opaque, hwaddr offset, unsigned size)
{
	n3ds_sha_state* s = (n3ds_sha_state*)opaque;
	
	uint64_t res = 0;
	switch(offset)
	{
	case 0:
		res = s->start | (s->final << 1) | (s->output_endian << 3) | (s->mode << 4);
		break;
	case 4:
		res = s->block_count;
		break;
	default:
		break;
	}
	
	if(offset >= 0x40 && offset < 0x60)
	{
		if(size == 1)
			res = hash[offset - 0x40];
		if(size == 2)
			res = *(uint16_t*)&hash[offset - 0x40];
		if(size = 4)
			res = *(uint32_t*)&hash[offset - 0x40];
	}
	
	return res;
}

static void n3ds_sha_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
	n3ds_sha_state* s = (n3ds_sha_state*)opaque;
	
	switch(offset)
	{
	case 0:
		s->start = value & 1;
		s->final = (value >> 1) & 1;
		s->output_endian = (value >> 3) & 1;
		s->mode = (value >> 4) & 3;
		break;
	default:
		break;
	}
}

static const MemoryRegionOps n3ds_sha_ops =
{
	.read = n3ds_sha_read,
	.write = n3ds_sha_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static int n3ds_sha_init(SysBusDevice *sbd)
{
	DeviceState *dev = DEVICE(sbd);
	n3ds_sha_state *s = N3DS_SHA(dev);

	memory_region_init_io(&s->iomem, OBJECT(s), &n3ds_sha_ops, s, "n3ds-sha", 0x100);
	sysbus_init_mmio(sbd, &s->iomem);
	
	s->start = 0;
	s->final = 0;
	s->output_endian = 1;
	s->mode = 0;
	
	memset(&s->wr_fifo, 0, sizeof(s->wr_fifo));

	return 0;
}

static const VMStateDescription n3ds_sha_vmsd = {
	.name = "n3ds-sha",
	.version_id = 1,
	.minimum_version_id = 1,
	.fields = (VMStateField[]) {
		VMSTATE_END_OF_LIST()
	}
};

static void n3ds_sha_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

	k->init = n3ds_sha_init;
	dc->vmsd = &n3ds_sha_vmsd;
}

static const TypeInfo n3ds_sha_info = {
	.name          = TYPE_N3DS_SHA,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(n3ds_sha_state),
	.class_init    = n3ds_sha_class_init,
};

static void n3ds_sha_register_types(void)
{
	type_register_static(&n3ds_sha_info);
}

type_init(n3ds_sha_register_types)