#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"

#define TYPE_CTR9_RSA "ctr9-rsa"
#define CTR9_RSA(obj) \
    OBJECT_CHECK(ctr9_rsa_state, (obj), TYPE_CTR9_RSA)

typedef struct ctr9_rsa_state {
	SysBusDevice parent_obj;
	MemoryRegion iomem;
	
	qemu_irq irq;
} ctr9_rsa_state;

static uint64_t ctr9_rsa_read(void* opaque, hwaddr offset, unsigned size)
{
	ctr9_rsa_state* s = (ctr9_rsa_state*)opaque;
	
	uint64_t res = 0;
	switch(offset)
	{
	default:
		break;
	}
	
	printf("ctr9_rsa_read  0x%03X %X %08X\n", (uint32_t)offset, size, res);
	
	return res;
}

static void ctr9_rsa_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
	ctr9_rsa_state* s = (ctr9_rsa_state*)opaque;
	
	printf("ctr9_rsa_write 0x%03X %X %08X\n", (uint32_t)offset, size, (uint32_t)value);
	
	switch(offset)
	{
	default:
		break;
	}
}

static const MemoryRegionOps ctr9_rsa_ops =
{
	.read = ctr9_rsa_read,
	.write = ctr9_rsa_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static int ctr9_rsa_init(SysBusDevice *sbd)
{
	DeviceState *dev = DEVICE(sbd);
	ctr9_rsa_state *s = CTR9_RSA(dev);
	
	sysbus_init_irq(sbd, &s->irq);

	memory_region_init_io(&s->iomem, OBJECT(s), &ctr9_rsa_ops, s, "ctr9-rsa", 0x1000);
	sysbus_init_mmio(sbd, &s->iomem);

	return 0;
}

static const VMStateDescription ctr9_rsa_vmsd = {
	.name = "ctr9-rsa",
	.version_id = 1,
	.minimum_version_id = 1,
	.fields = (VMStateField[]) {
		VMSTATE_END_OF_LIST()
	}
};

static void ctr9_rsa_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

	k->init = ctr9_rsa_init;
	dc->vmsd = &ctr9_rsa_vmsd;
}

static const TypeInfo ctr9_rsa_info = {
	.name          = TYPE_CTR9_RSA,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(ctr9_rsa_state),
	.class_init    = ctr9_rsa_class_init,
};

static void ctr9_rsa_register_types(void)
{
	type_register_static(&ctr9_rsa_info);
}

type_init(ctr9_rsa_register_types)