#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "ctr9_common.h"

#ifdef CONFIG_GNUTLS_HASH
#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>

static const uint32_t sha_mode_map[] = {GNUTLS_DIG_SHA256, GNUTLS_DIG_SHA224, GNUTLS_DIG_SHA1};
#else
typedef void* gnutls_hash_hd_t;
int gnutls_hash_init(gnutls_hash_hd_t* dig, uint32_t alg) {return 0;}
int gnutls_hash(gnutls_hash_hd_t handle, const void *text, size_t textlen) {return 0;}
void gnutls_hash_deinit(gnutls_hash_hd_t handle, void *digest) {}

static const uint32_t sha_mode_map[] = {0, 0, 0};
#endif

#define TYPE_CTR9_SHA "ctr9-sha"
#define CTR9_SHA(obj) \
    OBJECT_CHECK(ctr9_sha_state, (obj), TYPE_CTR9_SHA)



typedef struct ctr9_sha_state {
	SysBusDevice parent_obj;

	MemoryRegion iomem;
	
	uint8_t start;
	uint8_t final;
	uint8_t output_endian;
	uint8_t mode;
	
	uint32_t block_count;
	
	uint8_t hash[0x20];
	
	ctr9_iofifo in_fifo;
	gnutls_hash_hd_t dig;
} ctr9_sha_state;

static uint64_t ctr9_sha_read(void* opaque, hwaddr offset, unsigned size)
{
	ctr9_sha_state* s = (ctr9_sha_state*)opaque;
	
	uint64_t res = 0;
	switch(offset)
	{
	case 0:
		res = s->start | (s->final << 1) | (s->output_endian << 3) | (s->mode << 4);
		break;
	case 4:
		res = s->block_count;
		break;
	default:
		break;
	}
	
	if(offset >= 0x40 && offset < 0x60)
	{
		if(size == 1)
			res = s->hash[offset - 0x40];
		if(size == 2)
			res = *(uint16_t*)&s->hash[offset - 0x40];
		if(size == 4)
			res = *(uint32_t*)&s->hash[offset - 0x40];
	}
	
	printf("ctr9_sha_read  0x%03X %X %08X\n", (uint32_t)offset, size, res);
	
	return res;
}

static void ctr9_sha_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
	ctr9_sha_state* s = (ctr9_sha_state*)opaque;
	
	printf("ctr9_sha_write 0x%03X %X %08X\n", (uint32_t)offset, size, (uint32_t)value);
	
	switch(offset)
	{
	case 0x00: // REG_SHA_CNT
		s->start = value & 1;
		s->final = (value >> 1) & 1;
		s->output_endian = (value >> 3) & 1;
		s->mode = (value >> 4) & 3;
		
		if(s->start)
		{
			ctr9_fifo_reset(&s->in_fifo);

			gnutls_hash_init(&s->dig, sha_mode_map[s->mode]);
			s->start = 0;
			s->block_count = 0;
		}

		if(s->final)
		{
			uint32_t fifo_len = ctr9_fifo_len(&s->in_fifo);
			if(fifo_len)
			{
				s->block_count += fifo_len;
				// We have some leftover data
				gnutls_hash(s->dig, s->in_fifo.buffer, fifo_len);
				ctr9_fifo_reset(&s->in_fifo);
			}

			printf("final round\n");
			gnutls_hash_deinit(s->dig, s->hash);
			s->final = 0;
		}
		break;
	case 0x80 ... 0x83: // REG_SHA_INFIFO
		ctr9_fifo_push(&s->in_fifo, value, size);
		if(ctr9_fifo_len(&s->in_fifo) == 128)
		{
			s->block_count += 128;

			gnutls_hash(s->dig, s->in_fifo.buffer, 128);
			ctr9_fifo_reset(&s->in_fifo);
		}
		break;
	default:
		break;
	}
}

static const MemoryRegionOps ctr9_sha_ops =
{
	.read = ctr9_sha_read,
	.write = ctr9_sha_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static int ctr9_sha_init(SysBusDevice *sbd)
{
	DeviceState *dev = DEVICE(sbd);
	ctr9_sha_state *s = CTR9_SHA(dev);

	memory_region_init_io(&s->iomem, OBJECT(s), &ctr9_sha_ops, s, "ctr9-sha", 0x100);
	sysbus_init_mmio(sbd, &s->iomem);
	
	s->start = 0;
	s->final = 0;
	s->output_endian = 1;
	s->mode = 0;
	
	ctr9_fifo_reset(&s->in_fifo);

	return 0;
}

static const VMStateDescription ctr9_sha_vmsd = {
	.name = "ctr9-sha",
	.version_id = 1,
	.minimum_version_id = 1,
	.fields = (VMStateField[]) {
		VMSTATE_END_OF_LIST()
	}
};

static void ctr9_sha_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

	k->init = ctr9_sha_init;
	dc->vmsd = &ctr9_sha_vmsd;
}

static const TypeInfo ctr9_sha_info = {
	.name          = TYPE_CTR9_SHA,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(ctr9_sha_state),
	.class_init    = ctr9_sha_class_init,
};

static void ctr9_sha_register_types(void)
{
	type_register_static(&ctr9_sha_info);
}

type_init(ctr9_sha_register_types)