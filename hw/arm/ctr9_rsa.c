#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "ctr9_common.h"

#define TYPE_CTR9_RSA "ctr9-rsa"
#define CTR9_RSA(obj) \
    OBJECT_CHECK(ctr9_rsa_state, (obj), TYPE_CTR9_RSA)

#define RSA_CNT			0x000
#define RSA_UNK			0x0F0
#define RSA_SLOT0		0x100
#define RSA_SLOT1		0x110
#define RSA_SLOT2		0x120
#define RSA_SLOT3		0x130
#define RSA_EXPFIFO		0x200
#define RSA_MOD			0x400
#define RSA_TXT			0x800

#define RSA_SLOTCNT		0x00
#define RSA_SLOTSIZE	0x04

typedef struct ctr9_rsa_keyslot {
	// RSA_SLOTCNT
	bool set;
	bool key_wr_protect;
	
	// RSA_SLOTSIZE
	uint32_t slot_size;
	
	uint8_t exp[0x100];
	uint8_t mod[0x100];
} ctr9_rsa_keyslot;

typedef struct ctr9_rsa_state {
	SysBusDevice parent_obj;
	MemoryRegion iomem;
	
	qemu_irq irq;
	
	// RSA_CNT
	bool enable;
	bool cntunk;
	uint8_t keyslot;
	bool endian;
	bool order;
	
	// RSA_UNK
	uint32_t unk;
	
	// RSA_SLOT
	ctr9_rsa_keyslot keyslots[4];
	
	// RSA_TXT
	uint8_t text[0x100];
	
	ctr9_iofifo exp_fifo;
} ctr9_rsa_state;

static uint64_t ctr9_rsa_read(void* opaque, hwaddr offset, unsigned size)
{
	ctr9_rsa_state* s = (ctr9_rsa_state*)opaque;
	
	uint64_t res = 0;
	
	if(offset < (RSA_CNT + 4))
	{
		res = s->enable | (s->cntunk << 1) | (s->keyslot << 4) | (s->endian << 8) | (s->order << 9);
	}
	else if(offset >= RSA_UNK && offset < (RSA_UNK + 4))
	{
		res = s->unk;
	}
	else if(offset >= RSA_SLOT0 && offset < (RSA_SLOT3 + 0x10))
	{
		uint32_t keyslot_id = (offset - RSA_SLOT0) / 0x10;
		uint32_t keyslot_offset = (offset - RSA_SLOT0) % 0x10;
		
		ctr9_rsa_keyslot* k = &s->keyslots[keyslot_id];
		switch(keyslot_offset)
		{
		case RSA_SLOTCNT:
			res = k->set | (k->key_wr_protect << 1);
			break;
		case RSA_SLOTSIZE:
			res = k->slot_size;
			break;
		default:
			break;
		}
	}
	else if(offset >= RSA_EXPFIFO && offset < (RSA_EXPFIFO + 4))
	{
		// exp_fifo
	}
	else if(offset >= RSA_MOD && offset < (RSA_MOD + 0x100))
	{
		
	}
	else if(offset >= RSA_TXT && offset < (RSA_TXT + 0x100))
	{
		
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