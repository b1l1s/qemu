#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"

#define TYPE_N3DS_SHA "n3ds-sha"
#define N3DS_SHA(obj) \
    OBJECT_CHECK(n3ds_sha_state, (obj), TYPE_N3DS_SHA)

typedef struct n3ds_sha_state {
	SysBusDevice parent_obj;

	MemoryRegion iomem;
} n3ds_sha_state;

static uint64_t n3ds_sha_read(void* opaque, hwaddr offset, unsigned size)
{
	n3ds_sha_state* s = (n3ds_sha_state*)opaque;
	
	uint64_t res = 0;
	switch(offset)
	{
	default:
		break;
	}
	return res;
}

static void n3ds_sha_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
	n3ds_sha_state* s = (n3ds_sha_state*)opaque;
	
	switch(offset)
	{
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

	memory_region_init_io(&s->iomem, OBJECT(s), &n3ds_sha_ops, s, "n3ds-sha", 0x200);
	sysbus_init_mmio(sbd, &s->iomem);

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