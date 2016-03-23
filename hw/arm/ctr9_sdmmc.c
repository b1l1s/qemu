#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "sysemu/block-backend.h"
#include "hw/sd.h"
#include "hw/boards.h"

#define TYPE_CTR9_SDMMC "ctr9-sdmmc"

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
#define EMMC_IRQMASK0	0x20
#define EMMC_IRQMASK1	0x22
#define EMMC_CLKCTL		0x24
#define EMMC_BLKLEN		0x26
#define EMMC_OPT		0x28
#define EMMC_FIFO		0x30
#define EMMC_RESET		0xE0
#define EMMC_SDCTL_RESERVED5		0xF8

#define EMMC_DATACTL32			0x100
#define EMMC_SDBLKLEN32			0x104
#define EMMC_SDBLKCOUNT32		0x108
#define EMMC_SDFIFO32			0x10C

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

#define TMIO_STATUS0_NORMAL      (TMIO_STAT0_SIGSTATE | TMIO_STAT0_WRPROTECT | TMIO_STAT0_SIGSTATE_A)
#define TMIO_STATUS1_NORMAL      (0x0080)

#define EMMC_STATE_IDLE				0
#define EMMC_STATE_READY			1
#define EMMC_STATE_IDENT			2
#define EMMC_STATE_STANDBY			3
#define EMMC_STATE_TRANSFER			4
#define EMMC_STATE_READ				5
#define EMMC_STATE_WRITE			6
#define EMMC_STATE_PROG				7
#define EMMC_STATE_DC				8

typedef struct ctr9_sdmmc_device
{
	uint32_t cid[4]; // CID, LSB first minus the CRC and padded with 00 at the MSB
	uint32_t csd[4]; // CSD, LSB first minus the CRC and padded with 00 at the MSB
	
	bool is_sd;
	bool is_sdhc;
	
	uint32_t state;
	
	uint32_t block_len;
	uint32_t io_block_count;
	
	uint16_t status[2];
	uint16_t irqmask[2];
	uint32_t ctl32;
	
	uint32_t io_ptr;
	
	uint8_t buffer[0x1000];
	uint32_t ptr;
	
	FILE* file;
} ctr9_sdmmc_device;

typedef struct ctr9_sdmmc_state
{
	SysBusDevice parent_obj;
	MemoryRegion iomem;
	
	qemu_irq irq;
	
	SDState *card;
	int enabled;
	
	ctr9_sdmmc_device cards[2];
	
	int selected;
	
	uint16_t prevcmd;
	uint16_t cmdarg0;
	uint16_t cmdarg1;
	
	uint32_t ret[4];
} ctr9_sdmmc_state;

static int trap = 0;

static void ctr9_sdmmc_fileread(ctr9_sdmmc_state* s)
{
	ctr9_sdmmc_device* card = &s->cards[s->selected];
	
	if(card->io_block_count >= 1)
	{
		trap = 1;
		if(card->file)
		{
			fseek(card->file, card->io_ptr, SEEK_SET);
			fread(card->buffer, card->block_len, 1, card->file);
		}
		
		printf("ctr9_sdmmc_fileread : %08X %08X\n", card->io_ptr, card->io_block_count);
		
		card->ptr = 0;
		
		card->io_ptr += card->block_len;
		card->io_block_count--;
		
		card->status[0] = TMIO_STAT0_CMDRESPEND;
		card->status[1] = TMIO_STAT1_CMD_BUSY | TMIO_STAT1_RXRDY;
		card->ctl32 |= 0x100; // TODO
	}
	else
	{
		card->status[0] = TMIO_STAT0_CMDRESPEND | TMIO_STAT0_DATAEND;
		card->status[1] = 0;
		card->ctl32 = 0; // TODO

		card->state = EMMC_STATE_TRANSFER;
	}
}

static void ctr9_sdmmc_filewrite(ctr9_sdmmc_state* s)
{
	ctr9_sdmmc_device* card = &s->cards[s->selected];
	
	if(card->io_block_count >= 1)
	{
		if(card->file)
		{
			fseek(card->file, card->io_ptr, SEEK_SET);
			fwrite(card->buffer, card->block_len, 1, card->file);
		}
		
		printf("ctr9_sdmmc_filewrite : %08X %08X\n", card->io_ptr, card->io_block_count);
		
		card->ptr = 0;
		
		card->io_ptr += card->block_len;
		card->io_block_count--;
		
		if(card->io_block_count > 0)
		{
			card->status[0] = TMIO_STAT0_CMDRESPEND;
			card->status[1] = TMIO_STAT1_CMD_BUSY | TMIO_STAT1_TXRQ;
			card->ctl32 |= 0x100; // TODO
		}
		else
		{
			card->status[0] = TMIO_STAT0_CMDRESPEND | TMIO_STAT0_DATAEND;
			card->status[1] = 0;
			card->ctl32 = 0; // TODO
			
			card->state = EMMC_STATE_TRANSFER;
		}
	}
	else
	{
		card->status[0] = TMIO_STAT0_CMDRESPEND | TMIO_STAT0_DATAEND;
		card->status[1] = 0;
		
		card->state = EMMC_STATE_READY;
	}
}

