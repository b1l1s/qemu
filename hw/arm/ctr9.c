#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "net/net.h"
#include "hw/i2c/i2c.h"
#include "hw/boards.h"
#include "exec/address-spaces.h"

#include "qemu/error-report.h"
#include <stdio.h>

#define IRQ_ID_DMAC_1_0			0
#define IRQ_ID_DMAC_1_1			1
#define IRQ_ID_DMAC_1_2			2
#define IRQ_ID_DMAC_1_3			3
#define IRQ_ID_DMAC_1_4			4
#define IRQ_ID_DMAC_1_5			5
#define IRQ_ID_DMAC_1_6			6
#define IRQ_ID_DMAC_1_7			7
#define IRQ_ID_TIMER_0			8
#define IRQ_ID_TIMER_1			9
#define IRQ_ID_TIMER_2			10
#define IRQ_ID_TIMER_3			11
#define IRQ_ID_PXI_SYNC			12
#define IRQ_ID_PXI_NOT_FULL		13
#define IRQ_ID_PXI_NOT_EMPTY	14
#define IRQ_ID_AES				15
#define IRQ_ID_SDIO_1			16
#define IRQ_ID_SDIO_1_ASYNC		17
#define IRQ_ID_SDIO_3			18
#define IRQ_ID_SDIO_3_ASYNC		19
#define IRQ_ID_DEBUG_RECV		20
#define IRQ_ID_DEBUG_SEND		21
#define IRQ_ID_RSA				22
#define IRQ_ID_CTR_CARD_1		23
#define IRQ_ID_CTR_CARD_2		24
#define IRQ_ID_CGC				25
#define IRQ_ID_CGC_DET			26
#define IRQ_ID_DS_CARD			27
#define IRQ_ID_DMAC_2			28
#define IRQ_ID_DMAC_2_ABORT		29

static struct arm_boot_info ctr9_binfo;

typedef struct ctr9_fake11_state
{
	SysBusDevice parent_obj;

	MemoryRegion iomem;
	uint32_t val;
} ctr9_fake11_state;

ctr9_fake11_state fake11_state;

static uint64_t ctr9_fake11_read(void* opaque, hwaddr offset, unsigned size)
{
	ctr9_fake11_state* s = (ctr9_fake11_state*)opaque;
	
	uint64_t res = 0;
	switch(offset)
	{
	default:
		res = s->val;
		break;
	}
	return res;
}

static void ctr9_fake11_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
	ctr9_fake11_state* s = (ctr9_fake11_state*)opaque;
	
	switch(offset)
	{
	default:
		if(value == 1)
			s->val = 3;
		printf("ctr9-fakewrite11 : %08X\n", s->val);
		break;
	}
}

