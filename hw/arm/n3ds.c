#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "net/net.h"
#include "hw/i2c/i2c.h"
#include "hw/boards.h"
#include "exec/address-spaces.h"

#include "qemu/error-report.h"
#include <stdio.h>

static struct arm_boot_info n3ds_binfo;

typedef struct n3ds_fake11_state
{
	SysBusDevice parent_obj;

	MemoryRegion iomem;
	uint32_t val;
} n3ds_fake11_state;

n3ds_fake11_state fake11_state;

static uint64_t n3ds_fake11_read(void* opaque, hwaddr offset, unsigned size)
{
	n3ds_fake11_state* s = (n3ds_fake11_state*)opaque;
	
	uint64_t res = 0;
	switch(offset)
	{
	default:
		res = s->val;
		break;
	}
	return res;
}

static void n3ds_fake11_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
	n3ds_fake11_state* s = (n3ds_fake11_state*)opaque;
	
	switch(offset)
	{
	default:
		if(value == 1)
			s->val = 3;
		printf("n3ds-fakewrite11 : %08X\n", s->val);
		break;
	}
}

static const MemoryRegionOps n3ds_fake11_ops =
{
	.read = n3ds_fake11_read,
	.write = n3ds_fake11_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static void n3ds_init(MachineState *machine)
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
	
	sysbus_create_simple("n3ds-sdmmc", 0x10006000, NULL);
	sysbus_create_simple("n3ds-lcdfb", 0x10400000, NULL);
	sysbus_create_simple("n3ds-hid", 0x10146000, NULL);
	sysbus_create_simple("n3ds-aes", 0x10009000, NULL);
	
	//DeviceState *dev = qdev_create(NULL, "fake11");
	memory_region_init_io(&fake11_state.iomem, NULL, &n3ds_fake11_ops, &fake11_state, "fake11", 4);
	//sysbus_init_mmio(sbd, &fake11_state.iomem);
	memory_region_add_subregion_overlap(sysmem, 0x1FFFFFF0, &fake11_state.iomem, 2);
	fake11_state.val = 1;

	n3ds_binfo.ram_size = machine->ram_size;
	n3ds_binfo.kernel_filename = machine->kernel_filename;
	n3ds_binfo.kernel_cmdline = machine->kernel_cmdline;
	n3ds_binfo.initrd_filename = machine->initrd_filename;
	n3ds_binfo.board_id = 0x2ff;
	arm_load_kernel(cpu, &n3ds_binfo);
}

static QEMUMachine n3ds_machine = {
	.name = "n3ds",
	.desc = "nintendo 3ds (ARM946)",
	.init = n3ds_init,
	.block_default_type = IF_SCSI,
};

static void n3ds_machine_init(void)
{
	qemu_register_machine(&n3ds_machine);
}

machine_init(n3ds_machine_init);
