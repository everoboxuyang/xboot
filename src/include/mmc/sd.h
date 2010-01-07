#ifndef __SD_H__
#define __SD_H__

#include <configs.h>
#include <default.h>


/* SD commands                           type  argument     response */

/* class 0 */
#define SD_SEND_RELATIVE_ADDR		3	/* bcr                     R6  */
#define SD_SEND_IF_COND				8	/* bcr  [11:0] See below   R7  */

/* class 10 */
#define SD_SWITCH					6	/* adtc [31:0] See below   R1  */

/* application commands */
#define SD_APP_SET_BUS_WIDTH		6	/* ac   [1:0] bus width    R1  */
#define SD_APP_SEND_NUM_WR_BLKS		22	/* adtc                    R1  */
#define SD_APP_OP_COND				41	/* bcr  [31:0] OCR         R3  */
#define SD_APP_SEND_SCR				51	/* adtc                    R1  */

/*
 * SD_SWITCH argument format:
 *
 *      [31] Check (0) or switch (1)
 *      [30:24] Reserved (0)
 *      [23:20] Function group 6
 *      [19:16] Function group 5
 *      [15:12] Function group 4
 *      [11:8] Function group 3
 *      [7:4] Function group 2
 *      [3:0] Function group 1
 */

/*
 * SD_SEND_IF_COND argument format:
 *
 *	[31:12] Reserved (0)
 *	[11:8] Host Voltage Supply Flags
 *	[7:0] Check Pattern (0xAA)
 */

/*
 * SCR field definitions
 */
#define SCR_SPEC_VER_0				0	/* Implements system specification 1.0 - 1.01 */
#define SCR_SPEC_VER_1				1	/* Implements system specification 1.10 */
#define SCR_SPEC_VER_2				2	/* Implements system specification 2.00 */

/*
 * SD bus widths
 */
#define SD_BUS_WIDTH_1				0
#define SD_BUS_WIDTH_4				2

/*
 * SD_SWITCH mode
 */
#define SD_SWITCH_CHECK				0
#define SD_SWITCH_SET				1

/*
 * SD_SWITCH function groups
 */
#define SD_SWITCH_GRP_ACCESS		0

/*
 * SD_SWITCH access modes
 */
#define SD_SWITCH_ACCESS_DEF		0
#define SD_SWITCH_ACCESS_HS			1

#endif /* __SD_H__ */

