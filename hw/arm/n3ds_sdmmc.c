#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "sysemu/block-backend.h"
#include "hw/sd.h"
#include "hw/boards.h"

#define TYPE_N3DS_SDMMC "n3ds-sdmmc"

#define EMMC_CMD		0x00
#define EMMC_PORTSEL	0x02
#define EMMC_CMDARG0	0x04
#define EMMC_CMDARG1	0x06
#define EMMC_STOP		0x08
#define EMMC_BLKCOUNT	0x0A
#define EMMC_RESP0		0x0C
#define EMMC_RESP1		0x0E
#define EMMC_RESP2		0x10
#define EMMC_RESP3		0x12
#define EMMC_RESP4		0x14
#define EMMC_RESP5		0x16
#define EMMC_RESP6		0x18
#define EMMC_RESP7		0x1A
#define EMMC_STATUS0	0x1C
#define EMMC_STATUS1	0x1E
#define EMMC_CLKCTL		0x24
#define EMMC_BLKLEN		0x26
#define EMMC_OPT		0x28
#define EMMC_FIFO		0x30
#define EMMC_RESET		0xE0

#define TMIO_STAT0_CMDRESPEND    0x0001
#define TMIO_STAT0_DATAEND       0x0004
#define TMIO_STAT0_CARD_REMOVE   0x0008
#define TMIO_STAT0_CARD_INSERT   0x0010
#define TMIO_STAT0_SIGSTATE      0x0020
#define TMIO_STAT0_WRPROTECT     0x0080
#define TMIO_STAT0_CARD_REMOVE_A 0x0100
#define TMIO_STAT0_CARD_INSERT_A 0x0200
#define TMIO_STAT0_SIGSTATE_A    0x0400
#define TMIO_STAT1_CMD_IDX_ERR   0x0001
#define TMIO_STAT1_CRCFAIL       0x0002
#define TMIO_STAT1_STOPBIT_ERR   0x0004
#define TMIO_STAT1_DATATIMEOUT   0x0008
#define TMIO_STAT1_RXOVERFLOW    0x0010
#define TMIO_STAT1_TXUNDERRUN    0x0020
#define TMIO_STAT1_CMDTIMEOUT    0x0040
#define TMIO_STAT1_RXRDY         0x0100
#define TMIO_STAT1_TXRQ          0x0200
#define TMIO_STAT1_ILL_FUNC      0x2000
#define TMIO_STAT1_CMD_BUSY      0x4000
#define TMIO_STAT1_ILL_ACCESS    0x8000

#define EMMC_STATE_READY 0
#define EMMC_STATE_READ 1
#define EMMC_STATE_WRITE 2

typedef struct n3ds_sdmmc_device
{
	uint32_t cid[4];
	uint32_t csd[4];
	
	uint32_t state;
	
	uint32_t block_len;
	uint32_t io_block_count;
	
	uint16_t status[2];
	
	uint32_t io_ptr;
	
	uint8_t buffer[0x1000];
	uint32_t ptr;
	
	FILE* file;
} n3ds_sdmmc_device;

typedef struct n3ds_sdmmc_state
{
	SysBusDevice parent_obj;
	MemoryRegion iomem;
	
	SDState *card;
	int enabled;
	
	n3ds_sdmmc_device cards[2];
	
	int selected;
	
	uint16_t cmdarg0;
	uint16_t cmdarg1;
	
	uint32_t ret[4];
} n3ds_sdmmc_state;

static void n3ds_sdmmc_fileread(n3ds_sdmmc_state* s)
{
	n3ds_sdmmc_device* card = &s->cards[s->selected];
	
	if(card->io_block_count >= 1)
	{
		if(card->file)
		{
			fseek(card->file, card->io_ptr, SEEK_SET);
			fread(card->buffer, card->block_len, 1, card->file);
		}
		
		printf("n3ds_sdmmc_fileread : %08X %08X\n", card->io_ptr, card->io_block_count);
		
		card->ptr = 0;
		
		card->io_ptr += card->block_len;
		card->io_block_count--;
		
		card->status[1] |= TMIO_STAT1_RXRDY;
	}
	else
	{
		card->status[0] = TMIO_STAT0_CMDRESPEND | TMIO_STAT0_DATAEND;
		card->state = EMMC_STATE_READY;
	}
}

