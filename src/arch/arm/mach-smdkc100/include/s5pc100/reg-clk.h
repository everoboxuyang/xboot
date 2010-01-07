#ifndef __S5PC100_REG_CLK_H__
#define __S5PC100_REG_CLK_H__

#include <configs.h>
#include <default.h>


#define S5PC100_APLL_LOCK				(0xE0100000 + 0x000)
#define S5PC100_MPLL_LOCK				(0xE0100000 + 0x004)
#define S5PC100_EPLL_LOCK				(0xE0100000 + 0x008)
#define S5PC100_HPLL_LOCK				(0xE0100000 + 0x00C)
#define S5PC100_APLL_CON				(0xE0100000 + 0x100)
#define S5PC100_MPLL_CON				(0xE0100000 + 0x104)
#define S5PC100_EPLL_CON				(0xE0100000 + 0x108)
#define S5PC100_HPLL_CON				(0xE0100000 + 0x10C)
#define S5PC100_CLK_SRC0				(0xE0100000 + 0x200)
#define S5PC100_CLK_SRC1				(0xE0100000 + 0x204)
#define S5PC100_CLK_SRC2				(0xE0100000 + 0x208)
#define S5PC100_CLK_SRC3				(0xE0100000 + 0x20C)
#define S5PC100_CLK_DIV0				(0xE0100000 + 0x300)
#define S5PC100_CLK_DIV1				(0xE0100000 + 0x304)
#define S5PC100_CLK_DIV2				(0xE0100000 + 0x308)
#define S5PC100_CLK_DIV3				(0xE0100000 + 0x30C)
#define S5PC100_CLK_DIV4				(0xE0100000 + 0x310)
#define S5PC100_CLK_OUT					(0xE0100000 + 0x400)
#define S5PC100_CLK_GATE_D0_0			(0xE0100000 + 0x500)
#define S5PC100_CLK_GATE_D0_1			(0xE0100000 + 0x504)
#define S5PC100_CLK_GATE_D0_2			(0xE0100000 + 0x508)
#define S5PC100_CLK_GATE_D1_0			(0xE0100000 + 0x520)
#define S5PC100_CLK_GATE_D1_1			(0xE0100000 + 0x524)
#define S5PC100_CLK_GATE_D1_2			(0xE0100000 + 0x528)
#define S5PC100_CLK_GATE_D1_3			(0xE0100000 + 0x52C)
#define S5PC100_CLK_GATE_D1_4			(0xE0100000 + 0x530)
#define S5PC100_CLK_GATE_D1_5			(0xE0100000 + 0x534)
#define S5PC100_CLK_GATE_D2_0			(0xE0100000 + 0x540)
#define S5PC100_CLK_GATE_SCLK_0			(0xE0100000 + 0x560)
#define S5PC100_CLK_GATE_SCLK_1			(0xE0100000 + 0x564)

/*
 * CLKDIV0
 */
#define S5PC100_CLKDIV0_SECSS_MASK		(0x7 << 16)
#define S5PC100_CLKDIV0_SECSS_SHIFT		(16)
#define S5PC100_CLKDIV0_PCLKD0_MASK		(0x7 << 12)
#define S5PC100_CLKDIV0_PCLKD0_SHIFT	(12)
#define S5PC100_CLKDIV0_D0BUS_MASK		(0x7 << 8)
#define S5PC100_CLKDIV0_D0BUS_SHIFT		(8)
#define S5PC100_CLKDIV0_ARM_MASK		(0x7 << 4)
#define S5PC100_CLKDIV0_ARM_SHIFT		(4)
#define S5PC100_CLKDIV0_APLL_MASK		(0x1 << 0)
#define S5PC100_CLKDIV0_APLL_SHIFT		(0)

/*
 * CLKDIV1
 */
