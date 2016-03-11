#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "ui/pixel_ops.h"
#include "ui/console.h"
#include "hw/display/framebuffer.h"

#define TYPE_N3DS_LCDFB "n3ds-lcdfb"
#define N3DS_LCDFB(obj) \
    OBJECT_CHECK(N3dsLcdfbState, (obj), TYPE_N3DS_LCDFB)

#define N3DS_LCD_TOP 0
#define N3DS_LCD_SUB 1

typedef struct n3ds_fb_data
{
	MemoryRegionSection fbsection;
	uint32_t width;
	uint32_t height;
	
	uint32_t addr;
} n3ds_fb_data;

struct N3dsLcdfbState {
	SysBusDevice parent_obj;

	MemoryRegion regs_region;
	QemuConsole *con;
	
	uint32_t width;
	uint32_t height;
	
	n3ds_fb_data fbs[2];

	int invalidate;
	uint32_t fb_offset;
	uint32_t fb_mask;
};
typedef struct N3dsLcdfbState N3dsLcdfbState;

static void n3ds_update_display(DisplaySurface *ds, MemoryRegionSection *mem_section, int width, int height, int dst_stride, int dstxoff, int dstyoff)
{
	MemoryRegion* mem = mem_section->mr;
	if (!mem) {
		return;
	}
	memory_region_sync_dirty_bitmap(mem);
	
	ram_addr_t addr = mem_section->offset_within_region;

	uint8_t* src = memory_region_get_ram_ptr(mem) + addr;
	uint8_t* dest = surface_data(ds);
	
	dest += dst_stride * dstyoff;
	
	int x, y;
	for(y = 0; y < height; ++y)
	{
		for(x = 0; x < width; ++x)
		{
			dest[(x + dstxoff) * 4 + y * dst_stride + 0] = src[(height - 1 - y) * 3 + x * height * 3 + 0];
			dest[(x + dstxoff) * 4 + y * dst_stride + 1] = src[(height - 1 - y) * 3 + x * height * 3 + 1];
			dest[(x + dstxoff) * 4 + y * dst_stride + 2] = src[(height - 1 - y) * 3 + x * height * 3 + 2];
			dest[(x + dstxoff) * 4 + y * dst_stride + 3] = 0xFF;
		}
	}
}

static void vgafb_update_display(void *opaque)
{
	N3dsLcdfbState *s = opaque;
	SysBusDevice *sbd = SYS_BUS_DEVICE(s);
	DisplaySurface *surface = qemu_console_surface(s->con);
	
	// We are assuming that the destination buffer is 32bpp here because lazy
	
	if (s->invalidate) {
		framebuffer_update_memory_section(&s->fbs[N3DS_LCD_TOP].fbsection,
											sysbus_address_space(sbd),
											s->fbs[N3DS_LCD_TOP].addr,
											s->fbs[N3DS_LCD_TOP].height, s->fbs[N3DS_LCD_TOP].width * 3);
											
		framebuffer_update_memory_section(&s->fbs[N3DS_LCD_SUB].fbsection,
											sysbus_address_space(sbd),
											s->fbs[N3DS_LCD_SUB].addr,
											s->fbs[N3DS_LCD_SUB].height, s->fbs[N3DS_LCD_SUB].width * 3);
	}
	
	n3ds_update_display(surface, &s->fbs[N3DS_LCD_TOP].fbsection,
						s->fbs[N3DS_LCD_TOP].width, s->fbs[N3DS_LCD_TOP].height,
						s->width * 4, 0, 0
	);
	
	n3ds_update_display(surface, &s->fbs[N3DS_LCD_SUB].fbsection,
						s->fbs[N3DS_LCD_SUB].width, s->fbs[N3DS_LCD_SUB].height,
						s->width * 4, (s->width - s->fbs[N3DS_LCD_SUB].width) / 2, s->fbs[N3DS_LCD_TOP].height
	);

	dpy_gfx_update(s->con, 0, 0, s->width, s->height);
	s->invalidate = 0;
}

