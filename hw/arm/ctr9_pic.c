#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"

#define TYPE_CTR9_PIC "ctr9-pic"
#define CTR9_PIC(obj) \
    OBJECT_CHECK(ctr9_pic_state, (obj), TYPE_CTR9_PIC)

typedef struct ctr9_pic_state {
	SysBusDevice parent_obj;
	MemoryRegion iomem;
	
	uint32_t enabled;
	uint32_t pending;
	qemu_irq parent_irq;
} ctr9_pic_state;

static void ctr9_pic_update(ctr9_pic_state *s)
{
	qemu_set_irq(s->parent_irq, (s->pending & s->enabled));
}

static uint64_t ctr9_pic_read(void* opaque, hwaddr offset, unsigned size)
{
	ctr9_pic_state* s = (ctr9_pic_state*)opaque;
	
	uint64_t res = 0;
	switch(offset)
	{
	case 0:
		res = s->enabled;
		break;
	case 4:
		res = s->pending;
		break;
	default:
		break;
	}
	return res;
}

static void ctr9_pic_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
	ctr9_pic_state* s = (ctr9_pic_state*)opaque;
	
	switch(offset)
	{
	case 0:
		s->enabled = value;
		break;
	case 4:
		// Clear pending interrupt bits by writing 1 to it
		s->pending &= ~value;
		break;
	default:
		break;
	}
	
	ctr9_pic_update(s);
}

static const MemoryRegionOps ctr9_pic_ops =
{
	.read = ctr9_pic_read,
	.write = ctr9_pic_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static void ctr9_pic_set_irq(void *opaque, int irq, int level)
{
	ctr9_pic_state *s = opaque;
	
	if(((1 << irq) & s->enabled) == 0)
		return;

	if(level)
		s->pending |= 1 << irq;
	else
		s->pending &= ~(1 << irq);

	ctr9_pic_update(s);
}

static int ctr9_pic_init(SysBusDevice *sbd)
{
	DeviceState *dev = DEVICE(sbd);
	ctr9_pic_state *s = CTR9_PIC(dev);

	qdev_init_gpio_in(DEVICE(dev), ctr9_pic_set_irq, 32);
	sysbus_init_irq(sbd, &s->parent_irq);
	memory_region_init_io(&s->iomem, OBJECT(s), &ctr9_pic_ops, s, "ctr9-pic", 0x8);
	sysbus_init_mmio(sbd, &s->iomem);
	
	s->enabled = 0;
	s->pending = 0;

	return 0;
}

static const VMStateDescription ctr9_pic_vmsd = {
	.name = "ctr9-pic",
	.version_id = 1,
	.minimum_version_id = 1,
	.fields = (VMStateField[]) {
		VMSTATE_END_OF_LIST()
	}
};

static void ctr9_pic_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

	k->init = ctr9_pic_init;
	dc->vmsd = &ctr9_pic_vmsd;
}

static const TypeInfo ctr9_pic_info = {
	.name          = TYPE_CTR9_PIC,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(ctr9_pic_state),
	.class_init    = ctr9_pic_class_init,
};

static void ctr9_pic_register_types(void)
{
	type_register_static(&ctr9_pic_info);
}

type_init(ctr9_pic_register_types)