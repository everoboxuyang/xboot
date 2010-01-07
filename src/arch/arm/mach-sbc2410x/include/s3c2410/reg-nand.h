#ifndef __S3C2410_REG_NAND_H__
#define __S3C2410_REG_NAND_H__

#include <configs.h>
#include <default.h>

#define S3C2410_NFCONF				(0x4e000000)
#define S3C2410_NFCMD	  			(0x4e000004)
#define S3C2410_NFADDR	  			(0x4e000008)
#define S3C2410_NFDATA	  			(0x4e00000c)
#define S3C2410_NFSTAT	  			(0x4e000010)
#define S3C2410_NFECC	  			(0x4e000014)


#define S3C2410_NFCONF_EN			(1<<15)
#define S3C2410_NFCONF_512BYTE		(1<<14)
#define S3C2410_NFCONF_4STEP		(1<<13)
#define S3C2410_NFCONF_INITECC		(1<<12)
#define S3C2410_NFCONF_nFCE			(1<<11)
#define S3C2410_NFCONF_TACLS(x)		((x)<<8)
#define S3C2410_NFCONF_TWRPH0(x)	((x)<<4)
#define S3C2410_NFCONF_TWRPH1(x)	((x)<<0)
#define S3C2410_NFSTAT_BUSY			(1<<0)


#endif /* __S3C2410_REG_NAND_H__ */
