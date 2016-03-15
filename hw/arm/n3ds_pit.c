#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "hw/ptimer.h"
#include "qemu/main-loop.h"

#define TYPE_N3DS_PIT "n3ds-pit"
#define N3DS_PIT(obj) \
    OBJECT_CHECK(n3ds_pit_state, (obj), TYPE_N3DS_PIT)

#define BASE_FREQ (67027964ll)
//#define BASE_FREQ (100000ll)

static const uint32_t prescaler_table[] = 
{
	1, 64, 256, 1024
};

typedef struct n3ds_timer_state n3ds_timer_state;

typedef struct n3ds_timer_state
{
	ptimer_state *ptimer;
	uint32_t limit;
	uint32_t prescale;
	uint32_t irq_enable;
	uint32_t start;
	uint32_t counter; // counter mode
	uint16_t counter_val;
	qemu_irq irq;
	
	n3ds_timer_state* next;
} n3ds_timer_state;

static void n3ds_timer_trigger(void *opaque)
{
	n3ds_timer_state *s = opaque;
	
	if(s->irq_enable)
		qemu_irq_raise(s->irq);

	n3ds_timer_state* n = s->next;
	if(n && n->counter && n->start)
	{
		if(n->counter_val == 0xFFFF)
			n3ds_timer_trigger(n);

		n->counter_val++;
	}
}

static void n3ds_timer_init(SysBusDevice *dev, n3ds_timer_state *s, n3ds_timer_state *n, uint32_t prescale)
{
	QEMUBH *bh;

	sysbus_init_irq(dev, &s->irq);
	s->prescale = prescale;
	s->counter = 0;
	s->irq_enable = 0;
	s->start = 0;
	s->next = n;
	
	bh = qemu_bh_new(n3ds_timer_trigger, s);
	s->ptimer = ptimer_init(bh);
	ptimer_set_limit(s->ptimer, 0xFFFF, 1);
}

typedef struct n3ds_pit_state
{
	SysBusDevice parent_obj;
	MemoryRegion iomem;
	
	n3ds_timer_state timers[4];
} n3ds_pit_state;

static uint16_t n3ds_pit_read_cnt(n3ds_timer_state* t)
{
	return t->prescale | (t->counter << 2) | (t->irq_enable << 6) | (t->start << 7);
}

static uint64_t n3ds_pit_read(void* opaque, hwaddr offset, unsigned size)
{
	n3ds_pit_state* s = (n3ds_pit_state*)opaque;
	
	uint64_t res = 0;
	int timer_id = offset >> 2;
	n3ds_timer_state* t = &s->timers[timer_id];
	if(((offset & 2) == 2) && size <= 2)
	{
		res = n3ds_pit_read_cnt(t);
	}
	else
	{
		// REG_TIMER_VAL
		if(t->counter)
			res = t->counter_val;
		else
			res = 0xFFFF - ptimer_get_count(t->ptimer);
		
		if(size == 4)
			res |= n3ds_pit_read_cnt(t) << 16;
	}

	return res;
}

static void n3ds_pit_write_cnt(n3ds_timer_state* t, uint16_t value)
{
	// REG_TIMER_CNT
	t->prescale = value & 3;
	t->counter = (value >> 2) & 1;
	t->irq_enable = (value >> 6) & 1;
	if((value >> 7) & 1)
	{
		t->start = 1;
		ptimer_set_freq(t->ptimer, BASE_FREQ / prescaler_table[t->prescale]);
		ptimer_run(t->ptimer, 0);
	}
	else
	{
		t->start = 0;
		ptimer_stop(t->ptimer);
	}
}

static void n3ds_pit_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
	n3ds_pit_state* s = (n3ds_pit_state*)opaque;
	
	int timer_id = offset >> 2;
	n3ds_timer_state* t = &s->timers[timer_id];
	if(((offset & 2) == 2) && size <= 2)
	{
		n3ds_pit_write_cnt(t, value);
	}
	else
	{
		// REG_TIMER_VAL
		t->counter_val = value & 0xFFFF;
		ptimer_set_count(t->ptimer, 0xFFFF - (value & 0xFFFF));
		
		if(size == 4)
			n3ds_pit_write_cnt(t, value >> 16);
	}
}

static void n3ds_pit_reset(DeviceState *d)
{
	n3ds_pit_state *s = N3DS_PIT(d);

	int i;
	for(i = 0; i < 4; i++)
	{
		ptimer_stop(s->timers[i].ptimer);
		s->timers[i].limit = 0;
	}
}

static const MemoryRegionOps n3ds_pit_ops =
{
	.read = n3ds_pit_read,
	.write = n3ds_pit_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static int n3ds_pit_init(SysBusDevice *sbd)
{
	DeviceState *dev = DEVICE(sbd);
	n3ds_pit_state *s = N3DS_PIT(dev);
	
	int i;
	for(i = 0; i < 4; i++)
	{
		n3ds_timer_init(sbd, &s->timers[i], &s->timers[(i + 1) % 4], 1);
	}

	memory_region_init_io(&s->iomem, OBJECT(s), &n3ds_pit_ops, s, "n3ds-pit", 0x10);
	sysbus_init_mmio(sbd, &s->iomem);

	return 0;
}

static const VMStateDescription n3ds_pit_vmsd = {
	.name = "n3ds-pit",
	.version_id = 1,
	.minimum_version_id = 1,
	.fields = (VMStateField[]) {
		VMSTATE_END_OF_LIST()
	}
};

static void n3ds_pit_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

	k->init = n3ds_pit_init;
	dc->reset = n3ds_pit_reset;
	dc->vmsd = &n3ds_pit_vmsd;
}

static const TypeInfo n3ds_pit_info = {
	.name          = TYPE_N3DS_PIT,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(n3ds_pit_state),
	.class_init    = n3ds_pit_class_init,
};

static void n3ds_pit_register_types(void)
{
	type_register_static(&n3ds_pit_info);
}

type_init(n3ds_pit_register_types)