static uint64_t ctr9_sdmmc_read(void* opaque, hwaddr offset, unsigned size)
{
	ctr9_sdmmc_state* s = (ctr9_sdmmc_state*)opaque;
	ctr9_sdmmc_device* card = &s->cards[s->selected];
	
	uint32_t res = 0;
	switch(offset)
	{
	case EMMC_CMD:
		res = s->prevcmd;
		break;
	case EMMC_PORTSEL:
		res = s->selected;
		break;
	case EMMC_STATUS0:
		res = card->status[0] | TMIO_STATUS0_NORMAL;
		if(size == 4)
			res |= (card->status[1] | TMIO_STATUS1_NORMAL) << 16;
		break;
	case EMMC_STATUS1:
		res = card->status[1] | TMIO_STATUS1_NORMAL;
		break;
	case EMMC_IRQMASK0:
		res = card->irqmask[0];
		break;
	case EMMC_IRQMASK1:
		res = card->irqmask[1];
		break;
	case EMMC_CLKCTL:
		res = 0x0300;
		break;
	case EMMC_OPT:
		res = 0x40EB;
		break;
	case EMMC_RESET:
		res = 0x0007;
		break;
	case 0x0D8:
		res = 0x1012;
		break;
	case EMMC_RESP0:
		res = s->ret[0] & 0xFFFF;
		break;
	case EMMC_RESP1:
		res = s->ret[0] >> 16;
		break;
	case EMMC_RESP2:
		res = s->ret[1] & 0xFFFF;
		break;
	case EMMC_RESP3:
		res = s->ret[1] >> 16;
		break;
	case EMMC_RESP4:
		res = s->ret[2] & 0xFFFF;
		break;
	case EMMC_RESP5:
		res = s->ret[2] >> 16;
		break;
	case EMMC_RESP6:
		res = s->ret[3] & 0xFFFF;
		break;
	case EMMC_RESP7:
		res = s->ret[3] >> 16;
		break;
	case 0x38:
		res = 0xC007;
		break;
	case EMMC_SDCTL_RESERVED5:
		res = 6;
		break;
	case 0xFA:
		res = 7;
		break;
	case 0xFC:
	case 0xFE:
		res = 0xFF;
		break;
	case EMMC_SDFIFO32:
	case EMMC_FIFO:
		if(card->state == EMMC_STATE_READ && card->ptr < card->block_len)
		{
			if(size == 2)
				res = *(uint16_t*)&card->buffer[card->ptr];
			else if(size == 4)
				res = *(uint32_t*)&card->buffer[card->ptr];

			card->ptr += size;
			if(card->ptr == card->block_len)
			{
				// Refill buffer
				ctr9_sdmmc_fileread(s);
			}
		}
		break;
	case EMMC_DATACTL32:
		res = card->ctl32 | 2;
		//if(card->is_sd)
		//	res |= 2;
		//if(trap)
		//	cpu_single_step(qemu_get_cpu(0), SSTEP_ENABLE | SSTEP_NOIRQ | SSTEP_NOTIMER);
		break;
	default:
		break;
	}
	
	//printf("ctr9_sdmmc_read  0x%03X %X %08X\n", (uint32_t)offset, size, res);
	
	return res;
}

static void ctr9_sdmmc_resp_r1(ctr9_sdmmc_state* s, ctr9_sdmmc_device* card)
{
	s->ret[0] = (card->state << 1 | 1) << 8;
}