static void vgafb_invalidate_display(void *opaque)
{
	N3dsLcdfbState *s = opaque;
	s->invalidate = 1;
}

static int n3ds_lcdfb_post_load(void *opaque, int version_id)
{
	vgafb_invalidate_display(opaque);
	return 0;
}
/*
static void vgafb_resize(N3dsLcdfbState *s)
{
	if (!vgafb_enabled(s)) {
		return;
	}

	qemu_console_resize(s->con, 400, 480);
	s->invalidate = 1;
}*/

static uint64_t vgafb_read(void *opaque, hwaddr addr, unsigned size)
{
	uint32_t r = 0;

	return r;
}

static void vgafb_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
}

static const MemoryRegionOps vgafb_mmio_ops = {
    .read = vgafb_read,
    .write = vgafb_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void n3ds_lcdfb_reset(DeviceState *d)
{
    //N3dsLcdfbState *s = N3DS_LCDFB(d);
}

static const GraphicHwOps vgafb_ops = {
    .invalidate  = vgafb_invalidate_display,
    .gfx_update  = vgafb_update_display,
};

static int n3ds_lcdfb_init(SysBusDevice *dev)
{
	N3dsLcdfbState *s = N3DS_LCDFB(dev);

	memory_region_init_io(&s->regs_region, OBJECT(s), &vgafb_mmio_ops, s,
			"n3ds-lcdfb", 0);
	sysbus_init_mmio(dev, &s->regs_region);

	s->con = graphic_console_init(DEVICE(dev), 0, &vgafb_ops, s);
	
	s->invalidate = 1;
	s->fbs[N3DS_LCD_TOP].width = 400;
	s->fbs[N3DS_LCD_TOP].height = 240;
	s->fbs[N3DS_LCD_TOP].addr = 0x20000000;
	
	s->fbs[N3DS_LCD_SUB].width = 320;
	s->fbs[N3DS_LCD_SUB].height = 240;
	s->fbs[N3DS_LCD_SUB].addr = 0x20046500;
	
	s->width = s->fbs[N3DS_LCD_TOP].width;
	s->height = s->fbs[N3DS_LCD_TOP].height + s->fbs[N3DS_LCD_SUB].height;
	
	// Setup CakeHax style FB struct
	struct draw_s
	{
		uint32_t top_left;
		uint32_t top_right;
		uint32_t sub;
	} draw;
	draw.top_left = s->fbs[N3DS_LCD_TOP].addr;
	draw.top_right = s->fbs[N3DS_LCD_TOP].addr;
	draw.sub = s->fbs[N3DS_LCD_SUB].addr;
	
	cpu_physical_memory_write(0x23FFFE00, &draw, sizeof(draw));
	
	qemu_console_resize(s->con, s->width, s->height);

	return 0;
}

static const VMStateDescription vmstate_n3ds_lcdfb = {
	.name = "n3ds-lcdfb",
	.version_id = 1,
	.minimum_version_id = 1,
	.post_load = n3ds_lcdfb_post_load,
	.fields = (VMStateField[]) {
		VMSTATE_END_OF_LIST()
	}
};

static Property n3ds_lcdfb_properties[] = {
	DEFINE_PROP_UINT32("fb_offset", N3dsLcdfbState, fb_offset, 0x0),
	DEFINE_PROP_UINT32("fb_mask", N3dsLcdfbState, fb_mask, 0xffffffff),
	DEFINE_PROP_END_OF_LIST(),
};

static void n3ds_lcdfb_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

	k->init = n3ds_lcdfb_init;
	dc->reset = n3ds_lcdfb_reset;
	dc->vmsd = &vmstate_n3ds_lcdfb;
	dc->props = n3ds_lcdfb_properties;
}

static const TypeInfo n3ds_lcdfb_info = {
	.name          = TYPE_N3DS_LCDFB,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(N3dsLcdfbState),
	.class_init    = n3ds_lcdfb_class_init,
};

static void n3ds_lcdfb_register_types(void)
{
	type_register_static(&n3ds_lcdfb_info);
}

type_init(n3ds_lcdfb_register_types)