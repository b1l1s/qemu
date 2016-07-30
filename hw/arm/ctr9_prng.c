#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include <stdlib.h>

#define TYPE_CTR9_PRNG "ctr9-prng"
#define CTR9_PRNG(obj) \
    OBJECT_CHECK(ctr9_prng_state, (obj), TYPE_CTR9_PRNG)

typedef struct ctr9_prng_state {
	SysBusDevice parent_obj;

	MemoryRegion iomem;
} ctr9_prng_state;

static uint64_t ctr9_prng_read(void* opaque, hwaddr offset, unsigned size)
{
	return rand();
}

static void ctr9_prng_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
}

static const MemoryRegionOps ctr9_prng_ops =
{
	.read = ctr9_prng_read,
	.write = ctr9_prng_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static int ctr9_prng_init(SysBusDevice *sbd)
{
	DeviceState *dev = DEVICE(sbd);
	ctr9_prng_state *s = CTR9_PRNG(dev);

	memory_region_init_io(&s->iomem, OBJECT(s), &ctr9_prng_ops, s, "ctr9-prng", 0x4);
	sysbus_init_mmio(sbd, &s->iomem);
	
	srand(time(NULL));

	return 0;
}

static const VMStateDescription ctr9_prng_vmsd = {
	.name = "ctr9-prng",
	.version_id = 1,
	.minimum_version_id = 1,
	.fields = (VMStateField[]) {
		VMSTATE_END_OF_LIST()
	}
};

static void ctr9_prng_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

	k->init = ctr9_prng_init;
	dc->vmsd = &ctr9_prng_vmsd;
}

static const TypeInfo ctr9_prng_info = {
	.name          = TYPE_CTR9_PRNG,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(ctr9_prng_state),
	.class_init    = ctr9_prng_class_init,
};

static void ctr9_prng_register_types(void)
{
	type_register_static(&ctr9_prng_info);
}

type_init(ctr9_prng_register_types)