static const MemoryRegionOps ctr9_fake11_ops =
{
	.read = ctr9_fake11_read,
	.write = ctr9_fake11_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static void ctr9_init(MachineState *machine)
{
	Error *err = NULL;

	if (!machine->cpu_model) {
		machine->cpu_model = "arm946n";
	}

	ObjectClass* cpu_oc = cpu_class_by_name(TYPE_ARM_CPU, machine->cpu_model);
	if (!cpu_oc) {
		fprintf(stderr, "Unable to find CPU definition\n");
		exit(1);
	}

	Object* cpuobj = object_new(object_class_get_name(cpu_oc));

	/* By default ARM1176 CPUs have EL3 enabled.  This board does not
	 * currently support EL3 so the CPU EL3 property is disabled before
	 * realization.
	 */
	if (object_property_find(cpuobj, "has_el3", NULL)) {
		object_property_set_bool(cpuobj, false, "has_el3", &err);
		if (err) {
			error_report_err(err);
			exit(1);
		}
	}

	object_property_set_bool(cpuobj, true, "realized", &err);
	if (err) {
		error_report_err(err);
		exit(1);
	}

	ARMCPU* cpu = ARM_CPU(cpuobj);

	MemoryRegion* sysmem = get_system_memory();
	MemoryRegion* fcram = g_new(MemoryRegion, 1);
	memory_region_allocate_system_memory(fcram, NULL, "fcram", machine->ram_size);
	memory_region_add_subregion(sysmem, 0x20000000, fcram);
	
	MemoryRegion* intram = g_new(MemoryRegion, 1);
	memory_region_init_ram(intram, NULL, "intram", 0x00100000, &error_abort);
	vmstate_register_ram_global(intram);
	memory_region_add_subregion(sysmem, 0x08000000, intram);
	
	MemoryRegion* vram = g_new(MemoryRegion, 1);
	memory_region_init_ram(vram, NULL, "vram", 0x00600000, &error_abort);
	vmstate_register_ram_global(vram);
	memory_region_add_subregion(sysmem, 0x18000000, vram);
	
	MemoryRegion* dsp = g_new(MemoryRegion, 1);
	memory_region_init_ram(dsp, NULL, "dsp", 0x00080000, &error_abort);
	vmstate_register_ram_global(dsp);
	memory_region_add_subregion(sysmem, 0x1FF00000, dsp);
	
	MemoryRegion* axiwram = g_new(MemoryRegion, 1);
	memory_region_init_ram(axiwram, NULL, "axiwram", 0x00080000, &error_abort);
	vmstate_register_ram_global(axiwram);
	memory_region_add_subregion(sysmem, 0x1FF80000, axiwram);
	
	MemoryRegion* dtcm = g_new(MemoryRegion, 1);
	memory_region_init_ram(dtcm, NULL, "dtcm", 0x4000, &error_abort);
	vmstate_register_ram_global(dtcm);
	memory_region_add_subregion(sysmem, 0x28008000, dtcm);
	
	MemoryRegion* itcm = g_new(MemoryRegion, 1);
	memory_region_init_ram(itcm, NULL, "itcm", 0x8000, &error_abort);
	vmstate_register_ram_global(itcm);
	memory_region_add_subregion(sysmem, 0x00000000, itcm);
	
	MemoryRegion* itcm_alias = g_new(MemoryRegion, 1);
	memory_region_init_alias(itcm_alias, NULL, "itcm.alias", itcm, 0, 0x8000);
	memory_region_add_subregion(sysmem, 0x01FF8000, itcm_alias);
	
	FILE* fitcm = fopen("3ds-data/itcm.bin", "rb");
	if(fitcm)
	{
		fseek(fitcm, 0, SEEK_END);
		size_t size = ftell(fitcm);
		fseek(fitcm, 0, SEEK_SET);
		
		uint8_t* buffer = (uint8_t*) malloc(size);
		fread(buffer, size, 1, fitcm);
		
		cpu_physical_memory_write(0x01FF8000, buffer, size);
		
		free(buffer);
		fclose(fitcm);
	}
	
	MemoryRegion* bootrom = g_new(MemoryRegion, 1);
	memory_region_init_ram(bootrom, NULL, "bootrom", 0x00010000, &error_abort);
	vmstate_register_ram_global(bootrom);
	memory_region_add_subregion(sysmem, 0xFFFF0000, bootrom);
	
	FILE* bootrom9 = fopen("3ds-data/qemu_ctr_bootrom9.bin", "rb");
	if(bootrom9)
	{
		fseek(bootrom9, 0, SEEK_END);
		size_t size = ftell(bootrom9);
		fseek(bootrom9, 0, SEEK_SET);
		
		uint8_t* buffer = (uint8_t*) malloc(size);
		fread(buffer, size, 1, bootrom9);
		
		cpu_physical_memory_write(0xFFFF0000, buffer, size);
		
		free(buffer);
		fclose(bootrom9);
	}
	
	int i;
	qemu_irq pic[32];
	DeviceState* dev = sysbus_create_simple("ctr9-pic", 0x10001000, qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ));
	for (i = 0; i < 32; i++)
	{
		pic[i] = qdev_get_gpio_in(dev, i);
	}
	DeviceState* ndma = sysbus_create_varargs("ctr9-ndma", 0x10002000,
							pic[IRQ_ID_DMAC_1_0], pic[IRQ_ID_DMAC_1_1],
							pic[IRQ_ID_DMAC_1_2], pic[IRQ_ID_DMAC_1_3],
							pic[IRQ_ID_DMAC_1_4], pic[IRQ_ID_DMAC_1_5],
							pic[IRQ_ID_DMAC_1_6], pic[IRQ_ID_DMAC_1_7],
							NULL);
	
	sysbus_create_varargs("ctr9-pit", 0x10003000,
							pic[IRQ_ID_TIMER_0], pic[IRQ_ID_TIMER_1],
							pic[IRQ_ID_TIMER_2], pic[IRQ_ID_TIMER_3],
							NULL);

	sysbus_create_simple("ctr9-lcdfb", 0x10400000, NULL);
	sysbus_create_simple("ctr9-hid", 0x10146000, NULL);
	sysbus_create_simple("ctr9-sdmmc", 0x10006000, pic[IRQ_ID_SDIO_1]);
	sysbus_create_varargs("ctr9-pxi", 0x10008000, pic[IRQ_ID_PXI_SYNC],
							pic[IRQ_ID_PXI_NOT_FULL], pic[IRQ_ID_PXI_NOT_EMPTY],
							NULL);
	DeviceState* aes = sysbus_create_simple("ctr9-aes", 0x10009000, pic[IRQ_ID_AES]);
	sysbus_create_simple("ctr9-sha", 0x1000A000, NULL);
	sysbus_create_simple("ctr9-rsa", 0x1000B000, pic[IRQ_ID_RSA]);
	
	qdev_connect_gpio_out(aes, 0, qdev_get_gpio_in(ndma, 8));
	qdev_connect_gpio_out(aes, 1, qdev_get_gpio_in(ndma, 9));
	
	memory_region_init_io(&fake11_state.iomem, NULL, &ctr9_fake11_ops, &fake11_state, "fake11", 4);
	memory_region_add_subregion_overlap(sysmem, 0x1FFFFFF0, &fake11_state.iomem, 2);
	fake11_state.val = 1;

	ctr9_binfo.ram_size = machine->ram_size;
	ctr9_binfo.kernel_filename = machine->kernel_filename;
	ctr9_binfo.kernel_cmdline = machine->kernel_cmdline;
	ctr9_binfo.initrd_filename = machine->initrd_filename;
	ctr9_binfo.board_id = 0x2ff;
	arm_load_kernel(cpu, &ctr9_binfo);
}

static QEMUMachine ctr9_machine = {
	.name = "ctr9",
	.desc = "nintendo 3ds (ARM946)",
	.init = ctr9_init,
	.block_default_type = IF_SCSI,
};

static void ctr9_machine_init(void)
{
	qemu_register_machine(&ctr9_machine);
}

machine_init(ctr9_machine_init);
