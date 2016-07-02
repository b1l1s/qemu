Read the documentation in qemu-doc.html or on http://wiki.qemu-project.org

- QEMU team

qemu with CTR support
==============
3DS's ARM9 processor emulation with qemu.
Implemented devices :
 - sdmmc (not tested with actual firm, works with Normmatt's sdmmc.c)
 - lcd
 - aes (passthrough, data will not be encrypted or decrypted, except for TWL keyslots and slot 0x11 for testing purposes)
 - sha
 - rsa
 - ndma (immediate mode and startup mode for aes)
 - timers
 - interrupt controller

Howto
==============
Build:
```
./configure --disable-user --disable-gnutls --target-list=arm-softmmu
make
```
to compile with just arm support. SDL library required for LCD support, gcrypt required for crypto support.

To run a payload:
```
arm-softmmu/qemu-system-arm -kernel "path/to/payload.elf" -M ctr9
```

Debugging:
```
arm-softmmu/qemu-system-arm -S -gdb tcp:127.0.0.1:1234,ipv4 -kernel "path/to/payload.elf" -M ctr9
```

Optional support files:
 - 3ds-data/sd.bin - will be used as the image for the sdcard
 - 3ds-data/nand.bin - will be used as the image for the nand, CTRNAND partition of the image has to be manually decrypted beforehand if required
 - 3ds-data/sdmmc_info.bin - contains the csd and cid for the nand and sdcard

   ```
   struct {
      uint8_t nand_csd[16];
	  uint8_t nand_cid[16];
	  uint8_t sd_csd[16];
	  uint8_t sd_cid[16];
   };
   ```
 - 3ds-data/qemu_ctr_bootrom9.bin - file loaded to bootrom (for the interrupt redirection)
 - 3ds-data/itcm.bin - file loaded to itcm (for payloads that uses the data inside)

Key mapping:

| 3DS | PC |
|-----|----|
| A   | M  |
| B   | N  |
| X   | J  |
| Y   | H  |
| RT  | U  |
| LT  | Y  |

Dpad <-> arrow keys on the PC

Credits
==============
 - qemu maintainers and devs, obviously
 - 3dbrew for most of the info
 - Normmatt for sdmmc