static void ctr9_sdmmc_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
	//printf("ctr9_sdmmc_write 0x%03X %X %08X\n", (uint32_t)offset, size, (uint32_t)value);
	ctr9_sdmmc_state* s = (ctr9_sdmmc_state*)opaque;
	ctr9_sdmmc_device* card = &s->cards[s->selected];

	switch(offset)
	{
	case EMMC_CMD:
		{
			s->prevcmd = value;
			uint8_t cmd = (value & 0xFF);
			card->status[0] = 0;
			printf("%s%d : ", (cmd & 0x40) ? "ACMD" : "CMD", cmd & 0x3F);
			printf("%08X\n", s->cmdarg0 | (s->cmdarg1 << 16));
			
			switch(cmd)
			{
			case 0x00: // GO_IDLE_STATE
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				card->state = EMMC_STATE_IDLE;
				break;
			case 0x01: // SEND_OP_COND
				s->ret[0] = 0x80FF8080;
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				
				card->state = EMMC_STATE_READY;
				break;
			case 0x02: // ALL_SEND_CID
				memcpy(s->ret, card->cid, 0x10);
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				card->state = EMMC_STATE_IDENT;
				break;
			case 0x03: // SEND_RELATIVE_ADDR
				s->ret[0] = s->selected ? 1 : 0x48;
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x06: // SWITCH, switch MMC to high speed mode
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				
				ctr9_sdmmc_resp_r1(s, card);
				//card->state = EMMC_STATE_PROG;
				break;
			case 0x07: // SELECT_CARD
				// 0 to deselect, use result from SEND_RELATIVE_ADDR to select(always 1 for NAND?)
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				
				ctr9_sdmmc_resp_r1(s, card);
				card->state = EMMC_STATE_TRANSFER;
				break;
			case 0x08: // SEND_EXT_CSD/SEND_IF_COND
				if(card->is_sd)
				{
					// SEND_IF_COND
					s->ret[0] = s->cmdarg0 | (s->cmdarg1 << 16);
					card->status[0] = TMIO_STAT0_CMDRESPEND; // Responding here indicates sd v2 compliance
				}
				else
				{
					// SEND_EXT_CSD
					if(card->state == EMMC_STATE_IDLE)
					{
						card->status[0] = TMIO_STAT0_CMDRESPEND;
						card->status[1] = TMIO_STAT1_CMDTIMEOUT;
					}
					else
					{
						card->ptr = 0;
						card->io_block_count = 0;
						card->block_len = 0x200;
						
						FILE* file = fopen("3ds-data/extcsd.bin", "rb");
						if(file)
						{
							fread(card->buffer, card->block_len, 1, file);
							fclose(file);
						}
						else
						{
							printf("Failed to open extcsd.bin\n");
						}

						card->status[0] = TMIO_STAT0_CMDRESPEND;
						card->status[1] = TMIO_STAT1_CMD_BUSY | TMIO_STAT1_RXRDY;
						card->ctl32 |= 0x100; // TODO
						
						ctr9_sdmmc_resp_r1(s, card);
						card->state = EMMC_STATE_READ;
					}
				}
				break;
			case 0x09: // SEND_CSD
				memcpy(s->ret, card->csd, 0x10);
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x0A: // SEND_CID
				memcpy(s->ret, card->cid, 0x10);
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x0C: // STOP_TRANSMISSION
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				card->state = EMMC_STATE_TRANSFER;
				card->ptr = 0;
				card->io_block_count = 0;
				ctr9_sdmmc_resp_r1(s, card);
				break;
			case 0x0D: // SEND_STATUS
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				
				ctr9_sdmmc_resp_r1(s, card);
				break;
			case 0x10: // SET_BLOCKLEN
				card->block_len = s->cmdarg0;
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				
				ctr9_sdmmc_resp_r1(s, card);
				break;
			case 0x12: // READ_MULTIPLE_BLOCK
				card->io_ptr = s->cmdarg0 | (s->cmdarg1 << 16);
				card->state = EMMC_STATE_READ;
				ctr9_sdmmc_fileread(s);
				ctr9_sdmmc_resp_r1(s, card);
				break;
			case 0x19: // WRITE_MULTIPLE_BLOCK
				card->ptr = 0;
				card->io_ptr = s->cmdarg0 | (s->cmdarg1 << 16);
				card->state = EMMC_STATE_WRITE;
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				card->status[1] = TMIO_STAT1_TXRQ;
				break;
			case 55:  // APP_CMD
				if(!card->is_sd)
				{
					card->status[1] = TMIO_STAT1_CMDTIMEOUT;
				}
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x46:  // ACMD6
				if(!card->is_sd)
				{
					card->status[1] = TMIO_STAT1_CMDTIMEOUT;
				}
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			case 0x69:  // ACMD41 SD_APP_OP_COND
				if(card->is_sd)
				{
					s->ret[0] = 0x80000000; // Busy bit(indicates power up sequence is finished)
					s->ret[0] |= s->cmdarg0 | (s->cmdarg1 << 16); // Voltage window, just use the argument
					s->ret[0] &= ~0x40000000; // Card cap support bit, indicates SDHC support, disable for now
					
					card->state = EMMC_STATE_READY;
				}
				else
				{
					card->status[1] = TMIO_STAT1_CMDTIMEOUT;
				}
				
				card->status[0] = TMIO_STAT0_CMDRESPEND;
				break;
			}
			
			if((card->status[0] & ~card->irqmask[0]) || (card->status[1] & ~card->irqmask[1]))
				qemu_irq_raise(s->irq);
		}
		break;
	case EMMC_CMDARG0:
		s->cmdarg0 = value;
		break;
	case EMMC_CMDARG1:
		s->cmdarg1 = value;
		break;
	case EMMC_STOP:
		if(value)
		{
			if(card->state == EMMC_STATE_READ || card->state == EMMC_STATE_WRITE)
				card->state = EMMC_STATE_TRANSFER;
			else
				card->state = EMMC_STATE_READY;
			card->status[1] = 0;
			ctr9_sdmmc_resp_r1(s, card);
		}
		break;
	case EMMC_BLKCOUNT:
	case EMMC_SDBLKCOUNT32:
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
		card->status[0] &= value;
		break;
	case EMMC_STATUS1:
		card->status[1] &= value;
		break;
	case EMMC_IRQMASK0:
		card->irqmask[0] = value;// | 0x31D
		break;
	case EMMC_IRQMASK1:
		card->irqmask[1] = value;
		break;
	case EMMC_CLKCTL:
		break;
	case EMMC_OPT:
		break;
	case EMMC_RESET:
		break;
	case 0x0D8:
		break;
	case EMMC_DATACTL32:
		card->ctl32 = value;
		break;
	case EMMC_SDBLKLEN32:
		card->block_len = value;
		break;
	case EMMC_SDFIFO32:
	case EMMC_FIFO:
		if(card->state == EMMC_STATE_WRITE && card->ptr < card->block_len)
		{
			if(size == 2 || size == 4)
			{
				if(size == 2)
					*(uint16_t*)&card->buffer[card->ptr] = value;
				else
					*(uint32_t*)&card->buffer[card->ptr] = value;

				card->ptr += size;
				if(card->ptr == card->block_len)
				{
					// Flush buffer
					ctr9_sdmmc_filewrite(s);
				}
			}
		}
		break;
	default:
		break;
	}
}

