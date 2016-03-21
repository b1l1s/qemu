#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "ctr9_common.h"

#define TYPE_CTR9_AES "ctr9-aes"
#define CTR9_AES(obj) \
    OBJECT_CHECK(ctr9_aes_state, (obj), TYPE_CTR9_AES)

#define AES_CCM_DECRYPT_MODE	(0u << 27)
#define AES_CCM_ENCRYPT_MODE	(1u << 27)
#define AES_CTR_MODE			(2u << 27)
#define AES_CTR_MODE			(2u << 27)
#define AES_CBC_DECRYPT_MODE	(4u << 27)
#define AES_CBC_ENCRYPT_MODE	(5u << 27)
#define AES_ECB_DECRYPT_MODE	(6u << 27)
#define AES_ECB_ENCRYPT_MODE	(7u << 27)
#define AES_ALL_MODES			(7u << 27)

#define AES_CNT_START			0x80000000
#define AES_CNT_INPUT_ORDER		0x02000000
#define AES_CNT_OUTPUT_ORDER	0x01000000
#define AES_CNT_INPUT_ENDIAN	0x00800000
#define AES_CNT_OUTPUT_ENDIAN	0x00400000
#define AES_CNT_FLUSH_READ		0x00000800
#define AES_CNT_FLUSH_WRITE		0x00000400

#define AES_INPUT_BE			(AES_CNT_INPUT_ENDIAN)
#define AES_INPUT_LE			0
#define AES_INPUT_NORMAL		(AES_CNT_INPUT_ORDER)
#define AES_INPUT_REVERSED		0

#define AES_OUTPUT_BE			(AES_CNT_OUTPUT_ENDIAN)
#define AES_OUTPUT_LE			0
#define AES_OUTPUT_NORMAL		(AES_CNT_OUTPUT_ORDER)
#define AES_OUTPUT_REVERSED		0

typedef struct ctr9_aes_keyslot
{
	uint8_t keys[0x3][0x10];
} ctr9_aes_keyslot;

typedef struct ctr9_aes_keyfifo
{
	uint8_t key_buffer[0x10];
	uint8_t key_buffer_ptr;
} ctr9_aes_keyfifo;

typedef struct ctr9_aes_state {
	SysBusDevice parent_obj;
	MemoryRegion iomem;

	qemu_irq irq;

	ctr9_aes_keyslot keyslots[0x40];
	
	uint8_t output_endian;
	uint8_t input_endian;
	uint8_t output_order;
	uint8_t input_order;
	
	uint8_t mode;
	uint8_t interrupt;
	uint8_t start;
	
	uint32_t block_count;
	
	ctr9_iofifo wr_fifo;
	ctr9_iofifo rd_fifo;
	
	uint8_t keysel;
	uint8_t active_key[0x10];
	
	uint8_t keycnt_key;
	uint8_t scrambler_type;
	uint8_t keyfifo_en;
	
	uint8_t ctr[0x10];
	
	ctr9_aes_keyfifo keyfifos[0x3];
} ctr9_aes_state;

static uint64_t ctr9_aes_read(void* opaque, hwaddr offset, unsigned size)
{
	ctr9_aes_state* s = (ctr9_aes_state*)opaque;
	
	uint64_t res = 0;
	switch(offset)
	{
	case 0x00: // AES_CNT
		res = (ctr9_fifo_len(&s->rd_fifo) / 4) << 5 | (ctr9_fifo_len(&s->wr_fifo) / 4);
		res |= (s->input_order << 3 | s->output_order << 2 | s->input_endian << 1 | s->output_endian) << 22;
		res |= s->mode << 27;
		res |= s->interrupt << 30;
		res |= s->start << 31;
		break;
	case 0x0C: // AES_RDFIFO
		res = ctr9_fifo_pop(&s->rd_fifo);
		break;
	case 0x10: // AES_KEYSEL
		res = s->keysel;
		break;
	case 0x11: // AES_KEYCNT
		res = s->keycnt_key | (s->scrambler_type << 6) | (s->keyfifo_en << 7);
		break;
	default:
		break;
	}

	//printf("ctr9_aes_read  0x%03X %X %08X\n", (uint32_t)offset, size, res);
	
	return res;
}

static void rol_128(uint8_t* value, uint32_t c)
{
	uint64_t* v64 = (uint64_t*)value;
	if(c > 64)
	{
		uint64_t temp = v64[1];
		v64[1] = v64[0];
		v64[0] = temp;
		rol_128(value, c - 64);
	}
	else
	{
		uint64_t temp = v64[1];
		v64[1] = (v64[1] << c) | (v64[0] >> (64 - c));
		v64[0] = (v64[0] << c) | (temp >> (64 - c));
	}
}

