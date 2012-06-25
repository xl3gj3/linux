/*
 * Pepper
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/reboot.h>

#include <mach/hardware.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <plat/irqs.h>
#include <plat/board.h>
#include <plat/common.h>
#include <plat/mmc.h>

#include "common.h"
#include "devices.h"
#include "hsmmc.h"

/* Convert GPIO signal to GPIO pin number */
#define GPIO_TO_PIN(bank, gpio) (32 * (bank) + (gpio))

static struct omap_board_config_kernel pepper_config[] __initdata = {
};

/* mmc0 */

static struct omap2_hsmmc_info pepper_mmc[] __initdata = {
	{
		.mmc            = 1,
		.caps           = MMC_CAP_4_BIT_DATA,
		.gpio_cd        = GPIO_TO_PIN(0, 6),
		.ocr_mask       = MMC_VDD_32_33 | MMC_VDD_33_34, /* 3V3 */
	},
	{}      /* Terminator */
};

/* emif */

void __iomem *pepper_emif_base;

void __iomem * __init pepper_get_mem_ctlr(void)
{

	pepper_emif_base = ioremap(AM33XX_EMIF0_BASE, SZ_32K);

	if (!pepper_emif_base)
		pr_warning("%s: Unable to map DDR2 controller",	__func__);

	return pepper_emif_base;
}

void __iomem *am33xx_get_ram_base(void)
{
	return pepper_emif_base;
}

/* board init */

static void __init pepper_init(void)
{
	omap2_hsmmc_init(pepper_mmc);
	pepper_get_mem_ctlr();
	omap_sdrc_init(NULL, NULL);
	omap_serial_init();
	omap_board_config = pepper_config;
	omap_board_config_size = ARRAY_SIZE(pepper_config);
}

static void __init pepper_map_io(void)
{
	omap2_set_globals_am33xx();
	omapam33xx_map_common_io();
}

MACHINE_START(PEPPER, "pepper")
	.atag_offset	= 0x100,
	.map_io		= pepper_map_io,
	.init_early	= am33xx_init_early,
	.init_irq	= ti81xx_init_irq,
	.handle_irq     = omap3_intc_handle_irq,
	.timer		= &omap3_am33xx_timer,
	.init_machine	= pepper_init,
MACHINE_END

