#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "ctr9_common.h"

#ifdef CONFIG_GCRYPT
#include <gcrypt.h>

static const uint32_t aes_mode_map[] = {GCRY_CIPHER_MODE_CCM, GCRY_CIPHER_MODE_CTR, GCRY_CIPHER_MODE_CBC, GCRY_CIPHER_MODE_ECB};
#endif

#define TYPE_CTR9_AES "ctr9-aes"
#define CTR9_AES(obj) \
    OBJECT_CHECK(ctr9_aes_state, (obj), TYPE_CTR9_AES)

#define AES_CNT					0x000
#define AES_BLKCOUNT			0x004
#define AES_WRFIFO				0x008
#define AES_RDFIFO				0x00C
#define AES_KEYSEL				0x010
#define AES_KEYCNT				0x011
#define AES_CTR					0x020
#define AES_MAC					0x030
#define AES_TWLKEYS				0x040
#define AES_KEYFIFO				0x100
#define AES_KEYXFIFO			0x104
#define AES_KEYYFIFO			0x108

#define AES_CCM_DECRYPT_MODE	(0u)
#define AES_CCM_ENCRYPT_MODE	(1u)
#define AES_CTR_MODE			(2u)
#define AES_CTR_MODE			(2u)
#define AES_CBC_DECRYPT_MODE	(4u)
#define AES_CBC_ENCRYPT_MODE	(5u)
#define AES_ECB_DECRYPT_MODE	(6u)
#define AES_ECB_ENCRYPT_MODE	(7u)
#define AES_ALL_MODES			(7u)

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

#define AES_KEYN				0
#define AES_KEYX				1
#define AES_KEYY				2

typedef struct ctr9_aes_keyslot
{
	uint8_t keys[0x3][0x10]; // little endian, reverse order
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
	qemu_irq ndma_gpio[2];

	ctr9_aes_keyslot keyslots[0x40];
	
	bool output_endian;
	bool input_endian;
	bool output_order;
	bool input_order;
	
	uint8_t unk;
	
	uint8_t mode;
	bool irq_enable;
	bool start;
	
	uint32_t block_count;
	
	ctr9_iofifo wr_fifo;
	ctr9_iofifo rd_fifo;
	
	uint8_t keysel;
	uint8_t active_key[0x10]; // big endian, normal order
	
	uint8_t keycnt_key;
	uint8_t scrambler_type;
	uint8_t keyfifo_en;
	
	uint8_t ctr[0x10];
	ctr9_aes_keyfifo keyfifos[0x3];
	
	gcry_cipher_hd_t hd;
} ctr9_aes_state;

static void print_be(const uint8_t* src, int size)
{
	int i;
	for(i = 0; i < size; ++i)
		printf("%02X", src[i]);
	printf("\n");
}

