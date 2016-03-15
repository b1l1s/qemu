#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"

#define TYPE_N3DS_PIC "n3ds-pic"
#define N3DS_PIC(obj) \
    OBJECT_CHECK(n3ds_pic_state, (obj), TYPE_N3DS_PIC)

typedef struct n3ds_pic_state {
	SysBusDevice parent_obj;
	MemoryRegion iomem;
	
	uint32_t enabled;
	uint32_t pending;
	qemu_irq parent_irq;
} n3ds_pic_state;

static void n3ds_pic_update(n3ds_pic_state *s)
{
	qemu_set_irq(s->parent_irq, (s->pending & s->enabled));
}

static uint64_t n3ds_pic_read(void* opaque, hwaddr offset, unsigned size)
{
	n3ds_pic_state* s = (n3ds_pic_state*)opaque;
	
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

static void n3ds_pic_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
	n3ds_pic_state* s = (n3ds_pic_state*)opaque;
	
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
	
	n3ds_pic_update(s);
}

static const MemoryRegionOps n3ds_pic_ops =
{
	.read = n3ds_pic_read,
	.write = n3ds_pic_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static void n3ds_pic_set_irq(void *opaque, int irq, int level)
{
	n3ds_pic_state *s = opaque;
	
	if(((1 << irq) & s->enabled) == 0)
		return;

	if(level)
		s->pending |= 1 << irq;
	else
		s->pending &= ~(1 << irq);

	n3ds_pic_update(s);
}

static int n3ds_pic_init(SysBusDevice *sbd)
{
	DeviceState *dev = DEVICE(sbd);
	n3ds_pic_state *s = N3DS_PIC(dev);

	qdev_init_gpio_in(DEVICE(dev), n3ds_pic_set_irq, 32);
	sysbus_init_irq(sbd, &s->parent_irq);
	memory_region_init_io(&s->iomem, OBJECT(s), &n3ds_pic_ops, s, "n3ds-pic", 0x8);
	sysbus_init_mmio(sbd, &s->iomem);
	
	s->enabled = 0;
	s->pending = 0;

	return 0;
}

static const VMStateDescription n3ds_pic_vmsd = {
	.name = "n3ds-pic",
	.version_id = 1,
	.minimum_version_id = 1,
	.fields = (VMStateField[]) {
		VMSTATE_END_OF_LIST()
	}
};

static void n3ds_pic_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

	k->init = n3ds_pic_init;
	dc->vmsd = &n3ds_pic_vmsd;
}

static const TypeInfo n3ds_pic_info = {
	.name          = TYPE_N3DS_PIC,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(n3ds_pic_state),
	.class_init    = n3ds_pic_class_init,
};

static void n3ds_pic_register_types(void)
{
	type_register_static(&n3ds_pic_info);
}

type_init(n3ds_pic_register_types)