#define S5PC100_CLKDIV1_CAM_MASK		(0x1f << 24)
#define S5PC100_CLKDIV1_CAM_SHIFT		(24)
#define S5PC100_CLKDIV1_ONENAND_MASK	(0x3 << 20)
#define S5PC100_CLKDIV1_ONENAND_SHIFT	(20)
#define S5PC100_CLKDIV1_PCLKD1_MASK		(0x7 << 16)
#define S5PC100_CLKDIV1_PCLKD1_SHIFT	(16)
#define S5PC100_CLKDIV1_D1BUS_MASK		(0x7 << 12)
#define S5PC100_CLKDIV1_D1BUS_SHIFT		(12)
#define S5PC100_CLKDIV1_MPLL2_MASK		(0x1 << 8)
#define S5PC100_CLKDIV1_MPLL2_SHIFT		(8)
#define S5PC100_CLKDIV1_MPLL_MASK		(0x3 << 4)
#define S5PC100_CLKDIV1_MPLL_SHIFT		(4)
#define S5PC100_CLKDIV1_APLL2_MASK		(0x7 << 0)
#define S5PC100_CLKDIV1_APLL2_SHIFT		(0)

/*
 * CLKDIV2
 */
#define S5PC100_CLKDIV2_UHOST_MASK		(0xf << 20)
#define S5PC100_CLKDIV2_UHOST_SHIFT		(20)
#define S5PC100_CLKDIV2_IRDA_MASK		(0xf << 16)
#define S5PC100_CLKDIV2_IRDA_SHIFT		(16)
#define S5PC100_CLKDIV2_SPI2_MASK		(0xf << 12)
#define S5PC100_CLKDIV2_SPI2_SHIFT		(12)
#define S5PC100_CLKDIV2_SPI1_MASK		(0xf << 8)
#define S5PC100_CLKDIV2_SPI1_SHIFT		(8)
#define S5PC100_CLKDIV2_SPI0_MASK		(0xf << 4)
#define S5PC100_CLKDIV2_SPI0_SHIFT		(4)
#define S5PC100_CLKDIV2_UART_MASK		(0xf << 0)
#define S5PC100_CLKDIV2_UART_SHIFT		(0)

/*
 * CLKDIV3
 */
#define S5PC100_CLKDIV3_HDMI_MASK		(0xf << 28)
#define S5PC100_CLKDIV3_HDMI_SHIFT		(28)
#define S5PC100_CLKDIV3_FIMC2_MASK		(0xf << 24)
#define S5PC100_CLKDIV3_FIMC2_SHIFT		(24)
#define S5PC100_CLKDIV3_FIMC1_MASK		(0xf << 20)
#define S5PC100_CLKDIV3_FIMC1_SHIFT		(20)
#define S5PC100_CLKDIV3_FIMC0_MASK		(0xf << 16)
#define S5PC100_CLKDIV3_FIMC0_SHIFT		(16)
#define S5PC100_CLKDIV3_LCD_MASK		(0xf << 12)
#define S5PC100_CLKDIV3_LCD_SHIFT		(12)
#define S5PC100_CLKDIV3_MMC2_MASK		(0xf << 8)
#define S5PC100_CLKDIV3_MMC2_SHIFT		(8)
#define S5PC100_CLKDIV3_MMC1_MASK		(0xf << 4)
#define S5PC100_CLKDIV3_MMC1_SHIFT		(4)
#define S5PC100_CLKDIV3_MMC0_MASK		(0xf << 0)
#define S5PC100_CLKDIV3_MMC0_SHIFT		(0)

/*
 * CLKDIV4
 */
#define S5PC100_CLKDIV4_AUDIO2_MASK		(0xf << 20)
#define S5PC100_CLKDIV4_AUDIO2_SHIFT	(20)
#define S5PC100_CLKDIV4_AUDIO1_MASK		(0xf << 16)
#define S5PC100_CLKDIV4_AUDIO1_SHIFT	(16)
#define S5PC100_CLKDIV4_AUDIO0_MASK		(0xf << 12)
#define S5PC100_CLKDIV4_AUDIO0_SHIFT	(12)
#define S5PC100_CLKDIV4_I2SD2_MASK		(0xf << 8)
#define S5PC100_CLKDIV4_I2SD2_SHIFT		(8)
#define S5PC100_CLKDIV4_HCLKD2_MASK		(0x7 << 4)
#define S5PC100_CLKDIV4_HCLKD2_SHIFT	(4)
#define S5PC100_CLKDIV4_PWI_MASK		(0x7 << 0)
#define S5PC100_CLKDIV4_PWI_SHIFT		(0)


#endif /* __S5PC100_REG_CLK_H__ */