static void n3ds_sdmmc_filewrite(n3ds_sdmmc_state* s)
{
	n3ds_sdmmc_device* card = &s->cards[s->selected];
	
	if(card->io_block_count >= 1)
	{
		if(card->file)
		{
			fseek(card->file, card->io_ptr, SEEK_SET);
			fwrite(card->buffer, card->block_len, 1, card->file);
		}
		
		printf("n3ds_sdmmc_filewrite : %08X %08X\n", card->io_ptr, card->io_block_count);
		
		card->ptr = 0;
		
		card->io_ptr += card->block_len;
		card->io_block_count--;
		
		if(card->io_block_count > 0)
			card->status[1] |= TMIO_STAT1_TXRQ;
		else
		{
			card->status[0] = TMIO_STAT0_CMDRESPEND | TMIO_STAT0_DATAEND;
			card->status[1] = 0;
			
			card->state = EMMC_STATE_READY;
		}
	}
	else
	{
		card->status[0] = TMIO_STAT0_CMDRESPEND | TMIO_STAT0_DATAEND;
		card->status[1] = 0;
		
		card->state = EMMC_STATE_READY;
	}
}

static uint64_t n3ds_sdmmc_read(void* opaque, hwaddr offset, unsigned size)
{
	//printf("n3ds_sdmmc_read %x\n", offset);
	n3ds_sdmmc_state* s = (n3ds_sdmmc_state*)opaque;
	n3ds_sdmmc_device* card = &s->cards[s->selected];
	switch(offset)
	{
	case EMMC_PORTSEL:
		return s->selected;
	case EMMC_STATUS0:
		//return 0x07A1; TMIO_STAT0_SIGSTATE | TMIO_STAT0_CARD_INSERT | TMIO_STAT0_DATAEND | TMIO_STAT0_CMDRESPEND
		return card->status[0];
	case EMMC_STATUS1:
		//return 0x2080;
		return card->status[1];
	case EMMC_CLKCTL:
		return 0x0300;
	case EMMC_OPT:
		return 0x40EB;
	case EMMC_RESET:
		return 0x0007;
	case 0x0D8:
		return 0x1012;
	case 0x100:
		return 0x0002;
	case EMMC_RESP0:
		return s->ret[0] & 0xFFFF;
	case EMMC_RESP1:
		return s->ret[0] >> 16;
	case EMMC_RESP2:
		return s->ret[1] & 0xFFFF;
	case EMMC_RESP3:
		return s->ret[1] >> 16;
	case EMMC_RESP4:
		return s->ret[2] & 0xFFFF;
	case EMMC_RESP5:
		return s->ret[2] >> 16;
	case EMMC_RESP6:
		return s->ret[3] & 0xFFFF;
	case EMMC_RESP7:
		return s->ret[3] >> 16;
	case EMMC_FIFO:
		if(card->state == EMMC_STATE_READ && card->ptr < card->block_len)
		{
			uint16_t res = card->buffer[card->ptr] | card->buffer[card->ptr + 1] << 8;
			card->ptr += 2;
			if(card->ptr == card->block_len)
			{
				// Refill buffer
				n3ds_sdmmc_fileread(s);
			}
			return res;
		}
		return 0;
	default:
		return 0;
	}
	
	return 0;
}


static void n3ds_sdmmc_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
	//printf("n3ds_sdmmc_write 0x%03llX 0x%08llX 0x%X\n", offset, value, size);
	n3ds_sdmmc_state* s = (n3ds_sdmmc_state*)opaque;
	n3ds_sdmmc_device* card = &s->cards[s->selected];

	switch(offset)
	{
	case EMMC_CMD:
		{
			uint8_t cmd = (value & 0xFF);
			switch(cmd)
			{
			case 0x00: // GO_IDLE_STATE
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x01: // SEND_OP_COND
				s->ret[0] = 0x80000000;
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x02: // ALL_SEND_CID
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x03: // SEND_RELATIVE_ADDR
				s->ret[0] = s->selected ? 1 : 0x48;
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x06: // SWITCH
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x07: // SELECT_CARD
				// 0 to deselect, use result from SEND_RELATIVE_ADDR to select(always 1 for NAND?)
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x08: // SEND_EXT_CSD
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x09: // SEND_CSD
				memcpy(s->ret, card->csd, 0x10);
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x0A: // SEND_CID
				memcpy(s->ret, card->cid, 0x10);
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x0D: // SEND_STATUS
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x10: // SET_BLOCKLEN
				card->block_len = s->cmdarg0;
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x12: // READ_MULTIPLE_BLOCK
				card->io_ptr = s->cmdarg0 | (s->cmdarg1 << 16);
				card->state = EMMC_STATE_READ;
				n3ds_sdmmc_fileread(s);
				break;
			case 0x19: // WRITE_MULTIPLE_BLOCK
				card->ptr = 0;
				card->io_ptr = s->cmdarg0 | (s->cmdarg1 << 16);
				card->state = EMMC_STATE_WRITE;
				card->status[1] = TMIO_STAT1_TXRQ;
				break;
			case 55:  // APP_CMD
				card->status[0] = TMIO_STAT0_CMDRESPEND;
			case 0x46:  // ?? ACMD
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x69:  // ?? ACMD
				s->ret[0] = 0x80000000;
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			}
		}
		break;
	case EMMC_CMDARG0:
		s->cmdarg0 = value;
		break;
	case EMMC_CMDARG1:
		s->cmdarg1 = value;
		break;
	case EMMC_STOP:
		card->state = EMMC_STATE_READY;
		break;
	case EMMC_BLKCOUNT:
		card->io_block_count = value;
		break;
	case EMMC_PORTSEL:
		if((value & 0x03) == 0)
			s->selected = 0;
		else
			s->selected = 1;
		printf("Selected : %s\n", s->selected ? "NAND" : "SD");
		break;
	case EMMC_STATUS0:
		card->status[0] = value;
		break;
	case EMMC_STATUS1:
		card->status[1] = value;
		break;
	case EMMC_CLKCTL:
		break;
	case EMMC_OPT:
		break;
	case EMMC_FIFO:
		if(card->state == EMMC_STATE_WRITE && card->ptr < card->block_len)
		{
			card->buffer[card->ptr] = value & 0xFF;
			card->buffer[card->ptr + 1] = value >> 8;
			card->ptr += 2;
			if(card->ptr == card->block_len)
			{
				// Flush buffer
				n3ds_sdmmc_filewrite(s);
			}
		}
		break;
	case EMMC_RESET:
		break;
	case 0x0D8:
		break;
	case 0x100:
		break;
	default:
		break;
	}
}