static void ror_128(uint8_t* value, uint32_t c)
{
	uint64_t* v64 = (uint64_t*)value;
	if(c > 64)
	{
		uint64_t temp = v64[0];
		v64[0] = v64[1];
		v64[1] = temp;
		ror_128(value, c - 64);
	}
	else
	{
		uint64_t temp = v64[0];
		v64[0] = (v64[0] >> c) | (v64[1] << (64 - c));
		v64[1] = (v64[1] >> c) | (temp << (64 - c));
	}
}

static void add_128(uint8_t* lhs, const uint8_t* rhs)
{
	uint64_t* va64 = (uint64_t*)lhs;
	uint64_t* vb64 = (uint64_t*)rhs;
	va64[0] = va64[0] + vb64[0];
	va64[1] = va64[1] + vb64[1];
	if(va64[0] < vb64[0])
		va64[1]++;
}

static void xor_128(uint8_t* lhs, const uint8_t* rhs)
{
	uint64_t* va64 = (uint64_t*)lhs;
	uint64_t* vb32 = (uint64_t*)rhs;
	va64[0] = va64[0] ^ vb32[0];
	va64[1] = va64[1] ^ vb32[1];
}

static void bswap_128(uint8_t* value)
{
	uint32_t* v32 = (uint32_t*)value;
	uint32_t temp = v32[0];
	v32[0] = __builtin_bswap32(v32[3]);
	v32[3] = __builtin_bswap32(temp);

	temp = v32[1];
	v32[1] = __builtin_bswap32(v32[2]);
	v32[2] = __builtin_bswap32(temp);
}

static void ctr9_aes_keyfifo_scramble(ctr9_aes_state* s)
{
	// 3DS scrambler constant, little endian, reverse word order
	uint8_t C_CTR[0x10] = {
		0x8A, 0x76, 0x52, 0x5D,
		0xDC, 0x91, 0x45, 0x02,
		0x08, 0x04, 0xFE, 0xC5,
		0xAA, 0xE9, 0xF9, 0x1F
	};
	
	// TWL scrambler constant, little endian, reverse word order
	uint8_t C_TWL[0x10] = {
		0x79, 0x3E, 0x4F, 0x1A,
		0x5F, 0x0F, 0x68, 0x2A,
		0x58, 0x02, 0x59, 0x29,
		0x4E, 0xFB, 0xFE, 0xFF
	};
	
	uint8_t* target_key = s->keyslots[s->keycnt_key].keys[0];
	const uint8_t* keyx = s->keyslots[s->keycnt_key].keys[1];
	const uint8_t* keyy = s->keyslots[s->keycnt_key].keys[2];
	
	uint8_t key[0x10];
	uint8_t tkeyy[0x10];

	memcpy(key, keyx, 0x10);
	memcpy(tkeyy, keyy, 0x10);
	
	bswap_128(key);
	bswap_128(tkeyy);
	if(s->keycnt_key < 4 || s->scrambler_type == 1)
	{
		xor_128(key, tkeyy);
		add_128(key, C_TWL);
		rol_128(key, 42);
	}
	else
	{
		rol_128(key, 2);
		xor_128(key, tkeyy);
		add_128(key, C_CTR);
		ror_128(key, 41);
	}
	
	bswap_128(key);

	memcpy(target_key, key, 0x10);
}

static void ctr9_aes_keyfifo_write(ctr9_aes_state* s, int key_type, uint32_t value, uint32_t size)
{
	if(size == 0x1)
		value |= value << 8 | value << 16 | value << 24;
	if(size == 0x2)
		value |= value << 16;
	
	uint8_t* key_buffer, *key_buffer_ptr, *target_key;

	key_buffer = s->keyfifos[key_type].key_buffer;
	key_buffer_ptr = &s->keyfifos[key_type].key_buffer_ptr;
	target_key = s->keyslots[s->keycnt_key].keys[key_type];
	
	key_buffer[*key_buffer_ptr] = value;
	*key_buffer_ptr += 4;
	
	if(*key_buffer_ptr == 0x10)
	{
		// Flush the key
		if(s->input_endian == 0)
		{
			// Little endian
			int i;
			for(i = 0; i < 4; ++i)
				((uint32_t*)target_key)[i] = __builtin_bswap32(((uint32_t*)key_buffer)[i]);
		}
		else
			memcpy(target_key, key_buffer, 0x10);
		
		if(key_type == 2)
			ctr9_aes_keyfifo_scramble(s);
		
		*key_buffer_ptr = 0;
	}
}

