#ifndef __S3C2410_REG_CLK_H__
#define __S3C2410_REG_CLK_H__

#include <configs.h>
#include <default.h>


#define S3C2410_LOCKTIME			(0x4c000000)
#define S3C2410_MPLLCON	  			(0x4c000004)
#define S3C2410_UPLLCON	  			(0x4c000008)
#define S3C2410_CLKCON	  			(0x4c00000c)
#define S3C2410_CLKSLOW	  			(0x4c000010)
#define S3C2410_CLKDIVN	  			(0x4c000014)


#define S3C2410_PLLCON_MDIVSHIFT     (12)
#define S3C2410_PLLCON_PDIVSHIFT     (4)
#define S3C2410_PLLCON_SDIVSHIFT     (0)
#define S3C2410_PLLCON_MDIVMASK	     ((1<<(1+(19-12)))-1)
#define S3C2410_PLLCON_PDIVMASK	     ((1<<5)-1)
#define S3C2410_PLLCON_SDIVMASK	     (3)

#define S3C2410_CLKDIVN_PDIVN	     (1<<0)
#define S3C2410_CLKDIVN_HDIVN	     (1<<1)


#endif /* __S3C2410_REG_CLK_H__ */
