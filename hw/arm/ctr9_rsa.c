#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "ctr9_common.h"

#ifdef CONFIG_GCRYPT
#include <gcrypt.h>
#endif

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
	bool irq_enable;
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

static void mpi_print(gcry_mpi_t a)
{
	uint8_t p_buf[0x200];
	size_t written; int index;
	gcry_mpi_print(GCRYMPI_FMT_USG, (unsigned char*)p_buf , sizeof(p_buf), &written, a);

	for(index = 0; index < written; index++)
		printf("%02X", (unsigned char) p_buf[index]);
	printf("\n");
}

static void print_be(const uint8_t* src, int size)
{
	int i;
	for(i = 0; i < size; ++i)
		printf("%02X", src[i]);
	printf("\n");
}

static void ctr9_rsa_op(ctr9_rsa_state* s)
{
#ifdef CONFIG_GCRYPT
	ctr9_rsa_keyslot* k = &s->keyslots[s->keyslot];
	gcry_mpi_t mod = 0, exp = 0, plain = 0;

	size_t size = k->slot_size * 4;
	gcry_mpi_scan(&mod, GCRYMPI_FMT_USG, k->mod + (0x100 - size), size, 0);
	gcry_mpi_scan(&exp, GCRYMPI_FMT_USG, k->exp + (0x100 - size), size, 0);
	gcry_mpi_scan(&plain, GCRYMPI_FMT_USG, s->text + (0x100 - size), size, 0);
	
	if(!gcry_mpi_get_nbits(mod) || !gcry_mpi_get_nbits(exp) || !gcry_mpi_get_nbits(plain))
	{
		printf("Invalid values\n");
		return;
	}

	// Raw RSA operation
	gcry_mpi_t cipher = gcry_mpi_new(size * 8);
	gcry_mpi_powm(cipher, plain, exp, mod);
	
	// gcry_mpi_print removes leading zeroes, so we pad manually
	uint32_t pad = (size * 8 - gcry_mpi_get_nbits(cipher)) / 8;
	memset(s->text + (0x100 - size), 0, pad);
	
	// Print the result to our buffer
	gcry_mpi_print(GCRYMPI_FMT_USG, s->text + (0x100 - size) + pad, size, NULL, cipher);
	
	
	gcry_mpi_release(mod);
	gcry_mpi_release(exp);
	gcry_mpi_release(plain);
	gcry_mpi_release(cipher);
#endif
}

static uint64_t ctr9_rsa_read(void* opaque, hwaddr offset, unsigned size)
{
	ctr9_rsa_state* s = (ctr9_rsa_state*)opaque;
	
	uint64_t res = 0;
	
	if(offset < (RSA_CNT + 4))
	{
		res = s->enable | (s->irq_enable << 1) | (s->keyslot << 4) | (s->endian << 8) | (s->order << 9);
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
	else if(offset >= RSA_TXT && offset < (RSA_TXT + 0x100))
	{
		if(size == 1)
			res = s->text[offset - RSA_TXT];
		else if(size == 2)
			res = *(uint16_t*)&s->text[offset - RSA_TXT];
		else if(size == 4)
			res = *(uint32_t*)&s->text[offset - RSA_TXT];
	}
	
	printf("ctr9_rsa_read  0x%03X %X %08X\n", (uint32_t)offset, size, res);
	
	return res;
}

static void ctr9_rsa_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
	ctr9_rsa_state* s = (ctr9_rsa_state*)opaque;
	
	printf("ctr9_rsa_write 0x%03X %X %08X\n", (uint32_t)offset, size, (uint32_t)value);
	
	if(offset < (RSA_CNT + 4))
	{
		s->enable = value & 1;
		s->irq_enable = (value >> 1) & 1;
		s->keyslot = (value >> 4) & 0xF;
		s->endian = (value >> 8) & 1;
		s->order = (value >> 9) & 1;
		
		if(s->enable)
		{
			printf("RSA  *****\n");
			printf(" irq_enable : %d\n", s->irq_enable);
			printf(" keyslot : %d\n", s->keyslot);
			printf(" endian : %d\n", s->endian);
			printf(" order : %d\n", s->order);
			
			ctr9_rsa_op(s);
			
			if(s->irq_enable)
				qemu_irq_raise(s->irq);
			
			s->enable = 0;
		}
	}
	else if(offset >= RSA_UNK && offset < (RSA_UNK + 4))
	{
		s->unk = value;
	}
	else if(offset >= RSA_SLOT0 && offset < (RSA_SLOT3 + 0x10))
	{
		uint32_t keyslot_id = (offset - RSA_SLOT0) / 0x10;
		uint32_t keyslot_offset = (offset - RSA_SLOT0) % 0x10;
		
		ctr9_rsa_keyslot* k = &s->keyslots[keyslot_id];
		switch(keyslot_offset)
		{
		case RSA_SLOTCNT:
			k->set = value & 1;
			k->key_wr_protect = (value >> 1) & 1;
			
			if(!k->set)
				ctr9_fifo_reset(&s->exp_fifo);
			break;
		case RSA_SLOTSIZE:
			k->slot_size = value;
			break;
		default:
			break;
		}
	}
	else if(offset >= RSA_EXPFIFO && offset < (RSA_EXPFIFO + 4))
	{
		ctr9_fifo_push(&s->exp_fifo, value, size);
		
		ctr9_rsa_keyslot* k = &s->keyslots[s->keyslot];
		size_t len = ctr9_fifo_len(&s->exp_fifo);
		k->slot_size = len / 4;
		
		if(len == 0x80 || len == 0x100)
		{
			memcpy(k->exp + (0x100 - len), s->exp_fifo.buffer, len);
			k->set = true;
		}
	}
	else if(offset >= RSA_MOD && offset < (RSA_MOD + 0x100))
	{
		ctr9_rsa_keyslot* k = &s->keyslots[s->keyslot];
		if(size == 1)
			k->mod[offset - RSA_MOD] = value;
		else if(size == 2)
			*(uint16_t*)&k->mod[offset - RSA_MOD] = value;
		else if(size == 4)
			*(uint32_t*)&k->mod[offset - RSA_MOD] = value;
	}
	else if(offset >= RSA_TXT && offset < (RSA_TXT + 0x100))
	{
		if(size == 1)
			s->text[offset - RSA_TXT] = value;
		else if(size == 2)
			*(uint16_t*)&s->text[offset - RSA_TXT] = value;
		else if(size == 4)
			*(uint32_t*)&s->text[offset - RSA_TXT] = value;
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
	
	ctr9_fifo_init(&s->exp_fifo, 0x100);

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