static void ctr9_aes_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
	ctr9_aes_state* s = (ctr9_aes_state*)opaque;
	
	//printf("ctr9_aes_write 0x%03X %X %08X\n", (uint32_t)offset, size, (uint32_t)value);
	
	switch(offset)
	{
	case 0x00: // AES_CNT
		s->input_order = (value & AES_CNT_INPUT_ORDER) == AES_INPUT_NORMAL;
		s->output_order = (value & AES_CNT_OUTPUT_ORDER) == AES_OUTPUT_NORMAL;
		s->input_endian = (value & AES_CNT_INPUT_ENDIAN) == AES_INPUT_BE;
		s->output_endian = (value & AES_CNT_OUTPUT_ENDIAN) == AES_OUTPUT_BE;
		s->mode = (value >> 27) & 7;
		s->interrupt = (value >> 30) & 1;
		s->start = (value >> 31) & 1;
		
		if(value & (1 << 26))
		{
			// select keyslot
			memcpy(s->active_key, s->keyslots[s->keysel].keys[0], 0x10);
			printf("AES  *****\n");
			printf(" selected keyslot 0x%02X\n", s->keysel);
		}
		if(s->start)
		{
			/*
			printf("AES  *****\n");
			printf(" input_order 0x%02X\n", s->input_order);
			printf(" output_order 0x%02X\n", s->output_order);
			printf(" input_endian 0x%02X\n", s->input_endian);
			printf(" output_endian 0x%02X\n", s->output_endian);
			printf(" mode 0x%02X\n", s->mode);
			printf(" interrupt 0x%02X\n", s->interrupt);*/
			s->start = 0;
		}
		
		break;
	case 0x04: // AES_BLKCOUNT
		s->block_count = value >> 16;
		break;
	case 0x08: // AES_WRFIFO
		// TODO currently passthrough, we need a proper one
		if(s->block_count)
		{
			ctr9_fifo_push(&s->wr_fifo, value, 4);
			if(ctr9_fifo_len(&s->wr_fifo) == 0x10)
			{
				int i;
				for(i = 0; i < 4; ++i)
					ctr9_fifo_push(&s->rd_fifo, ctr9_fifo_pop(&s->wr_fifo), 4);
				
				s->block_count -= 1;
				if(s->block_count == 0)
				{
					s->start = 0;
				}
			}
		}

		break;
	case 0x10: // AES_KEYSEL
		s->keysel = value & 0x3F;
		break;
	case 0x11: // AES_KEYCNT
		s->keycnt_key = value & 0x3F;
		s->scrambler_type = (value >> 6) & 1;
		s->keyfifo_en = (value >> 7) & 1;
		break;
	case 0x20: // AES_CTR
	case 0x24:
	case 0x28:
	case 0x2C:
		if(size == 4)
			*(uint32_t*)&s->ctr[offset - 0x20] = value;
		break;
	case 0x100 ... 0x103: // AES_KEYFIFO
		ctr9_aes_keyfifo_write(s, 0, value, size);
		break;
	case 0x104 ... 0x107: // AES_KEYXFIFO
		ctr9_aes_keyfifo_write(s, 1, value, size);
		break;
	case 0x108 ... 0x10B: // AES_KEYYFIFO
		ctr9_aes_keyfifo_write(s, 2, value, size);
		break;
	default:
		break;
	}
}

static const MemoryRegionOps ctr9_aes_ops =
{
	.read = ctr9_aes_read,
	.write = ctr9_aes_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static int ctr9_aes_init(SysBusDevice *sbd)
{
	DeviceState *dev = DEVICE(sbd);
	ctr9_aes_state *s = CTR9_AES(dev);

	sysbus_init_irq(sbd, &s->irq);
	
	memory_region_init_io(&s->iomem, OBJECT(s), &ctr9_aes_ops, s, "ctr9-aes", 0x200);
	sysbus_init_mmio(sbd, &s->iomem);
	
	memset(s->keyslots, 0, sizeof(s->keyslots));
	memset(&s->wr_fifo, 0, sizeof(s->wr_fifo));
	memset(&s->rd_fifo, 0, sizeof(s->rd_fifo));
	
	s->output_endian = 1;
	s->input_endian = 1;
	s->output_order = 1;
	s->input_order = 1;
	
	s->mode = 0;
	s->interrupt = 0;
	s->start = 0;
	
	s->block_count = 0;
	
	s->keysel = 0;
	
	s->keycnt_key = 0;
	s->scrambler_type = 0;
	s->keyfifo_en = 0;
	
	memset(s->ctr, 0, sizeof(s->ctr));
	memset(s->keyfifos, 0, sizeof(s->keyfifos));

	return 0;
}

static const VMStateDescription ctr9_aes_vmsd = {
	.name = "ctr9-aes",
	.version_id = 1,
	.minimum_version_id = 1,
	.fields = (VMStateField[]) {
		VMSTATE_END_OF_LIST()
	}
};

static void ctr9_aes_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

	k->init = ctr9_aes_init;
	dc->vmsd = &ctr9_aes_vmsd;
}

static const TypeInfo ctr9_aes_info = {
	.name          = TYPE_CTR9_AES,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(ctr9_aes_state),
	.class_init    = ctr9_aes_class_init,
};

static void ctr9_aes_register_types(void)
{
	type_register_static(&ctr9_aes_info);
}

type_init(ctr9_aes_register_types)