static uint64_t ctr9_aes_read(void* opaque, hwaddr offset, unsigned size)
{
	ctr9_aes_state* s = (ctr9_aes_state*)opaque;
	
	uint64_t res = 0;
	switch(offset)
	{
	case AES_CNT:
		res = (ctr9_fifo_len(&s->rd_fifo) / 4) << 5 | (ctr9_fifo_len(&s->wr_fifo) / 4);
		res |= (s->input_order << 3 | s->output_order << 2 | s->input_endian << 1 | s->output_endian) << 22;
		res |= s->unk << 12;
		res |= s->mode << 27;
		res |= s->irq_enable << 30;
		res |= s->start << 31;
		break;
	case AES_RDFIFO:
		res = ctr9_fifo_pop(&s->rd_fifo);
		break;
	case AES_KEYSEL:
		res = s->keysel;
		break;
	case AES_KEYCNT:
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

static void ctr9_aes_keyfifo_scramble(ctr9_aes_state* s, uint32_t keyslot)
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
	
	uint8_t* target_key = s->keyslots[keyslot].keys[AES_KEYN];
	const uint8_t* keyx = s->keyslots[keyslot].keys[AES_KEYX];
	const uint8_t* keyy = s->keyslots[keyslot].keys[AES_KEYY];
	
	printf("AES  *****\n");
	printf(" scramble, slot : 0x%02X\n", keyslot);
	printf(" endian, order %d %d %d %d\n", s->input_endian, s->input_order, s->output_endian, s->output_order);
	printf(" keyx "); print_be(keyx, 0x10);
	printf(" keyy "); print_be(keyy, 0x10);
	
	uint8_t key[0x10];
	memcpy(key, keyx, 0x10);

	if(keyslot < 4 || s->scrambler_type == 1)
	{
		xor_128(key, keyy);
		add_128(key, C_TWL);
		rol_128(key, 42);
	}
	else
	{
		rol_128(key, 2);
		xor_128(key, keyy);
		add_128(key, C_CTR);
		ror_128(key, 41);
	}

	memcpy(target_key, key, 0x10);
	printf(" keyn "); print_be(target_key, 0x10);
}

static void ctr9_aes_keyfifo_write(ctr9_aes_state* s, int keytype, uint32_t value, uint32_t size)
{
	if(size == 0x1)
		value |= value << 8 | value << 16 | value << 24;
	else if(size == 0x2)
		value |= value << 16;
	
	uint8_t* key_buffer, *key_buffer_ptr, *target_key;

	key_buffer = s->keyfifos[keytype].key_buffer;
	key_buffer_ptr = &s->keyfifos[keytype].key_buffer_ptr;
	target_key = s->keyslots[s->keycnt_key].keys[keytype];
	
	*(uint32_t*)&key_buffer[*key_buffer_ptr] = s->input_endian ? __builtin_bswap32(value) : value;
	*key_buffer_ptr += 4;
	
	if(*key_buffer_ptr == 0x10)
	{
		// Flush the key
		int i;
		if(s->input_order)
		{
			for(i = 0; i < 4; ++i)
				((uint32_t*)target_key)[3 - i] = ((uint32_t*)key_buffer)[i];
		}
		else
			memcpy(target_key, key_buffer, 0x10);

		printf("AES  *****\n");
		printf(" keyset, slot : 0x%02X, type : %d\n key : ", s->keycnt_key, keytype);
		print_be(target_key, 0x10);
		
		if(keytype == AES_KEYY)
			ctr9_aes_keyfifo_scramble(s, s->keycnt_key);
		
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
		s->unk = (value >> 12) & 3;
		s->mode = (value >> 27) & 7;
		s->irq_enable = (value >> 30) & 1;
		s->start = (value >> 31) & 1;
		
		if(value & (1 << 26))
		{
			// select keyslot
			memcpy(s->active_key, s->keyslots[s->keysel].keys[0], 0x10);
			bswap_128(s->active_key);

			printf("AES  *****\n");
			printf(" selected keyslot 0x%02X\n", s->keysel);
		}
		if(s->start)
		{
			printf("AES  *****\n");
			printf(" input_order 0x%02X\n", s->input_order);
			printf(" output_order 0x%02X\n", s->output_order);
			printf(" input_endian 0x%02X\n", s->input_endian);
			printf(" output_endian 0x%02X\n", s->output_endian);
			printf(" mode 0x%02X\n", s->mode);
			printf(" irq_enable 0x%02X\n", s->irq_enable);
			printf(" block_count 0x%08X\n", s->block_count);
			printf(" key    "); print_be(s->active_key, 0x10);
			printf(" CTR/IV "); print_be(s->ctr, 0x10);
			printf(" keyslot 0x%02X\n", s->keysel);
			
			if(s->keysel < 4 || s->keysel == 0x11)
			{
				gcry_cipher_open(&s->hd, GCRY_CIPHER_AES128, aes_mode_map[s->mode / 2], 0);
				gcry_cipher_setkey(s->hd, s->active_key, 0x10);
				
				uint8_t ctr[0x10];
				memcpy(ctr, s->ctr, 0x10);
				bswap_128(ctr);
				if(s->mode == AES_CTR_MODE)
					gcry_cipher_setctr(s->hd, ctr, 0x10);
				else if(s->mode == AES_CBC_DECRYPT_MODE || s->mode == AES_CBC_ENCRYPT_MODE)
					gcry_cipher_setiv(s->hd, ctr, 0x10);
			}
			
			qemu_irq_raise(s->ndma_gpio[0]);
		}
		
		break;
	case AES_BLKCOUNT:
		s->block_count = value >> 16;
		break;
	case AES_BLKCOUNT + 2:
		s->block_count = value;
		break;
	case AES_WRFIFO:
		if(s->start && s->block_count)
		{
			ctr9_fifo_push(&s->wr_fifo, value, 4);
			if(ctr9_fifo_len(&s->wr_fifo) == 0x10)
			{
				int i;
				if(s->keysel < 4 || s->keysel == 0x11)
				{
					uint32_t cipher[4];
					if(s->mode % 2)
						gcry_cipher_encrypt(s->hd, cipher, 0x10, s->wr_fifo.buffer, 0x10);
					else
						gcry_cipher_decrypt(s->hd, cipher, 0x10, s->wr_fifo.buffer, 0x10);

					ctr9_fifo_reset(&s->wr_fifo);

					for(i = 0; i < 4; ++i)
						ctr9_fifo_push(&s->rd_fifo, cipher[i], 4);
				}
				else
				{
					for(i = 0; i < 4; ++i)
						ctr9_fifo_push(&s->rd_fifo, ctr9_fifo_pop(&s->wr_fifo), 4);
				}
				
				// trigger NDMA gpio
				if((128 - ctr9_fifo_len(&s->rd_fifo)) >= 0x10)
					qemu_irq_raise(s->ndma_gpio[1]); // RDFIFO has 4 words available
				if((128 - ctr9_fifo_len(&s->wr_fifo)) >= 0x10)
					qemu_irq_raise(s->ndma_gpio[0]); // WRFIFO has 4 words available
				
				s->block_count -= 1;
				if(s->block_count == 0)
				{
					// Done processing everything
					s->start = false;
					ctr9_fifo_reset(&s->wr_fifo);
					
					if(s->keysel < 4 || s->keysel == 0x11)
						gcry_cipher_close(s->hd);
					
					if(s->irq_enable)
						qemu_irq_raise(s->irq);
				}
			}
		}

		break;
	case AES_KEYSEL:
		s->keysel = value & 0x3F;
		break;
	case AES_KEYCNT:
		s->keycnt_key = value & 0x3F;
		s->scrambler_type = (value >> 6) & 1;
		s->keyfifo_en = (value >> 7) & 1;
		break;
	case AES_CTR + 0x0:
	case AES_CTR + 0x4:
	case AES_CTR + 0x8:
	case AES_CTR + 0xC:
		if(size == 4)
			*(uint32_t*)&s->ctr[offset - AES_CTR] = s->input_endian ? __builtin_bswap32(value) : value;
		break;
	case AES_KEYFIFO ... (AES_KEYFIFO + 3):
		ctr9_aes_keyfifo_write(s, AES_KEYN, value, size);
		break;
	case AES_KEYXFIFO ... (AES_KEYXFIFO + 3):
		ctr9_aes_keyfifo_write(s, AES_KEYX, value, size);
		break;
	case AES_KEYYFIFO ... (AES_KEYYFIFO + 3):
		ctr9_aes_keyfifo_write(s, AES_KEYY, value, size);
		break;
	default:
		break;
	}
	
	if(offset >= AES_TWLKEYS && offset < (AES_TWLKEYS + 0x10 * 3 * 4))
	{
		uint32_t keyslot = (offset - AES_TWLKEYS) / (0x10 * 3);
		uint32_t keytype = ((offset - AES_TWLKEYS) / 0x10) % 3;
		uint32_t keyoff = offset % 0x10;
		
		printf("ctr9_aes_write 0x%03X %X %08X\n", (uint32_t)offset, size, (uint32_t)value);
		printf("%02X %02X %02X\n", keyslot, keytype, keyoff);
		
		uint8_t* t = &s->keyslots[keyslot].keys[keytype][keyoff];

		if(size == 1)
			*t = value;
		else if(size == 2)
			*(uint16_t*)t = value;
		else if(size == 4)
			*(uint32_t*)t = s->input_endian ? __builtin_bswap32(value) : value;
		
		if(keytype == AES_KEYY && keyoff >= 0xC) // Last word/byte to keyy changed, update normal key
			ctr9_aes_keyfifo_scramble(s, keyslot);
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
	qdev_init_gpio_out(dev, s->ndma_gpio, 2);

	memory_region_init_io(&s->iomem, OBJECT(s), &ctr9_aes_ops, s, "ctr9-aes", 0x200);
	sysbus_init_mmio(sbd, &s->iomem);
	
	memset(s->keyslots, 0, sizeof(s->keyslots));
	
	ctr9_fifo_init(&s->wr_fifo, 128);
	ctr9_fifo_init(&s->rd_fifo, 128);
	
	s->output_endian = 1;
	s->input_endian = 1;
	s->output_order = 1;
	s->input_order = 1;
	
	s->mode = 0;
	s->irq_enable = 0;
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