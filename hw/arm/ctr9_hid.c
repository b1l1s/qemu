#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "ui/console.h"

#define TYPE_CTR9_HID "ctr9-hid"
#define CTR9_HID(obj) \
    OBJECT_CHECK(ctr9_hid_state, (obj), TYPE_CTR9_HID)

// http://www.philipstorr.id.au/pcbook/book3/scancode.htm
#define KEY_RELEASED			0x80
#define KEY_CODE				0x7f

#define KEYCODE_TAB				0x0f
#define KEYCODE_ENTER			0x1c
#define KEYCODE_F				0x21
#define KEYCODE_H				0x23
#define KEYCODE_J				0x24
#define KEYCODE_M				0x32
#define KEYCODE_N				0x31
#define KEYCODE_U				0x16
#define KEYCODE_Y				0x15

#define KEYCODE_EXTENDED		0xe0
#define KEYCODE_UP				0x48
#define KEYCODE_DOWN			0x50
#define KEYCODE_LEFT			0x4b
#define KEYCODE_RIGHT			0x4d

#define HID_A		((uint32_t)0x001)
#define HID_B		((uint32_t)0x002)
#define HID_SEL		((uint32_t)0x004)
#define HID_START	((uint32_t)0x008)
#define HID_RIGHT	((uint32_t)0x010)
#define HID_LEFT	((uint32_t)0x020)
#define HID_UP		((uint32_t)0x040)
#define HID_DOWN	((uint32_t)0x080)
#define HID_RT		((uint32_t)0x100)
#define HID_LT		((uint32_t)0x200)
#define HID_X		((uint32_t)0x400)
#define HID_Y		((uint32_t)0x800)

typedef struct ctr9_hid_state {
	SysBusDevice parent_obj;

	MemoryRegion iomem;
	uint32_t kbd_extended;
	uint32_t pressed_keys;
	qemu_irq out[8];
} ctr9_hid_state;

static uint64_t ctr9_hid_read(void* opaque, hwaddr offset, unsigned size)
{
	ctr9_hid_state* s = (ctr9_hid_state*)opaque;
	return ~s->pressed_keys;
}

static void ctr9_hid_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
}

static const MemoryRegionOps ctr9_hid_ops =
{
	.read = ctr9_hid_read,
	.write = ctr9_hid_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static void ctr9_hid_event(void *opaque, int keycode)
{
	ctr9_hid_state *s = opaque;
	if (keycode == KEYCODE_EXTENDED) {
		s->kbd_extended = 1;
		return;
	}
	
	uint32_t event = 0;
	switch (keycode & KEY_CODE) {
	case KEYCODE_UP:
		event = HID_UP;
		break;

	case KEYCODE_DOWN:
		event = HID_DOWN;
		break;

	case KEYCODE_LEFT:
		event = HID_LEFT;
		break;

	case KEYCODE_RIGHT:
		event = HID_RIGHT;
		break;
	
	case KEYCODE_M:
		event = HID_A;
		break;

	case KEYCODE_N:
		event = HID_B;
		break;

	case KEYCODE_J:
		event = HID_X;
		break;

	case KEYCODE_H:
		event = HID_Y;
		break;
		
	case KEYCODE_U:
		event = HID_RT;
		break;

	case KEYCODE_Y:
		event = HID_LT;
		break;
	}

	// Do not repeat already pressed buttons
	if (!(keycode & KEY_RELEASED) && (s->pressed_keys & event)) {
		event = 0;
	}
	
	if (event) {
		if (keycode & KEY_RELEASED) {
			s->pressed_keys &= ~event;
		} else {
			s->pressed_keys |= event;
		}
	}

	s->kbd_extended = 0;
}

static int ctr9_hid_init(SysBusDevice *sbd)
{
	DeviceState *dev = DEVICE(sbd);
	ctr9_hid_state *s = CTR9_HID(dev);

	memory_region_init_io(&s->iomem, OBJECT(s), &ctr9_hid_ops, s, "ctr9-hid", 4);
	sysbus_init_mmio(sbd, &s->iomem);

	s->kbd_extended = 0;
	s->pressed_keys = 0;

	qdev_init_gpio_out(dev, s->out, ARRAY_SIZE(s->out));

	qemu_add_kbd_event_handler(ctr9_hid_event, s);

	return 0;
}

static const VMStateDescription ctr9_hid_vmsd = {
	.name = "ctr9-hid",
	.version_id = 1,
	.minimum_version_id = 1,
	.fields = (VMStateField[]) {
		VMSTATE_UINT32(kbd_extended, ctr9_hid_state),
		VMSTATE_UINT32(pressed_keys, ctr9_hid_state),
		VMSTATE_END_OF_LIST()
	}
};

static void ctr9_hid_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

	k->init = ctr9_hid_init;
	dc->vmsd = &ctr9_hid_vmsd;
}

static const TypeInfo ctr9_hid_info = {
	.name          = TYPE_CTR9_HID,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(ctr9_hid_state),
	.class_init    = ctr9_hid_class_init,
};

static void ctr9_hid_register_types(void)
{
	type_register_static(&ctr9_hid_info);
}

type_init(ctr9_hid_register_types)