static const MemoryRegionOps n3ds_sdmmc_ops =
{
	.read = n3ds_sdmmc_read,
	.write = n3ds_sdmmc_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static int n3ds_sdmmc_init(SysBusDevice* sbd)
{
	DeviceState* dev = DEVICE(sbd);
	n3ds_sdmmc_state* s = OBJECT_CHECK(n3ds_sdmmc_state, dev, TYPE_N3DS_SDMMC);
	
	/*
	TODO: use the sd backend instead of emulating manually
	DriveInfo* dinfo = drive_get_next(IF_SD);
	BlockBackend* blk = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
	s->card = sd_init(blk, false);
	if(s->card == NULL)
	{
		printf("n3ds_sdmmc_init sd_init failed\n");
		return -1;
	}
	
	s->enabled = blk && blk_is_inserted(blk);
	*/
	
	// Reset response
	int i;
	for(i = 0; i < 4; ++i) s->ret[i] = 0;
	
	// Setup CID, CSD
	FILE* sdmmc_info = fopen("3ds-data/sdmmc_info.bin", "rb");
	if(sdmmc_info)
	{
		fread(s->cards[1].csd, 0x10, 1, sdmmc_info);
		fread(s->cards[1].cid, 0x10, 1, sdmmc_info);
		fread(s->cards[0].csd, 0x10, 1, sdmmc_info);
		fread(s->cards[0].cid, 0x10, 1, sdmmc_info);
		fclose(sdmmc_info);
	}
	else
	{
		printf("Error opening 3ds-data/sdmmc_info.bin\n");
	}

	s->cards[0].status[0] = 0;
	s->cards[0].status[1] = 0;
	
	s->cards[1].status[0] = 0;
	s->cards[1].status[1] = 0;
	
	s->selected = 0;
	
	s->cards[0].file = fopen("3ds-data/sd.bin", "r+b");
	if(!s->cards[0].file)
	{
		printf("Error opening 3ds-data/sd.bin\n");
	}
	s->cards[1].file = fopen("3ds-data/nand.bin", "r+b");
	if(!s->cards[0].file)
	{
		printf("Error opening 3ds-data/nand.bin\n");
	}
	
	memory_region_init_io(&s->iomem, OBJECT(s), &n3ds_sdmmc_ops, s, "n3ds-sdmmc", 0x200);
	sysbus_init_mmio(sbd, &s->iomem);
	return 0;
}

static void n3ds_sdmmc_class_init(ObjectClass *klass, void *data)
{
	SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
	k->init = n3ds_sdmmc_init;
	
	//DeviceClass *dc = DEVICE_CLASS(klass);
	//dc->vmsd = &vmstate_vpb_sic;
}

static const TypeInfo n3ds_sdmmc_info = {
	.name          = TYPE_N3DS_SDMMC,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(n3ds_sdmmc_state),
	.class_init    = n3ds_sdmmc_class_init,
};

static void n3ds_register_types(void)
{
	type_register_static(&n3ds_sdmmc_info);
}

type_init(n3ds_register_types)