static const MemoryRegionOps ctr9_sdmmc_ops =
{
	.read = ctr9_sdmmc_read,
	.write = ctr9_sdmmc_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static int ctr9_sdmmc_init(SysBusDevice* sbd)
{
	DeviceState* dev = DEVICE(sbd);
	ctr9_sdmmc_state* s = OBJECT_CHECK(ctr9_sdmmc_state, dev, TYPE_CTR9_SDMMC);
	
	/*
	TODO: use the sd backend instead of emulating manually
	DriveInfo* dinfo = drive_get_next(IF_SD);
	BlockBackend* blk = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
	s->card = sd_init(blk, false);
	if(s->card == NULL)
	{
		printf("ctr9_sdmmc_init sd_init failed\n");
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

	sysbus_init_irq(sbd, &s->irq);
	
	s->cards[0].state = EMMC_STATE_IDLE;
	s->cards[1].state = EMMC_STATE_IDLE;

	s->cards[0].status[0] = 0;
	s->cards[0].status[1] = 0;
	
	s->cards[1].status[0] = 0;
	s->cards[1].status[1] = 0;
	
	s->cards[0].irqmask[0] = 0x31D;
	s->cards[0].irqmask[1] = 0x807F;
	
	s->cards[1].irqmask[0] = 0x31D;
	s->cards[1].irqmask[1] = 0x837F;
	
	s->cards[0].ctl32 = 0;
	s->cards[1].ctl32 = 0;
	
	s->selected = 0;
	
	s->cards[0].is_sd = 1;
	s->cards[0].is_sdhc = 0;
	
	s->cards[1].is_sd = 0;
	s->cards[1].is_sdhc = 0;
	
	FILE* file = fopen("3ds-data/sd.bin", "r+b");
	if(!file)
	{
		printf("Error opening 3ds-data/sd.bin\n");
	}
	s->cards[0].file = file;

	file = fopen("3ds-data/nand.bin", "r+b");
	if(!file)
	{
		printf("Error opening 3ds-data/nand.bin\n");
	}
	s->cards[1].file = file;
	
	memory_region_init_io(&s->iomem, OBJECT(s), &ctr9_sdmmc_ops, s, "ctr9-sdmmc", 0x200);
	sysbus_init_mmio(sbd, &s->iomem);
	return 0;
}

static void ctr9_sdmmc_class_init(ObjectClass *klass, void *data)
{
	SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
	k->init = ctr9_sdmmc_init;
	
	//DeviceClass *dc = DEVICE_CLASS(klass);
	//dc->vmsd = &vmstate_vpb_sic;
}

static const TypeInfo ctr9_sdmmc_info = {
	.name          = TYPE_CTR9_SDMMC,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(ctr9_sdmmc_state),
	.class_init    = ctr9_sdmmc_class_init,
};

static void ctr9_register_types(void)
{
	type_register_static(&ctr9_sdmmc_info);
}

type_init(ctr9_register_types)