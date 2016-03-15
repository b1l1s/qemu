#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"

#define TYPE_CTR9_SHA "ctr9-sha"
#define CTR9_SHA(obj) \
    OBJECT_CHECK(ctr9_sha_state, (obj), TYPE_CTR9_SHA)

typedef struct ctr9_sha_state {
	SysBusDevice parent_obj;

	MemoryRegion iomem;
} ctr9_sha_state;

static uint64_t ctr9_sha_read(void* opaque, hwaddr offset, unsigned size)
{
	ctr9_sha_state* s = (ctr9_sha_state*)opaque;
	
	uint64_t res = 0;
	switch(offset)
	{
	default:
		break;
	}
	return res;
}

static void ctr9_sha_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
	ctr9_sha_state* s = (ctr9_sha_state*)opaque;
	
	switch(offset)
	{
	default:
		break;
	}
}

static const MemoryRegionOps ctr9_sha_ops =
{
	.read = ctr9_sha_read,
	.write = ctr9_sha_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static int ctr9_sha_init(SysBusDevice *sbd)
{
	DeviceState *dev = DEVICE(sbd);
	ctr9_sha_state *s = CTR9_SHA(dev);

	memory_region_init_io(&s->iomem, OBJECT(s), &ctr9_sha_ops, s, "ctr9-sha", 0x200);
	sysbus_init_mmio(sbd, &s->iomem);

	return 0;
}

static const VMStateDescription ctr9_sha_vmsd = {
	.name = "ctr9-sha",
	.version_id = 1,
	.minimum_version_id = 1,
	.fields = (VMStateField[]) {
		VMSTATE_END_OF_LIST()
	}
};

static void ctr9_sha_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

	k->init = ctr9_sha_init;
	dc->vmsd = &ctr9_sha_vmsd;
}

static const TypeInfo ctr9_sha_info = {
	.name          = TYPE_CTR9_SHA,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(ctr9_sha_state),
	.class_init    = ctr9_sha_class_init,
};

static void ctr9_sha_register_types(void)
{
	type_register_static(&ctr9_sha_info);
}

type_init(ctr9_sha_register_types)