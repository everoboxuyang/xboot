/*
 * arch/arm/lib/cpu/cmd_dasm.c
 *
 * Copyright(c) 2007-2013 jianjun jiang <jerryjianjun@gmail.com>
 * official site: http://xboot.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <xboot.h>
#include <types.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include <io.h>
#include <fs/fileio.h>
#include <xboot/log.h>
#include <xboot/list.h>
#include <xboot/printk.h>
#include <xboot/initcall.h>
#include <command/command.h>


#if	defined(CONFIG_COMMAND_DASM) && (CONFIG_COMMAND_DASM > 0)

#define	COND(opcode)	\
	(arm_condition_strings[(opcode & 0xf0000000) >> 28])

enum arm_instruction_type
{
	ARM_UNKNOWN_INSTUCTION,

	/* branch instructions */
	ARM_B,
	ARM_BL,
	ARM_BX,
	ARM_BLX,

	/* data processing instructions */
	ARM_AND,
	ARM_EOR,
	ARM_SUB,
	ARM_RSB,
	ARM_ADD,
	ARM_ADC,
	ARM_SBC,
	ARM_RSC,
	ARM_TST,
	ARM_TEQ,
	ARM_CMP,
	ARM_CMN,
	ARM_ORR,
	ARM_MOV,
	ARM_BIC,
	ARM_MVN,

	/* load / store instructions */
	ARM_LDR,
	ARM_LDRB,
	ARM_LDRT,
	ARM_LDRBT,

	ARM_LDRH,
	ARM_LDRSB,
	ARM_LDRSH,

	ARM_LDM,

	ARM_STR,
	ARM_STRB,
	ARM_STRT,
	ARM_STRBT,

	ARM_STRH,

	ARM_STM,

	/* status register access instructions */
	ARM_MRS,
	ARM_MSR,

	/* multiply instructions */
	ARM_MUL,
	ARM_MLA,
	ARM_SMULL,
	ARM_SMLAL,
	ARM_UMULL,
	ARM_UMLAL,

	/* miscellaneous instructions */
	ARM_CLZ,

	/* exception generating instructions */
	ARM_BKPT,
	ARM_SWI,

	/* coprocessor instructions */
	ARM_CDP,
	ARM_LDC,
	ARM_STC,
	ARM_MCR,
	ARM_MRC,

	/* semaphore instructions */
	ARM_SWP,
	ARM_SWPB,

	/* enhanced dsp extensions */
	ARM_MCRR,
	ARM_MRRC,
	ARM_PLD,
	ARM_QADD,
	ARM_QDADD,
	ARM_QSUB,
	ARM_QDSUB,
	ARM_SMLAxy,
	ARM_SMLALxy,
	ARM_SMLAWy,
	ARM_SMULxy,
	ARM_SMULWy,
	ARM_LDRD,
	ARM_STRD,

	ARM_UNDEFINED_INSTRUCTION = 0xffffffff,
};

struct arm_b_bl_bx_blx_instr
{
	s32_t reg_operand;
	u32_t target_address;
};

union arm_shifter_operand
{
	struct {
		u32_t immediate;
	} immediate;
	struct {
		u8_t Rm;
		u8_t shift;			/* 0: LSL, 1: LSR, 2: ASR, 3: ROR, 4: RRX */
		u8_t shift_imm;
	} immediate_shift;
	struct {
		u8_t Rm;
		u8_t shift;
		u8_t Rs;
	} register_shift;
};

struct arm_data_proc_instr
{
	s32_t variant;			/* 0: immediate, 1: immediate_shift, 2: register_shift */
	u8_t S;
	u8_t Rn;
	u8_t Rd;
	union arm_shifter_operand shifter_operand;
};

struct arm_load_store_instr
{
	u8_t Rd;
	u8_t Rn;
	u8_t U;
	s32_t index_mode;		/* 0: offset, 1: pre-indexed, 2: post-indexed */
	s32_t offset_mode;		/* 0: immediate, 1: (scaled) register */
	union
	{
		u32_t offset;
		struct {
			u8_t Rm;
			u8_t shift; 	/* 0: LSL, 1: LSR, 2: ASR, 3: ROR, 4: RRX */
			u8_t shift_imm;
		} reg;
	} offset;
};

struct arm_load_store_multiple_instr
{
	u8_t Rn;
	u32_t register_list;
	u8_t addressing_mode; 	/* 0: IA, 1: IB, 2: DA, 3: DB */
	u8_t S;
	u8_t W;
};

struct arm_instruction
{
	enum arm_instruction_type type;
	char text[128];
	u32_t opcode;
	u32_t instruction_size;

	union {
		struct arm_b_bl_bx_blx_instr b_bl_bx_blx;
		struct arm_data_proc_instr data_proc;
		struct arm_load_store_instr load_store;
		struct arm_load_store_multiple_instr load_store_multiple;
	} info;
};

static const char * arm_condition_strings[] =
{
	"EQ", "NE", "CS", "CC", "MI", "PL", "VS", "VC", "HI", "LS", "GE", "LT", "GT", "LE", "", "NV"
};

/*
 * make up for c's missing ror
 */
static u32_t ror(u32_t value, s32_t places)
{
	return (value >> places) | (value << (32 - places));
}

static s32_t evaluate_unknown(u32_t opcode,	u32_t address, struct arm_instruction * instruction)
{
	instruction->type = ARM_UNDEFINED_INSTRUCTION;

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    UNDEFINED INSTRUCTION", address, opcode);

	return 0;
}

static s32_t evaluate_pld(u32_t opcode,	u32_t address, struct arm_instruction * instruction)
{
	/* pld */
	if ((opcode & 0x0d70f000) == 0x0550f000)
	{
		instruction->type = ARM_PLD;

		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    PLD...TODO...", address, opcode);

		return 0;
	}

	return evaluate_unknown(opcode, address, instruction);
}

static s32_t evaluate_srs(u32_t opcode,	u32_t address, struct arm_instruction * instruction)
{
	const char * wback = (opcode & (1 << 21)) ? "!" : "";
	const char * mode = "";

	switch((opcode >> 23) & 0x3)
	{
	case 0:
		mode = "DA";
		break;
	case 1:
		/* "IA" is default */
		break;
	case 2:
		mode = "DB";
		break;
	case 3:
		mode = "IB";
		break;
	}

	switch(opcode & 0x0e500000)
	{
	case 0x08400000:
		snprintf(instruction->text, 128,
				(const char *)"0x%08lx 0x%08lx    SRS%s SP%s, #%ld",
				address, opcode,
				mode, wback, (opcode & 0x1f));
		break;
	case 0x08100000:
		snprintf(instruction->text, 128,
				(const char *)"0x%08lx 0x%08lx    RFE%s r%ld%s",
				address, opcode,
				mode, ((opcode >> 16) & 0xf), wback);
		break;
	default:
		return evaluate_unknown(opcode, address, instruction);
	}

	return 0;
}

static s32_t evaluate_swi(u32_t opcode,	u32_t address, struct arm_instruction * instruction)
{
	instruction->type = ARM_SWI;

	snprintf(instruction->text, 128,
			(const char *)"0x%08lx 0x%08lx    SVC %#6.6lx",
			address, opcode, (opcode & 0xffffff));

	return 0;
}

static s32_t evaluate_blx_imm(u32_t opcode,	u32_t address, struct arm_instruction * instruction)
{
	s32_t offset;
	u32_t immediate;
	u32_t target_address;

	instruction->type = ARM_BLX;
	immediate = opcode & 0x00ffffff;

	/* sign extend 24-bit immediate */
	if (immediate & 0x00800000)
		offset = 0xff000000 | immediate;
	else
		offset = immediate;

	/* shift two bits left */
	offset <<= 2;

	/* odd / event halfword */
	if (opcode & 0x01000000)
		offset |= 0x2;

	target_address = address + 8 + offset;

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    BLX 0x%08lx", address, opcode, target_address);

	instruction->info.b_bl_bx_blx.reg_operand = -1;
	instruction->info.b_bl_bx_blx.target_address = target_address;

	return 0;
}

static s32_t evaluate_b_bl(u32_t opcode, u32_t address, struct arm_instruction * instruction)
{
	u8_t L;
	u32_t immediate;
	s32_t offset;
	u32_t target_address;

	immediate = opcode & 0x00ffffff;
	L = (opcode & 0x01000000) >> 24;

	/* sign extend 24-bit immediate */
	if (immediate & 0x00800000)
		offset = 0xff000000 | immediate;
	else
		offset = immediate;

	/* shift two bits left */
	offset <<= 2;

	target_address = address + 8 + offset;

	if(L)
		instruction->type = ARM_BL;
	else
		instruction->type = ARM_B;

	snprintf(instruction->text, 128,
			(const char *)"0x%08lx 0x%08lx    B%s%s 0x%08lx",
			address, opcode, (L) ? "L" : "", COND(opcode), target_address);

	instruction->info.b_bl_bx_blx.reg_operand = -1;
	instruction->info.b_bl_bx_blx.target_address = target_address;

	return 0;
}

/*
 * coprocessor load / store and double register transfers
 * both normal and extended instruction space (condition field b1111)
 */
static s32_t evaluate_ldc_stc_mcrr_mrrc(u32_t opcode, u32_t address, struct arm_instruction * instruction)
{
	u8_t cp_num = (opcode & 0xf00) >> 8;

	/* MCRR or MRRC */
	if(((opcode & 0x0ff00000) == 0x0c400000) || ((opcode & 0x0ff00000) == 0x0c400000))
	{
		u8_t cp_opcode, Rd, Rn, CRm;
		char * mnemonic;

		cp_opcode = (opcode & 0xf0) >> 4;
		Rd = (opcode & 0xf000) >> 12;
		Rn = (opcode & 0xf0000) >> 16;
		CRm = (opcode & 0xf);

		/* MCRR */
		if ((opcode & 0x0ff00000) == 0x0c400000)
		{
			instruction->type = ARM_MCRR;
			mnemonic = "MCRR";
		}

		/* MRRC */
		if ((opcode & 0x0ff00000) == 0x0c500000)
		{
			instruction->type = ARM_MRRC;
			mnemonic = "MRRC";
		}

		snprintf(instruction->text, 128,
				(const char *)"0x%08lx 0x%08lx    %s%s%s p%i, %x, r%i, r%i, c%i",
				address, opcode, mnemonic,	((opcode & 0xf0000000) == 0xf0000000) ? "2" : COND(opcode), COND(opcode), cp_num, cp_opcode, Rd, Rn, CRm);
	}
	/* LDC or STC */
	else
	{
		u8_t CRd, Rn, offset;
		u8_t U, N;
		char * mnemonic;
		char addressing_mode[32];

		CRd = (opcode & 0xf000) >> 12;
		Rn = (opcode & 0xf0000) >> 16;
		offset = (opcode & 0xff) << 2;

		/* load / store */
		if (opcode & 0x00100000)
		{
			instruction->type = ARM_LDC;
			mnemonic = "LDC";
		}
		else
		{
			instruction->type = ARM_STC;
			mnemonic = "STC";
		}

		U = (opcode & 0x00800000) >> 23;
		N = (opcode & 0x00400000) >> 22;

		/* addressing modes */
		if ((opcode & 0x01200000) == 0x01000000)		/* offset */
			snprintf(addressing_mode, 32, (const char *)"[r%i, #%s%d]",	Rn, U ? "" : "-", offset);
		else if ((opcode & 0x01200000) == 0x01200000)	/* pre-indexed */
			snprintf(addressing_mode, 32, (const char *)"[r%i, #%s%d]!", Rn, U ? "" : "-", offset);
		else if ((opcode & 0x01200000) == 0x00200000)	/* post-indexed */
			snprintf(addressing_mode, 32, (const char *)"[r%i], #%s%d",	Rn, U ? "" : "-", offset);
		else if ((opcode & 0x01200000) == 0x00000000)	/* unindexed */
			snprintf(addressing_mode, 32, (const char *)"[r%i], {%d}", Rn, offset >> 2);

		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    %s%s%s p%i, c%i, %s",
				address, opcode, mnemonic, ((opcode & 0xf0000000) == 0xf0000000) ? "2" : COND(opcode),
				(opcode & (1 << 22)) ? "L" : "", cp_num, CRd, addressing_mode);
	}

	return 0;
}

/*
 * coprocessor data processing instructions
 * coprocessor register transfer instructions
 * both normal and extended instruction space (condition field b1111)
 */
static s32_t evaluate_cdp_mcr_mrc(u32_t opcode,	u32_t address, struct arm_instruction * instruction)
{
	const char * cond;
	char * mnemonic;
	u8_t cp_num, opcode_1, CRd_Rd, CRn, CRm, opcode_2;

	cond = ((opcode & 0xf0000000) == 0xf0000000) ? "2" : COND(opcode);
	cp_num = (opcode & 0xf00) >> 8;
	CRd_Rd = (opcode & 0xf000) >> 12;
	CRn = (opcode & 0xf0000) >> 16;
	CRm = (opcode & 0xf);
	opcode_2 = (opcode & 0xe0) >> 5;

	/* CDP or MRC / MCR */
	if (opcode & 0x00000010)		/* bit 4 set -> MRC/MCR */
	{
		if (opcode & 0x00100000)	/* bit 20 set -> MRC */
		{
			instruction->type = ARM_MRC;
			mnemonic = "MRC";
		}
		else						/* bit 20 not set -> MCR */
		{
			instruction->type = ARM_MCR;
			mnemonic = "MCR";
		}

		opcode_1 = (opcode & 0x00e00000) >> 21;

		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    %s%s p%i, 0x%2.2x, r%i, c%i, c%i, 0x%2.2x",
				 address, opcode, mnemonic, cond, cp_num, opcode_1, CRd_Rd, CRn, CRm, opcode_2);
	}
	/* bit 4 not set -> CDP */
	else
	{
		instruction->type = ARM_CDP;
		mnemonic = "CDP";

		opcode_1 = (opcode & 0x00f00000) >> 20;

		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    %s%s p%i, 0x%2.2x, c%i, c%i, c%i, 0x%2.2x",
				 address, opcode, mnemonic, cond, cp_num, opcode_1, CRd_Rd, CRn, CRm, opcode_2);
	}

	return 0;
}

/*
 * load / store instructions
 */
static s32_t evaluate_load_store(u32_t opcode, u32_t address, struct arm_instruction * instruction)
{
	u8_t I, P, U, B, W, L;
	u8_t Rn, Rd;
	char * operation;	/* "LDR" or "STR" */
	char * suffix;		/* "", "B", "T", "BT" */
	char offset[32];

	/* examine flags */
	I = (opcode & 0x02000000) >> 25;
	P = (opcode & 0x01000000) >> 24;
	U = (opcode & 0x00800000) >> 23;
	B = (opcode & 0x00400000) >> 22;
	W = (opcode & 0x00200000) >> 21;
	L = (opcode & 0x00100000) >> 20;

	/* target register */
	Rd = (opcode & 0xf000) >> 12;

	/* base register */
	Rn = (opcode & 0xf0000) >> 16;

	instruction->info.load_store.Rd = Rd;
	instruction->info.load_store.Rn = Rn;
	instruction->info.load_store.U = U;

	/* determine operation */
	if (L)
		operation = "LDR";
	else
		operation = "STR";

	/* determine instruction type and suffix */
	if (B)
	{
		if ((P == 0) && (W == 1))
		{
			if (L)
				instruction->type = ARM_LDRBT;
			else
				instruction->type = ARM_STRBT;
			suffix = "BT";
		}
		else
		{
			if (L)
				instruction->type = ARM_LDRB;
			else
				instruction->type = ARM_STRB;
			suffix = "B";
		}
	}
	else
	{
		if ((P == 0) && (W == 1))
		{
			if (L)
				instruction->type = ARM_LDRT;
			else
				instruction->type = ARM_STRT;
			suffix = "T";
		}
		else
		{
			if (L)
				instruction->type = ARM_LDR;
			else
				instruction->type = ARM_STR;
			suffix = "";
		}
	}

	/* #+-<offset_12> */
	if (!I)
	{
		u32_t offset_12 = (opcode & 0xfff);
		if (offset_12)
			snprintf(offset, 32, (const char *)", #%s0x%lx", (U) ? "" : "-", offset_12);
		else
			snprintf(offset, 32, (const char *)"%s", "");

		instruction->info.load_store.offset_mode = 0;
		instruction->info.load_store.offset.offset = offset_12;
	}
	/* either +-<Rm> or +-<Rm>, <shift>, #<shift_imm> */
	else
	{
		u8_t shift_imm, shift;
		u8_t Rm;

		shift_imm = (opcode & 0xf80) >> 7;
		shift = (opcode & 0x60) >> 5;
		Rm = (opcode & 0xf);

		/* LSR encodes a shift by 32 bit as 0x0 */
		if ((shift == 0x1) && (shift_imm == 0x0))
			shift_imm = 0x20;

		/* ASR encodes a shift by 32 bit as 0x0 */
		if ((shift == 0x2) && (shift_imm == 0x0))
			shift_imm = 0x20;

		/* ROR by 32 bit is actually a RRX */
		if ((shift == 0x3) && (shift_imm == 0x0))
			shift = 0x4;

		instruction->info.load_store.offset_mode = 1;
		instruction->info.load_store.offset.reg.Rm = Rm;
		instruction->info.load_store.offset.reg.shift = shift;
		instruction->info.load_store.offset.reg.shift_imm = shift_imm;

		/* +-<Rm> */
		if ((shift_imm == 0x0) && (shift == 0x0))
		{
			snprintf(offset, 32, (const char *)", %sr%i", (U) ? "" : "-", Rm);
		}
		/* +-<Rm>, <Shift>, #<shift_imm> */
		else
		{
			switch (shift)
			{
				case 0x0: /* LSL */
					snprintf(offset, 32, (const char *)", %sr%i, LSL #0x%x", (U) ? "" : "-", Rm, shift_imm);
					break;
				case 0x1: /* LSR */
					snprintf(offset, 32, (const char *)", %sr%i, LSR #0x%x", (U) ? "" : "-", Rm, shift_imm);
					break;
				case 0x2: /* ASR */
					snprintf(offset, 32, (const char *)", %sr%i, ASR #0x%x", (U) ? "" : "-", Rm, shift_imm);
					break;
				case 0x3: /* ROR */
					snprintf(offset, 32, (const char *)", %sr%i, ROR #0x%x", (U) ? "" : "-", Rm, shift_imm);
					break;
				case 0x4: /* RRX */
					snprintf(offset, 32, (const char *)", %sr%i, RRX", (U) ? "" : "-", Rm);
					break;
			}
		}
	}

	if (P == 1)
	{
		if (W == 0)	/* offset */
		{
			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    %s%s%s r%i, [r%i%s]",
					 address, opcode, operation, COND(opcode), suffix, Rd, Rn, offset);

			instruction->info.load_store.index_mode = 0;
		}
		else		/* pre-indexed */
		{
			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    %s%s%s r%i, [r%i%s]!",
					 address, opcode, operation, COND(opcode), suffix, Rd, Rn, offset);

			instruction->info.load_store.index_mode = 1;
		}
	}
	else			/* post-indexed */
	{
		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    %s%s%s r%i, [r%i]%s",
				 address, opcode, operation, COND(opcode), suffix, Rd, Rn, offset);

		instruction->info.load_store.index_mode = 2;
	}

	return 0;
}

static s32_t evaluate_extend(u32_t opcode, u32_t address, char * cp)
{
	u8_t rm = (opcode >> 0) & 0xf;
	u8_t rd = (opcode >> 12) & 0xf;
	u8_t rn = (opcode >> 16) & 0xf;
	char * type, * rot;

	switch ((opcode >> 24) & 0x3)
	{
	case 0:
		type = "B16";
		break;
	case 1:
		sprintf((char *)cp, (const char *)"UNDEFINED");
		return ARM_UNDEFINED_INSTRUCTION;
	case 2:
		type = "B";
		break;
	default:
		type = "H";
		break;
	}

	switch ((opcode >> 10) & 0x3)
	{
	case 0:
		rot = "";
		break;
	case 1:
		rot = ", ROR #8";
		break;
	case 2:
		rot = ", ROR #16";
		break;
	default:
		rot = ", ROR #24";
		break;
	}

	if (rn == 0xf)
	{
		sprintf((char *)cp, (const char *)"%cXT%s%s r%d, r%d%s",
				(opcode & (1 << 22)) ? 'U' : 'S',
				type, COND(opcode),
				rd, rm, rot);
		return ARM_MOV;
	}
	else
	{
		sprintf((char *)cp, (const char *)"%cXTA%s%s r%d, r%d, r%d%s",
				(opcode & (1 << 22)) ? 'U' : 'S',
				type, COND(opcode),
				rd, rn, rm, rot);
		return ARM_ADD;
	}
}

static s32_t evaluate_p_add_sub(u32_t opcode, u32_t address, char * cp)
{
	char * prefix;
	char * op;
	s32_t type;

	switch ((opcode >> 20) & 0x7)
	{
	case 1:
		prefix = "S";
		break;
	case 2:
		prefix = "Q";
		break;
	case 3:
		prefix = "SH";
		break;
	case 5:
		prefix = "U";
		break;
	case 6:
		prefix = "UQ";
		break;
	case 7:
		prefix = "UH";
		break;
	default:
		goto undef;
	}

	switch ((opcode >> 5) & 0x7)
	{
	case 0:
		op = "ADD16";
		type = ARM_ADD;
		break;
	case 1:
		op = "ADDSUBX";
		type = ARM_ADD;
		break;
	case 2:
		op = "SUBADDX";
		type = ARM_SUB;
		break;
	case 3:
		op = "SUB16";
		type = ARM_SUB;
		break;
	case 4:
		op = "ADD8";
		type = ARM_ADD;
		break;
	case 7:
		op = "SUB8";
		type = ARM_SUB;
		break;
	default:
		goto undef;
	}

	sprintf((char *)cp, (const char *)"%s%s%s r%d, r%d, r%d", prefix, op, COND(opcode),
			(u8_t) (opcode >> 12) & 0xf,
			(u8_t) (opcode >> 16) & 0xf,
			(u8_t) (opcode >> 0) & 0xf);
	return type;

undef:
	/*
	 * these opcodes might be used someday
	 */
	sprintf((char *)cp, (const char *)"UNDEFINED");
	return ARM_UNDEFINED_INSTRUCTION;
}

/*
 * ARMv6 and later support "media" instructions (includes SIMD)
 */
static s32_t evaluate_media(u32_t opcode, u32_t address, struct arm_instruction * instruction)
{
	char * cp = instruction->text;
	char * mnemonic = NULL;

	sprintf(cp, (const char *)"0x%08lx 0x%08lx    ", address, opcode);
	cp = strchr(cp, 0);

	/* parallel add / subtract */
	if ((opcode & 0x01800000) == 0x00000000)
	{
		instruction->type = evaluate_p_add_sub(opcode, address, cp);
		return 0;
	}

	/* halfword pack */
	if ((opcode & 0x01f00020) == 0x00800000)
	{
		char * type, * shift;
		u8_t imm = (unsigned) (opcode >> 7) & 0x1f;

		if (opcode & (1 << 6))
		{
			type = "TB";
			shift = "ASR";
			if (imm == 0)
				imm = 32;
		}
		else
		{
			type = "BT";
			shift = "LSL";
		}
		sprintf(cp, (const char *)"PKH%s%s r%d, r%d, r%d, %s #%d",
			type, COND(opcode),
			(u8_t) (opcode >> 12) & 0xf,
			(u8_t) (opcode >> 16) & 0xf,
			(u8_t) (opcode >> 0) & 0xf,
			shift, imm);
		return 0;
	}

	/* word saturate */
	if ((opcode & 0x01a00020) == 0x00a00000)
	{
		char * shift;
		u8_t imm = (u8_t)(opcode >> 7) & 0x1f;

		if (opcode & (1 << 6))
		{
			shift = "ASR";
			if (imm == 0)
				imm = 32;
		}
		else
		{
			shift = "LSL";
		}

		sprintf(cp, (const char *)"%cSAT%s r%d, #%d, r%d, %s #%d",
			(opcode & (1 << 22)) ? 'U' : 'S',
			COND(opcode),
			(u8_t) (opcode >> 12) & 0xf,
			(u8_t) (opcode >> 16) & 0x1f,
			(u8_t) (opcode >> 0) & 0xf,
			shift, imm);
		return 0;
	}

	/* sign extension */
	if ((opcode & 0x018000f0) == 0x00800070)
	{
		instruction->type = evaluate_extend(opcode, address, cp);
		return 0;
	}

	/* multiplies */
	if ((opcode & 0x01f00080) == 0x01000000)
	{
		u8_t rn = (opcode >> 12) & 0xf;

		if (rn != 0xf)
			sprintf(cp, (const char *)"SML%cD%s%s r%d, r%d, r%d, r%d",
				(opcode & (1 << 6)) ? 'S' : 'A',
				(opcode & (1 << 5)) ? "X" : "",
				COND(opcode),
				(u8_t) (opcode >> 16) & 0xf,
				(u8_t) (opcode >> 0) & 0xf,
				(u8_t) (opcode >> 8) & 0xf,
				rn);
		else
			sprintf(cp, (const char *)"SMU%cD%s%s r%d, r%d, r%d",
				(opcode & (1 << 6)) ? 'S' : 'A',
				(opcode & (1 << 5)) ? "X" : "",
				COND(opcode),
				(u8_t) (opcode >> 16) & 0xf,
				(u8_t) (opcode >> 0) & 0xf,
				(u8_t) (opcode >> 8) & 0xf);
		return 0;
	}
	if ((opcode & 0x01f00000) == 0x01400000)
	{
		sprintf(cp, (const char *)"SML%cLD%s%s r%d, r%d, r%d, r%d",
			(opcode & (1 << 6)) ? 'S' : 'A',
			(opcode & (1 << 5)) ? "X" : "",
			COND(opcode),
			(u8_t) (opcode >> 12) & 0xf,
			(u8_t) (opcode >> 16) & 0xf,
			(u8_t) (opcode >> 0) & 0xf,
			(u8_t) (opcode >> 8) & 0xf);
		return 0;
	}
	if ((opcode & 0x01f00000) == 0x01500000)
	{
		u8_t rn = (opcode >> 12) & 0xf;

		switch (opcode & 0xc0)
		{
		case 3:
			if (rn == 0xf)
				goto undef;
		case 0:
			break;
		default:
			goto undef;
		}

		if (rn != 0xf)
			sprintf(cp, (const char *)"SMML%c%s%s r%d, r%d, r%d, r%d",
				(opcode & (1 << 6)) ? 'S' : 'A',
				(opcode & (1 << 5)) ? "R" : "",
				COND(opcode),
				(u8_t) (opcode >> 16) & 0xf,
				(u8_t) (opcode >> 0) & 0xf,
				(u8_t) (opcode >> 8) & 0xf,
				rn);
		else
			sprintf(cp, (const char *)"SMMUL%s%s r%d, r%d, r%d",
				(opcode & (1 << 5)) ? "R" : "",
				COND(opcode),
				(u8_t) (opcode >> 16) & 0xf,
				(u8_t) (opcode >> 0) & 0xf,
				(u8_t) (opcode >> 8) & 0xf);
		return 0;
	}


	/* simple matches against the remaining decode bits */
	switch (opcode & 0x01f000f0)
	{
	case 0x00a00030:
	case 0x00e00030:
		/* parallel halfword saturate */
		sprintf(cp, (const char *)"%cSAT16%s r%d, #%d, r%d",
			(opcode & (1 << 22)) ? 'U' : 'S',
			COND(opcode),
			(u8_t) (opcode >> 12) & 0xf,
			(u8_t) (opcode >> 16) & 0xf,
			(u8_t) (opcode >> 0) & 0xf);
		return 0;
	case 0x00b00030:
		mnemonic = "REV";
		break;
	case 0x00b000b0:
		mnemonic = "REV16";
		break;
	case 0x00f000b0:
		mnemonic = "REVSH";
		break;
	case 0x008000b0:
		/* select bytes */
		sprintf(cp, (const char *)"SEL%s r%d, r%d, r%d", COND(opcode),
			(u8_t) (opcode >> 12) & 0xf,
			(u8_t) (opcode >> 16) & 0xf,
			(u8_t) (opcode >> 0) & 0xf);
		return 0;
	case 0x01800010:
		/* unsigned sum of absolute differences */
		if (((opcode >> 12) & 0xf) == 0xf)
			sprintf(cp, (const char *)"USAD8%s r%d, r%d, r%d", COND(opcode),
				(u8_t) (opcode >> 16) & 0xf,
				(u8_t) (opcode >> 0) & 0xf,
				(u8_t) (opcode >> 8) & 0xf);
		else
			sprintf(cp, (const char *)"USADA8%s r%d, r%d, r%d, r%d", COND(opcode),
				(u8_t) (opcode >> 16) & 0xf,
				(u8_t) (opcode >> 0) & 0xf,
				(u8_t) (opcode >> 8) & 0xf,
				(u8_t) (opcode >> 12) & 0xf);
		return 0;
	}
	if (mnemonic) {
		u8_t rm = (opcode >> 0) & 0xf;
		u8_t rd = (opcode >> 12) & 0xf;

		sprintf(cp, (const char *)"%s%s r%d, r%d", mnemonic, COND(opcode), rm, rd);
		return 0;
	}

undef:
	/* these opcodes might be used someday */
	sprintf(cp, (const char *)"UNDEFINED");
	return 0;
}

/*
 * miscellaneous load/store instructions
 */
static s32_t evaluate_misc_load_store(u32_t opcode,	u32_t address, struct arm_instruction * instruction)
{
	u8_t P, U, I, W, L, S, H;
	u8_t Rn, Rd;
	char * operation;	/* "LDR" or "STR" */
	char * suffix;		/* "H", "SB", "SH", "D" */
	char offset[32];

	/* examine flags */
	P = (opcode & 0x01000000) >> 24;
	U = (opcode & 0x00800000) >> 23;
	I = (opcode & 0x00400000) >> 22;
	W = (opcode & 0x00200000) >> 21;
	L = (opcode & 0x00100000) >> 20;
	S = (opcode & 0x00000040) >> 6;
	H = (opcode & 0x00000020) >> 5;

	/* target register */
	Rd = (opcode & 0xf000) >> 12;

	/* base register */
	Rn = (opcode & 0xf0000) >> 16;

	instruction->info.load_store.Rd = Rd;
	instruction->info.load_store.Rn = Rn;
	instruction->info.load_store.U = U;

	/* determine instruction type and suffix */
	if (S) /* signed */
	{
		if (L) /* load */
		{
			if (H)
			{
				operation = "LDR";
				instruction->type = ARM_LDRSH;
				suffix = "SH";
			}
			else
			{
				operation = "LDR";
				instruction->type = ARM_LDRSB;
				suffix = "SB";
			}
		}
		else /* there are no signed stores, so this is used to encode double-register load/stores */
		{
			suffix = "D";
			if (H)
			{
				operation = "STR";
				instruction->type = ARM_STRD;
			}
			else
			{
				operation = "LDR";
				instruction->type = ARM_LDRD;
			}
		}
	}
	else /* unsigned */
	{
		suffix = "H";
		if (L) /* load */
		{
			operation = "LDR";
			instruction->type = ARM_LDRH;
		}
		else /* store */
		{
			operation = "STR";
			instruction->type = ARM_STRH;
		}
	}

	if (I) /* Immediate offset/index (#+-<offset_8>)*/
	{
		u32_t offset_8 = ((opcode & 0xf00) >> 4) | (opcode & 0xf);
		snprintf(offset, 32, (const char *)"#%s0x%lx", (U) ? "" : "-", offset_8);

		instruction->info.load_store.offset_mode = 0;
		instruction->info.load_store.offset.offset = offset_8;
	}
	else /* Register offset/index (+-<Rm>) */
	{
		u8_t Rm;
		Rm = (opcode & 0xf);
		snprintf(offset, 32, (const char *)"%sr%i", (U) ? "" : "-", Rm);

		instruction->info.load_store.offset_mode = 1;
		instruction->info.load_store.offset.reg.Rm = Rm;
		instruction->info.load_store.offset.reg.shift = 0x0;
		instruction->info.load_store.offset.reg.shift_imm = 0x0;
	}

	if (P == 1)
	{
		if (W == 0) /* offset */
		{
			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    %s%s%s r%i, [r%i, %s]",
					 address, opcode, operation, COND(opcode), suffix,
					 Rd, Rn, offset);

			instruction->info.load_store.index_mode = 0;
		}
		else /* pre-indexed */
		{
			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    %s%s%s r%i, [r%i, %s]!",
					 address, opcode, operation, COND(opcode), suffix,
					 Rd, Rn, offset);

			instruction->info.load_store.index_mode = 1;
		}
	}
	else /* post-indexed */
	{
		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    %s%s%s r%i, [r%i], %s",
				 address, opcode, operation, COND(opcode), suffix,
				 Rd, Rn, offset);

		instruction->info.load_store.index_mode = 2;
	}

	return 0;
}

/*
 * load / store multiples instructions
 */
static s32_t evaluate_ldm_stm(u32_t opcode,	u32_t address, struct arm_instruction * instruction)
{
	u8_t P, U, S, W, L, Rn;
	u32_t register_list;
	char * addressing_mode;
	char * mnemonic;
	char reg_list[69];
	char * reg_list_p;
	s32_t i;
	s32_t first_reg = 1;

	P = (opcode & 0x01000000) >> 24;
	U = (opcode & 0x00800000) >> 23;
	S = (opcode & 0x00400000) >> 22;
	W = (opcode & 0x00200000) >> 21;
	L = (opcode & 0x00100000) >> 20;
	register_list = (opcode & 0xffff);
	Rn = (opcode & 0xf0000) >> 16;

	instruction->info.load_store_multiple.Rn = Rn;
	instruction->info.load_store_multiple.register_list = register_list;
	instruction->info.load_store_multiple.S = S;
	instruction->info.load_store_multiple.W = W;

	if (L)
	{
		instruction->type = ARM_LDM;
		mnemonic = "LDM";
	}
	else
	{
		instruction->type = ARM_STM;
		mnemonic = "STM";
	}

	if (P)
	{
		if (U)
		{
			instruction->info.load_store_multiple.addressing_mode = 1;
			addressing_mode = "IB";
		}
		else
		{
			instruction->info.load_store_multiple.addressing_mode = 3;
			addressing_mode = "DB";
		}
	}
	else
	{
		if (U)
		{
			instruction->info.load_store_multiple.addressing_mode = 0;
			/* "IA" is the default in UAL syntax */
			addressing_mode = "";
		}
		else
		{
			instruction->info.load_store_multiple.addressing_mode = 2;
			addressing_mode = "DA";
		}
	}

	reg_list_p = reg_list;
	for (i = 0; i <= 15; i++)
	{
		if ((register_list >> i) & 1)
		{
			if (first_reg)
			{
				first_reg = 0;
				reg_list_p += snprintf((char *)reg_list_p, (reg_list + 69 - reg_list_p), (const char *)"r%i", (u8_t)i);
			}
			else
			{
				reg_list_p += snprintf((char *)reg_list_p, (reg_list + 69 - reg_list_p), (const char *)", r%i", (u8_t)i);
			}
		}
	}

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    %s%s%s r%i%s, {%s}%s",
			 address, opcode,
			 mnemonic, addressing_mode, COND(opcode),
			 Rn, (W) ? "!" : "", reg_list, (S) ? "^" : "");

	return 0;
}

/*
 * multiplies, extra load / stores
 */
static s32_t evaluate_mul_and_extra_ld_st(u32_t opcode,	u32_t address, struct arm_instruction * instruction)
{
	/* Multiply (accumulate) (long) and Swap / swap byte */
	if ((opcode & 0x000000f0) == 0x00000090)
	{
		/* Multiply (accumulate) */
		if ((opcode & 0x0f800000) == 0x00000000)
		{
			u8_t Rm, Rs, Rn, Rd, S;
			Rm = opcode & 0xf;
			Rs = (opcode & 0xf00) >> 8;
			Rn = (opcode & 0xf000) >> 12;
			Rd = (opcode & 0xf0000) >> 16;
			S = (opcode & 0x00100000) >> 20;

			/* examine A bit (accumulate) */
			if (opcode & 0x00200000)
			{
				instruction->type = ARM_MLA;
				snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    MLA%s%s r%i, r%i, r%i, r%i",
						address, opcode, COND(opcode), (S) ? "S" : "", Rd, Rm, Rs, Rn);
			}
			else
			{
				instruction->type = ARM_MUL;
				snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    MUL%s%s r%i, r%i, r%i",
						 address, opcode, COND(opcode), (S) ? "S" : "", Rd, Rm, Rs);
			}

			return 0;
		}

		/* Multiply (accumulate) long */
		if ((opcode & 0x0f800000) == 0x00800000)
		{
			char* mnemonic = NULL;
			u8_t Rm, Rs, RdHi, RdLow, S;
			Rm = opcode & 0xf;
			Rs = (opcode & 0xf00) >> 8;
			RdHi = (opcode & 0xf000) >> 12;
			RdLow = (opcode & 0xf0000) >> 16;
			S = (opcode & 0x00100000) >> 20;

			switch ((opcode & 0x00600000) >> 21)
			{
				case 0x0:
					instruction->type = ARM_UMULL;
					mnemonic = "UMULL";
					break;
				case 0x1:
					instruction->type = ARM_UMLAL;
					mnemonic = "UMLAL";
					break;
				case 0x2:
					instruction->type = ARM_SMULL;
					mnemonic = "SMULL";
					break;
				case 0x3:
					instruction->type = ARM_SMLAL;
					mnemonic = "SMLAL";
					break;
			}

			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    %s%s%s r%i, r%i, r%i, r%i",
						address, opcode, mnemonic, COND(opcode), (S) ? "S" : "",
						RdLow, RdHi, Rm, Rs);

			return 0;
		}

		/* Swap / swap byte */
		if ((opcode & 0x0f800000) == 0x01000000)
		{
			u8_t Rm, Rd, Rn;
			Rm = opcode & 0xf;
			Rd = (opcode & 0xf000) >> 12;
			Rn = (opcode & 0xf0000) >> 16;

			/* examine B flag */
			instruction->type = (opcode & 0x00400000) ? ARM_SWPB : ARM_SWP;

			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    %s%s r%i, r%i, [r%i]",
					 address, opcode, (opcode & 0x00400000) ? "SWPB" : "SWP", COND(opcode), Rd, Rm, Rn);
			return 0;
		}

	}

	return evaluate_misc_load_store(opcode, address, instruction);
}

static s32_t evaluate_mrs_msr(u32_t opcode,	u32_t address, struct arm_instruction * instruction)
{
	s32_t R = (opcode & 0x00400000) >> 22;
	char * PSR = (R) ? "SPSR" : "CPSR";

	/* Move register to status register (MSR) */
	if (opcode & 0x00200000)
	{
		instruction->type = ARM_MSR;

		/* immediate variant */
		if (opcode & 0x02000000)
		{
			u8_t immediate = (opcode & 0xff);
			u8_t rotate = (opcode & 0xf00);

			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    MSR%s %s_%s%s%s%s, 0x%08lx" ,
					 address, opcode, COND(opcode), PSR,
					 (opcode & 0x10000) ? "c" : "",
					 (opcode & 0x20000) ? "x" : "",
					 (opcode & 0x40000) ? "s" : "",
					 (opcode & 0x80000) ? "f" : "",
					 ror(immediate, (rotate * 2)));
		}
		/* register variant */
		else
		{
			u8_t Rm = opcode & 0xf;
			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    MSR%s %s_%s%s%s%s, r%i",
					 address, opcode, COND(opcode), PSR,
					 (opcode & 0x10000) ? "c" : "",
					 (opcode & 0x20000) ? "x" : "",
					 (opcode & 0x40000) ? "s" : "",
					 (opcode & 0x80000) ? "f" : "",
					 Rm);
		}

	}
	else /* Move status register to register (MRS) */
	{
		u8_t Rd;

		instruction->type = ARM_MRS;
		Rd = (opcode & 0x0000f000) >> 12;

		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    MRS%s r%i, %s",
				 address, opcode, COND(opcode), Rd, PSR);
	}

	return 0;
}

/*
 * miscellaneous instructions
 */
static s32_t evaluate_misc_instr(u32_t opcode, u32_t address, struct arm_instruction * instruction)
{
	/* MRS / MSR */
	if ((opcode & 0x000000f0) == 0x00000000)
	{
		evaluate_mrs_msr(opcode, address, instruction);
	}

	/* BX */
	if ((opcode & 0x006000f0) == 0x00200010)
	{
		u8_t Rm;
		instruction->type = ARM_BX;
		Rm = opcode & 0xf;

		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    BX%s r%i",
				 address, opcode, COND(opcode), Rm);

		instruction->info.b_bl_bx_blx.reg_operand = Rm;
		instruction->info.b_bl_bx_blx.target_address = -1;
	}

	/* BXJ - "Jazelle" support (ARMv5-J) */
	if ((opcode & 0x006000f0) == 0x00200020)
	{
		u8_t Rm;
		instruction->type = ARM_BX;
		Rm = opcode & 0xf;

		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    BXJ%s r%i",
				 address, opcode, COND(opcode), Rm);

		instruction->info.b_bl_bx_blx.reg_operand = Rm;
		instruction->info.b_bl_bx_blx.target_address = -1;
	}

	/* CLZ */
	if ((opcode & 0x006000f0) == 0x00600010)
	{
		u8_t Rm, Rd;
		instruction->type = ARM_CLZ;
		Rm = opcode & 0xf;
		Rd = (opcode & 0xf000) >> 12;

		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    CLZ%s r%i, r%i",
				 address, opcode, COND(opcode), Rd, Rm);
	}

	/* BLX(2) */
	if ((opcode & 0x006000f0) == 0x00200030)
	{
		u8_t Rm;
		instruction->type = ARM_BLX;
		Rm = opcode & 0xf;

		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    BLX%s r%i",
				 address, opcode, COND(opcode), Rm);

		instruction->info.b_bl_bx_blx.reg_operand = Rm;
		instruction->info.b_bl_bx_blx.target_address = -1;
	}

	/* Enhanced DSP add/subtracts */
	if ((opcode & 0x0000000f0) == 0x00000050)
	{
		u8_t Rm, Rd, Rn;
		char *mnemonic = NULL;
		Rm = opcode & 0xf;
		Rd = (opcode & 0xf000) >> 12;
		Rn = (opcode & 0xf0000) >> 16;

		switch ((opcode & 0x00600000) >> 21)
		{
			case 0x0:
				instruction->type = ARM_QADD;
				mnemonic = "QADD";
				break;
			case 0x1:
				instruction->type = ARM_QSUB;
				mnemonic = "QSUB";
				break;
			case 0x2:
				instruction->type = ARM_QDADD;
				mnemonic = "QDADD";
				break;
			case 0x3:
				instruction->type = ARM_QDSUB;
				mnemonic = "QDSUB";
				break;
		}

		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    %s%s r%i, r%i, r%i",
				 address, opcode, mnemonic, COND(opcode), Rd, Rm, Rn);
	}

	/* Software breakpoints */
	if ((opcode & 0x0000000f0) == 0x00000070)
	{
		u32_t immediate;
		instruction->type = ARM_BKPT;
		immediate = ((opcode & 0x000fff00) >> 4) | (opcode & 0xf);

		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    BKPT 0x%04lx",
				 address, opcode, immediate);
	}

	/* Enhanced DSP multiplies */
	if ((opcode & 0x000000090) == 0x00000080)
	{
		s32_t x = (opcode & 0x20) >> 5;
		s32_t y = (opcode & 0x40) >> 6;

		/* SMLA < x><y> */
		if ((opcode & 0x00600000) == 0x00000000)
		{
			u8_t Rd, Rm, Rs, Rn;
			instruction->type = ARM_SMLAxy;
			Rd = (opcode & 0xf0000) >> 16;
			Rm = (opcode & 0xf);
			Rs = (opcode & 0xf00) >> 8;
			Rn = (opcode & 0xf000) >> 12;

			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    SMLA%s%s%s r%i, r%i, r%i, r%i",
					 address, opcode, (x) ? "T" : "B", (y) ? "T" : "B", COND(opcode),
					 Rd, Rm, Rs, Rn);
		}

		/* SMLAL < x><y> */
		if ((opcode & 0x00600000) == 0x00400000)
		{
			u8_t RdLow, RdHi, Rm, Rs;
			instruction->type = ARM_SMLAxy;
			RdHi = (opcode & 0xf0000) >> 16;
			RdLow = (opcode & 0xf000) >> 12;
			Rm = (opcode & 0xf);
			Rs = (opcode & 0xf00) >> 8;

			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    SMLA%s%s%s r%i, r%i, r%i, r%i",
					 address, opcode, (x) ? "T" : "B", (y) ? "T" : "B", COND(opcode),
					 RdLow, RdHi, Rm, Rs);
		}

		/* SMLAW < y> */
		if (((opcode & 0x00600000) == 0x00100000) && (x == 0))
		{
			u8_t Rd, Rm, Rs, Rn;
			instruction->type = ARM_SMLAWy;
			Rd = (opcode & 0xf0000) >> 16;
			Rm = (opcode & 0xf);
			Rs = (opcode & 0xf00) >> 8;
			Rn = (opcode & 0xf000) >> 12;

			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    SMLAW%s%s r%i, r%i, r%i, r%i",
					 address, opcode, (y) ? "T" : "B", COND(opcode),
					 Rd, Rm, Rs, Rn);
		}

		/* SMUL < x><y> */
		if ((opcode & 0x00600000) == 0x00300000)
		{
			u8_t Rd, Rm, Rs;
			instruction->type = ARM_SMULxy;
			Rd = (opcode & 0xf0000) >> 16;
			Rm = (opcode & 0xf);
			Rs = (opcode & 0xf00) >> 8;

			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    SMULW%s%s%s r%i, r%i, r%i",
					 address, opcode, (x) ? "T" : "B", (y) ? "T" : "B", COND(opcode),
					 Rd, Rm, Rs);
		}

		/* SMULW < y> */
		if (((opcode & 0x00600000) == 0x00100000) && (x == 1))
		{
			u8_t Rd, Rm, Rs;
			instruction->type = ARM_SMULWy;
			Rd = (opcode & 0xf0000) >> 16;
			Rm = (opcode & 0xf);
			Rs = (opcode & 0xf00) >> 8;

			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    SMULW%s%s r%i, r%i, r%i",
					 address, opcode, (y) ? "T" : "B", COND(opcode),
					 Rd, Rm, Rs);
		}
	}

	return 0;
}

static s32_t evaluate_data_proc(u32_t opcode, u32_t address, struct arm_instruction * instruction)
{
	u8_t I, op, S, Rn, Rd;
	char * mnemonic = NULL;
	char shifter_operand[32];

	I = (opcode & 0x02000000) >> 25;
	op = (opcode & 0x01e00000) >> 21;
	S = (opcode & 0x00100000) >> 20;

	Rd = (opcode & 0xf000) >> 12;
	Rn = (opcode & 0xf0000) >> 16;

	instruction->info.data_proc.Rd = Rd;
	instruction->info.data_proc.Rn = Rn;
	instruction->info.data_proc.S = S;

	switch (op)
	{
		case 0x0:
			instruction->type = ARM_AND;
			mnemonic = "AND";
			break;
		case 0x1:
			instruction->type = ARM_EOR;
			mnemonic = "EOR";
			break;
		case 0x2:
			instruction->type = ARM_SUB;
			mnemonic = "SUB";
			break;
		case 0x3:
			instruction->type = ARM_RSB;
			mnemonic = "RSB";
			break;
		case 0x4:
			instruction->type = ARM_ADD;
			mnemonic = "ADD";
			break;
		case 0x5:
			instruction->type = ARM_ADC;
			mnemonic = "ADC";
			break;
		case 0x6:
			instruction->type = ARM_SBC;
			mnemonic = "SBC";
			break;
		case 0x7:
			instruction->type = ARM_RSC;
			mnemonic = "RSC";
			break;
		case 0x8:
			instruction->type = ARM_TST;
			mnemonic = "TST";
			break;
		case 0x9:
			instruction->type = ARM_TEQ;
			mnemonic = "TEQ";
			break;
		case 0xa:
			instruction->type = ARM_CMP;
			mnemonic = "CMP";
			break;
		case 0xb:
			instruction->type = ARM_CMN;
			mnemonic = "CMN";
			break;
		case 0xc:
			instruction->type = ARM_ORR;
			mnemonic = "ORR";
			break;
		case 0xd:
			instruction->type = ARM_MOV;
			mnemonic = "MOV";
			break;
		case 0xe:
			instruction->type = ARM_BIC;
			mnemonic = "BIC";
			break;
		case 0xf:
			instruction->type = ARM_MVN;
			mnemonic = "MVN";
			break;
	}

	if (I) /* immediate shifter operand (#<immediate>)*/
	{
		u8_t immed_8 = opcode & 0xff;
		u8_t rotate_imm = (opcode & 0xf00) >> 8;
		u32_t immediate;

		immediate = ror(immed_8, rotate_imm * 2);

		snprintf(shifter_operand, 32, (const char *)"#0x%lx", immediate);

		instruction->info.data_proc.variant = 0;
		instruction->info.data_proc.shifter_operand.immediate.immediate = immediate;
	}
	else /* register-based shifter operand */
	{
		u8_t shift, Rm;
		shift = (opcode & 0x60) >> 5;
		Rm = (opcode & 0xf);

		if ((opcode & 0x10) != 0x10) /* Immediate shifts ("<Rm>" or "<Rm>, <shift> #<shift_immediate>") */
		{
			u8_t shift_imm;
			shift_imm = (opcode & 0xf80) >> 7;

			instruction->info.data_proc.variant = 1;
			instruction->info.data_proc.shifter_operand.immediate_shift.Rm = Rm;
			instruction->info.data_proc.shifter_operand.immediate_shift.shift_imm = shift_imm;
			instruction->info.data_proc.shifter_operand.immediate_shift.shift = shift;

			/* LSR encodes a shift by 32 bit as 0x0 */
			if ((shift == 0x1) && (shift_imm == 0x0))
				shift_imm = 0x20;

			/* ASR encodes a shift by 32 bit as 0x0 */
			if ((shift == 0x2) && (shift_imm == 0x0))
				shift_imm = 0x20;

			/* ROR by 32 bit is actually a RRX */
			if ((shift == 0x3) && (shift_imm == 0x0))
				shift = 0x4;

			if ((shift_imm == 0x0) && (shift == 0x0))
			{
				snprintf(shifter_operand, 32, (const char *)"r%i", Rm);
			}
			else
			{
				if (shift == 0x0) /* LSL */
				{
					snprintf(shifter_operand, 32, (const char *)"r%i, LSL #0x%x", Rm, shift_imm);
				}
				else if (shift == 0x1) /* LSR */
				{
					snprintf(shifter_operand, 32, (const char *)"r%i, LSR #0x%x", Rm, shift_imm);
				}
				else if (shift == 0x2) /* ASR */
				{
					snprintf(shifter_operand, 32, (const char *)"r%i, ASR #0x%x", Rm, shift_imm);
				}
				else if (shift == 0x3) /* ROR */
				{
					snprintf(shifter_operand, 32, (const char *)"r%i, ROR #0x%x", Rm, shift_imm);
				}
				else if (shift == 0x4) /* RRX */
				{
					snprintf(shifter_operand, 32, (const char *)"r%i, RRX", Rm);
				}
			}
		}
		else /* Register shifts ("<Rm>, <shift> <Rs>") */
		{
			u8_t Rs = (opcode & 0xf00) >> 8;

			instruction->info.data_proc.variant = 2;
			instruction->info.data_proc.shifter_operand.register_shift.Rm = Rm;
			instruction->info.data_proc.shifter_operand.register_shift.Rs = Rs;
			instruction->info.data_proc.shifter_operand.register_shift.shift = shift;

			if (shift == 0x0) /* LSL */
			{
				snprintf(shifter_operand, 32, (const char *)"r%i, LSL r%i", Rm, Rs);
			}
			else if (shift == 0x1) /* LSR */
			{
				snprintf(shifter_operand, 32, (const char *)"r%i, LSR r%i", Rm, Rs);
			}
			else if (shift == 0x2) /* ASR */
			{
				snprintf(shifter_operand, 32, (const char *)"r%i, ASR r%i", Rm, Rs);
			}
			else if (shift == 0x3) /* ROR */
			{
				snprintf(shifter_operand, 32, (const char *)"r%i, ROR r%i", Rm, Rs);
			}
		}
	}

	if ((op < 0x8) || (op == 0xc) || (op == 0xe)) /* <opcode3>{<cond>}{S} <Rd>, <Rn>, <shifter_operand> */
	{
		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    %s%s%s r%i, r%i, %s",
				 address, opcode, mnemonic, COND(opcode),
				 (S) ? "S" : "", Rd, Rn, shifter_operand);
	}
	else if ((op == 0xd) || (op == 0xf)) /* <opcode1>{<cond>}{S} <Rd>, <shifter_operand> */
	{
		if (opcode == 0xe1a00000) /* print MOV r0,r0 as NOP */
			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    NOP",address, opcode);
		else
			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    %s%s%s r%i, %s",
				 address, opcode, mnemonic, COND(opcode),
				 (S) ? "S" : "", Rd, shifter_operand);
	}
	else /* <opcode2>{<cond>} <Rn>, <shifter_operand> */
	{
		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    %s%s r%i, %s",
				 address, opcode, mnemonic, COND(opcode),
				 Rn, shifter_operand);
	}

	return 0;
}

static s32_t arm_evaluate_opcode(u32_t opcode, u32_t address, struct arm_instruction * instruction)
{
	/* clear fields, to avoid confusion */
	memset(instruction, 0, sizeof(struct arm_instruction));
	instruction->opcode = opcode;
	instruction->instruction_size = 4;

	/* catch opcodes with condition field [31:28] = b1111 */
	if ((opcode & 0xf0000000) == 0xf0000000)
	{
		/* Undefined instruction (or ARMv5E cache preload PLD) */
		if ((opcode & 0x08000000) == 0x00000000)
			return evaluate_pld(opcode, address, instruction);

		/* Undefined instruction (or ARMv6+ SRS/RFE) */
		if ((opcode & 0x0e000000) == 0x08000000)
			return evaluate_srs(opcode, address, instruction);

		/* Branch and branch with link and change to Thumb */
		if ((opcode & 0x0e000000) == 0x0a000000)
			return evaluate_blx_imm(opcode, address, instruction);

		/* Extended coprocessor opcode space (ARMv5 and higher)*/
		/* Coprocessor load/store and double register transfers */
		if ((opcode & 0x0e000000) == 0x0c000000)
			return evaluate_ldc_stc_mcrr_mrrc(opcode, address, instruction);

		/* Coprocessor data processing */
		if ((opcode & 0x0f000100) == 0x0c000000)
			return evaluate_cdp_mcr_mrc(opcode, address, instruction);

		/* Coprocessor register transfers */
		if ((opcode & 0x0f000010) == 0x0c000010)
			return evaluate_cdp_mcr_mrc(opcode, address, instruction);

		/* Undefined instruction */
		if ((opcode & 0x0f000000) == 0x0f000000)
		{
			instruction->type = ARM_UNDEFINED_INSTRUCTION;
			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    UNDEFINED INSTRUCTION", address, opcode);
			return 0;
		}
	}

	/* catch opcodes with [27:25] = b000 */
	if ((opcode & 0x0e000000) == 0x00000000)
	{
		/* Multiplies, extra load/stores */
		if ((opcode & 0x00000090) == 0x00000090)
			return evaluate_mul_and_extra_ld_st(opcode, address, instruction);

		/* Miscellaneous instructions */
		if ((opcode & 0x0f900000) == 0x01000000)
			return evaluate_misc_instr(opcode, address, instruction);

		return evaluate_data_proc(opcode, address, instruction);
	}

	/* catch opcodes with [27:25] = b001 */
	if ((opcode & 0x0e000000) == 0x02000000)
	{
		/* Undefined instruction */
		if ((opcode & 0x0fb00000) == 0x03000000)
		{
			instruction->type = ARM_UNDEFINED_INSTRUCTION;
			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    UNDEFINED INSTRUCTION", address, opcode);
			return 0;
		}

		/* Move immediate to status register */
		if ((opcode & 0x0fb00000) == 0x03200000)
			return evaluate_mrs_msr(opcode, address, instruction);

		return evaluate_data_proc(opcode, address, instruction);

	}

	/* catch opcodes with [27:25] = b010 */
	if ((opcode & 0x0e000000) == 0x04000000)
	{
		/* Load/store immediate offset */
		return evaluate_load_store(opcode, address, instruction);
	}

	/* catch opcodes with [27:25] = b011 */
	if ((opcode & 0x0e000000) == 0x06000000)
	{
		/* Load/store register offset */
		if ((opcode & 0x00000010) == 0x00000000)
			return evaluate_load_store(opcode, address, instruction);

		/* Architecturally Undefined instruction
		 * ... don't expect these to ever be used
		 */
		if ((opcode & 0x07f000f0) == 0x07f000f0)
		{
			instruction->type = ARM_UNDEFINED_INSTRUCTION;
			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%08lx    UNDEFINED INSTRUCTION", address, opcode);
			return 0;
		}

		/* "media" instructions */
		return evaluate_media(opcode, address, instruction);
	}

	/* catch opcodes with [27:25] = b100 */
	if ((opcode & 0x0e000000) == 0x08000000)
	{
		/* Load/store multiple */
		return evaluate_ldm_stm(opcode, address, instruction);
	}

	/* catch opcodes with [27:25] = b101 */
	if ((opcode & 0x0e000000) == 0x0a000000)
	{
		/* Branch and branch with link */
		return evaluate_b_bl(opcode, address, instruction);
	}

	/* catch opcodes with [27:25] = b110 */
	if ((opcode & 0x0e000000) == 0x0c000000)
	{
		/* Coprocessor load/store and double register transfers */
		return evaluate_ldc_stc_mcrr_mrrc(opcode, address, instruction);
	}

	/* catch opcodes with [27:25] = b111 */
	if ((opcode & 0x0e000000) == 0x0e000000)
	{
		/* Software interrupt */
		if ((opcode & 0x0f000000) == 0x0f000000)
			return evaluate_swi(opcode, address, instruction);

		/* Coprocessor data processing */
		if ((opcode & 0x0f000010) == 0x0e000000)
			return evaluate_cdp_mcr_mrc(opcode, address, instruction);

		/* Coprocessor register transfers */
		if ((opcode & 0x0f000010) == 0x0e000010)
			return evaluate_cdp_mcr_mrc(opcode, address, instruction);
	}

	return -1;
}

static s32_t evaluate_b_bl_blx_thumb(u16_t opcode, u32_t address, struct arm_instruction * instruction)
{
	u32_t offset = opcode & 0x7ff;
	u32_t opc = (opcode >> 11) & 0x3;
	u32_t target_address;
	char * mnemonic = NULL;

	/* sign extend 11-bit offset */
	if (((opc == 0) || (opc == 2)) && (offset & 0x00000400))
		offset = 0xfffff800 | offset;

	target_address = address + 4 + (offset << 1);

	switch (opc)
	{
		/* unconditional branch */
		case 0:
			instruction->type = ARM_B;
			mnemonic = "B";
			break;
		/* BLX suffix */
		case 1:
			instruction->type = ARM_BLX;
			mnemonic = "BLX";
			target_address &= 0xfffffffc;
			break;
		/* BL/BLX prefix */
		case 2:
			instruction->type = ARM_UNKNOWN_INSTUCTION;
			mnemonic = "prefix";
			target_address = offset << 12;
			break;
		/* BL suffix */
		case 3:
			instruction->type = ARM_BL;
			mnemonic = "BL";
			break;
	}

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    %s %#08lx",
			address, opcode, mnemonic, target_address);

	instruction->info.b_bl_bx_blx.reg_operand = -1;
	instruction->info.b_bl_bx_blx.target_address = target_address;

	return 0;
}

static s32_t evaluate_add_sub_thumb(u16_t opcode, u32_t address, struct arm_instruction * instruction)
{
	u8_t Rd = (opcode >> 0) & 0x7;
	u8_t Rn = (opcode >> 3) & 0x7;
	u8_t Rm_imm = (opcode >> 6) & 0x7;
	u32_t opc = opcode & (1 << 9);
	u32_t reg_imm  = opcode & (1 << 10);
	char *mnemonic;

	if (opc)
	{
		instruction->type = ARM_SUB;
		mnemonic = "SUBS";
	}
	else
	{
		/* REVISIT:  if reg_imm == 0, display as "MOVS" */
		instruction->type = ARM_ADD;
		mnemonic = "ADDS";
	}

	instruction->info.data_proc.Rd = Rd;
	instruction->info.data_proc.Rn = Rn;
	instruction->info.data_proc.S = 1;

	if (reg_imm)
	{
		instruction->info.data_proc.variant = 0; /*immediate*/
		instruction->info.data_proc.shifter_operand.immediate.immediate = Rm_imm;
		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    %s r%i, r%i, #%d",
			address, opcode, mnemonic, Rd, Rn, Rm_imm);
	}
	else
	{
		instruction->info.data_proc.variant = 1; /*immediate shift*/
		instruction->info.data_proc.shifter_operand.immediate_shift.Rm = Rm_imm;
		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    %s r%i, r%i, r%i",
			address, opcode, mnemonic, Rd, Rn, Rm_imm);
	}

	return 0;
}

static s32_t evaluate_shift_imm_thumb(u16_t opcode, u32_t address, struct arm_instruction * instruction)
{
	u8_t Rd = (opcode >> 0) & 0x7;
	u8_t Rm = (opcode >> 3) & 0x7;
	u8_t imm = (opcode >> 6) & 0x1f;
	u8_t opc = (opcode >> 11) & 0x3;
	char *mnemonic = NULL;

	switch (opc)
	{
		case 0:
			instruction->type = ARM_MOV;
			mnemonic = "LSLS";
			instruction->info.data_proc.shifter_operand.immediate_shift.shift = 0;
			break;
		case 1:
			instruction->type = ARM_MOV;
			mnemonic = "LSRS";
			instruction->info.data_proc.shifter_operand.immediate_shift.shift = 1;
			break;
		case 2:
			instruction->type = ARM_MOV;
			mnemonic = "ASRS";
			instruction->info.data_proc.shifter_operand.immediate_shift.shift = 2;
			break;
	}

	if ((imm == 0) && (opc != 0))
		imm = 32;

	instruction->info.data_proc.Rd = Rd;
	instruction->info.data_proc.Rn = -1;
	instruction->info.data_proc.S = 1;

	instruction->info.data_proc.variant = 1; /*immediate_shift*/
	instruction->info.data_proc.shifter_operand.immediate_shift.Rm = Rm;
	instruction->info.data_proc.shifter_operand.immediate_shift.shift_imm = imm;

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    %s r%i, r%i, #%#2.2x" ,
			address, opcode, mnemonic, Rd, Rm, imm);

	return 0;
}

static s32_t evaluate_data_proc_imm_thumb(u16_t opcode,	u32_t address, struct arm_instruction * instruction)
{
	u8_t imm = opcode & 0xff;
	u8_t Rd = (opcode >> 8) & 0x7;
	u32_t opc = (opcode >> 11) & 0x3;
	char * mnemonic = NULL;

	instruction->info.data_proc.Rd = Rd;
	instruction->info.data_proc.Rn = Rd;
	instruction->info.data_proc.S = 1;
	instruction->info.data_proc.variant = 0; /*immediate*/
	instruction->info.data_proc.shifter_operand.immediate.immediate = imm;

	switch (opc)
	{
		case 0:
			instruction->type = ARM_MOV;
			mnemonic = "MOVS";
			instruction->info.data_proc.Rn = -1;
			break;
		case 1:
			instruction->type = ARM_CMP;
			mnemonic = "CMP";
			instruction->info.data_proc.Rd = -1;
			break;
		case 2:
			instruction->type = ARM_ADD;
			mnemonic = "ADDS";
			break;
		case 3:
			instruction->type = ARM_SUB;
			mnemonic = "SUBS";
			break;
	}

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    %s r%i, #%#2.2x",
			address, opcode, mnemonic, Rd, imm);

	return 0;
}

static s32_t evaluate_data_proc_thumb(u16_t opcode,	u32_t address, struct arm_instruction * instruction)
{
	u8_t high_reg, op, Rm, Rd,H1,H2;
	char *mnemonic = NULL;
	bool_t nop = FALSE;

	high_reg = (opcode & 0x0400) >> 10;
	op = (opcode & 0x03C0) >> 6;

	Rd = (opcode & 0x0007);
	Rm = (opcode & 0x0038) >> 3;
	H1 = (opcode & 0x0080) >> 7;
	H2 = (opcode & 0x0040) >> 6;

	instruction->info.data_proc.Rd = Rd;
	instruction->info.data_proc.Rn = Rd;
	instruction->info.data_proc.S = (!high_reg || (instruction->type == ARM_CMP));
	instruction->info.data_proc.variant = 1;
	instruction->info.data_proc.shifter_operand.immediate_shift.Rm = Rm;

	if (high_reg)
	{
		Rd |= H1 << 3;
		Rm |= H2 << 3;
		op >>= 2;

		switch (op)
		{
			case 0x0:
				instruction->type = ARM_ADD;
				mnemonic = "ADD";
				break;
			case 0x1:
				instruction->type = ARM_CMP;
				mnemonic = "CMP";
				break;
			case 0x2:
				instruction->type = ARM_MOV;
				mnemonic = "MOV";
				if (Rd == Rm)
					nop = TRUE;
				break;
			case 0x3:
				if ((opcode & 0x7) == 0x0)
				{
					instruction->info.b_bl_bx_blx.reg_operand = Rm;
					if (H1)
					{
						instruction->type = ARM_BLX;
						snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    BLX r%i",
							address, opcode, Rm);
					}
					else
					{
						instruction->type = ARM_BX;
						snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    BX r%i",
							address, opcode, Rm);
					}
				}
				else
				{
					instruction->type = ARM_UNDEFINED_INSTRUCTION;
					snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    UNDEFINED INSTRUCTION",
						address, opcode);
				}
				return 0;
				break;
		}
	}
	else
	{
		switch (op)
		{
			case 0x0:
				instruction->type = ARM_AND;
				mnemonic = "ANDS";
				break;
			case 0x1:
				instruction->type = ARM_EOR;
				mnemonic = "EORS";
				break;
			case 0x2:
				instruction->type = ARM_MOV;
				mnemonic = "LSLS";
				instruction->info.data_proc.variant = 2 /*register shift*/;
				instruction->info.data_proc.shifter_operand.register_shift.shift = 0;
				instruction->info.data_proc.shifter_operand.register_shift.Rm = Rd;
				instruction->info.data_proc.shifter_operand.register_shift.Rs = Rm;
				break;
			case 0x3:
				instruction->type = ARM_MOV;
				mnemonic = "LSRS";
				instruction->info.data_proc.variant = 2 /*register shift*/;
				instruction->info.data_proc.shifter_operand.register_shift.shift = 1;
				instruction->info.data_proc.shifter_operand.register_shift.Rm = Rd;
				instruction->info.data_proc.shifter_operand.register_shift.Rs = Rm;
				break;
			case 0x4:
				instruction->type = ARM_MOV;
				mnemonic = "ASRS";
				instruction->info.data_proc.variant = 2 /*register shift*/;
				instruction->info.data_proc.shifter_operand.register_shift.shift = 2;
				instruction->info.data_proc.shifter_operand.register_shift.Rm = Rd;
				instruction->info.data_proc.shifter_operand.register_shift.Rs = Rm;
				break;
			case 0x5:
				instruction->type = ARM_ADC;
				mnemonic = "ADCS";
				break;
			case 0x6:
				instruction->type = ARM_SBC;
				mnemonic = "SBCS";
				break;
			case 0x7:
				instruction->type = ARM_MOV;
				mnemonic = "RORS";
				instruction->info.data_proc.variant = 2 /*register shift*/;
				instruction->info.data_proc.shifter_operand.register_shift.shift = 3;
				instruction->info.data_proc.shifter_operand.register_shift.Rm = Rd;
				instruction->info.data_proc.shifter_operand.register_shift.Rs = Rm;
				break;
			case 0x8:
				instruction->type = ARM_TST;
				mnemonic = "TST";
				break;
			case 0x9:
				instruction->type = ARM_RSB;
				mnemonic = "RSBS";
				instruction->info.data_proc.variant = 0 /*immediate*/;
				instruction->info.data_proc.shifter_operand.immediate.immediate = 0;
				instruction->info.data_proc.Rn = Rm;
				break;
			case 0xA:
				instruction->type = ARM_CMP;
				mnemonic = "CMP";
				break;
			case 0xB:
				instruction->type = ARM_CMN;
				mnemonic = "CMN";
				break;
			case 0xC:
				instruction->type = ARM_ORR;
				mnemonic = "ORRS";
				break;
			case 0xD:
				instruction->type = ARM_MUL;
				mnemonic = "MULS";
				break;
			case 0xE:
				instruction->type = ARM_BIC;
				mnemonic = "BICS";
				break;
			case 0xF:
				instruction->type = ARM_MVN;
				mnemonic = "MVNS";
				break;
		}
	}

	if (nop)
		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    NOP ; (%s r%i, r%i)",
				 address, opcode, mnemonic, Rd, Rm);
	else
		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    %s r%i, r%i",
				 address, opcode, mnemonic, Rd, Rm);

	return 0;
}

/*
 * pc-relative data addressing is word-aligned even with thumb
 */
static inline u32_t thumb_alignpc4(u32_t addr)
{
	return (addr + 4) & ~3;
}

static s32_t evaluate_load_literal_thumb(u16_t opcode, u32_t address, struct arm_instruction * instruction)
{
	u32_t immediate;
	u8_t Rd = (opcode >> 8) & 0x7;

	instruction->type = ARM_LDR;
	immediate = opcode & 0x000000ff;
	immediate *= 4;

	instruction->info.load_store.Rd = Rd;
	instruction->info.load_store.Rn = 15			/* pc */;
	instruction->info.load_store.index_mode = 0;	/* offset */
	instruction->info.load_store.offset_mode = 0;	/* immediate */
	instruction->info.load_store.offset.offset = immediate;

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    LDR r%i, [pc, #%#lx] ; %#08lx",
		address, opcode, Rd, immediate,
		thumb_alignpc4(address) + immediate);

	return 0;
}

static s32_t evaluate_load_store_reg_thumb(u16_t opcode, u32_t address, struct arm_instruction * instruction)
{
	u8_t Rd = (opcode >> 0) & 0x7;
	u8_t Rn = (opcode >> 3) & 0x7;
	u8_t Rm = (opcode >> 6) & 0x7;
	u8_t opc = (opcode >> 9) & 0x7;
	char * mnemonic = NULL;

	switch (opc)
	{
		case 0:
			instruction->type = ARM_STR;
			mnemonic = "STR";
			break;
		case 1:
			instruction->type = ARM_STRH;
			mnemonic = "STRH";
			break;
		case 2:
			instruction->type = ARM_STRB;
			mnemonic = "STRB";
			break;
		case 3:
			instruction->type = ARM_LDRSB;
			mnemonic = "LDRSB";
			break;
		case 4:
			instruction->type = ARM_LDR;
			mnemonic = "LDR";
			break;
		case 5:
			instruction->type = ARM_LDRH;
			mnemonic = "LDRH";
			break;
		case 6:
			instruction->type = ARM_LDRB;
			mnemonic = "LDRB";
			break;
		case 7:
			instruction->type = ARM_LDRSH;
			mnemonic = "LDRSH";
			break;
	}

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    %s r%i, [r%i, r%i]",
			address, opcode, mnemonic, Rd, Rn, Rm);

	instruction->info.load_store.Rd = Rd;
	instruction->info.load_store.Rn = Rn;
	instruction->info.load_store.index_mode = 0;	/* offset */
	instruction->info.load_store.offset_mode = 1;	/* register */
	instruction->info.load_store.offset.reg.Rm = Rm;

	return 0;
}

static s32_t evaluate_load_store_imm_thumb(u16_t opcode, u32_t address, struct arm_instruction * instruction)
{
	u32_t offset = (opcode >> 6) & 0x1f;
	u8_t Rd = (opcode >> 0) & 0x7;
	u8_t Rn = (opcode >> 3) & 0x7;
	u32_t L = opcode & (1 << 11);
	u32_t B = opcode & (1 << 12);
	char *mnemonic;
	char suffix = ' ';
	u32_t shift = 2;

	if (L)
	{
		instruction->type = ARM_LDR;
		mnemonic = "LDR";
	}
	else
	{
		instruction->type = ARM_STR;
		mnemonic = "STR";
	}

	if ((opcode&0xF000) == 0x8000)
	{
		suffix = 'H';
		shift = 1;
	}
	else if (B)
	{
		suffix = 'B';
		shift = 0;
	}

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    %s%c r%i, [r%i, #%#lx]",
		address, opcode, mnemonic, suffix, Rd, Rn, offset << shift);

	instruction->info.load_store.Rd = Rd;
	instruction->info.load_store.Rn = Rn;
	instruction->info.load_store.index_mode = 0;	/* offset */
	instruction->info.load_store.offset_mode = 0;	/* immediate */
	instruction->info.load_store.offset.offset = offset << shift;

	return 0;
}

static s32_t evaluate_load_store_stack_thumb(u16_t opcode, u32_t address, struct arm_instruction * instruction)
{
	u32_t offset = opcode  & 0xff;
	u8_t Rd = (opcode >> 8) & 0x7;
	u32_t L = opcode & (1 << 11);
	char * mnemonic;

	if (L)
	{
		instruction->type = ARM_LDR;
		mnemonic = "LDR";
	}
	else
	{
		instruction->type = ARM_STR;
		mnemonic = "STR";
	}

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    %s r%i, [SP, #%#lx]",
		address, opcode, mnemonic, Rd, offset*4);

	instruction->info.load_store.Rd = Rd;
	instruction->info.load_store.Rn = 13			/* sp */;
	instruction->info.load_store.index_mode = 0;	/* offset */
	instruction->info.load_store.offset_mode = 0;	/* immediate */
	instruction->info.load_store.offset.offset = offset*4;

	return 0;
}

static s32_t evaluate_add_sp_pc_thumb(u16_t opcode, u32_t address, struct arm_instruction * instruction)
{
	u32_t imm = opcode  & 0xff;
	u8_t Rd = (opcode >> 8) & 0x7;
	u8_t Rn;
	u32_t SP = opcode & (1 << 11);
	char * reg_name;

	instruction->type = ARM_ADD;

	if (SP)
	{
		reg_name = "SP";
		Rn = 13;
	}
	else
	{
		reg_name = "PC";
		Rn = 15;
	}

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    ADD r%i, %s, #%#lx",
		address, opcode, Rd, reg_name, imm * 4);

	instruction->info.data_proc.variant = 0 /* immediate */;
	instruction->info.data_proc.Rd = Rd;
	instruction->info.data_proc.Rn = Rn;
	instruction->info.data_proc.shifter_operand.immediate.immediate = imm*4;

	return 0;
}

static s32_t evaluate_adjust_stack_thumb(u16_t opcode, u32_t address, struct arm_instruction * instruction)
{
	u32_t imm = opcode  & 0x7f;
	u8_t opc = opcode & (1 << 7);
	char *mnemonic;


	if (opc)
	{
		instruction->type = ARM_SUB;
		mnemonic = "SUB";
	}
	else
	{
		instruction->type = ARM_ADD;
		mnemonic = "ADD";
	}

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    %s SP, #%#lx",
			address, opcode, mnemonic, imm*4);

	instruction->info.data_proc.variant = 0;	/* immediate */
	instruction->info.data_proc.Rd = 13;		/* sp */
	instruction->info.data_proc.Rn = 13;		/* sp */
	instruction->info.data_proc.shifter_operand.immediate.immediate = imm*4;

	return 0;
}

static s32_t evaluate_breakpoint_thumb(u16_t opcode, u32_t address, struct arm_instruction * instruction)
{
	u32_t imm = opcode  & 0xff;

	instruction->type = ARM_BKPT;

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    BKPT %#02lx",
			address, opcode, imm);

	return 0;
}

static s32_t evaluate_load_store_multiple_thumb(u16_t opcode, u32_t address, struct arm_instruction * instruction)
{
	u32_t reg_list = opcode  & 0xff;
	u32_t L = opcode & (1 << 11);
	u32_t R = opcode & (1 << 8);
	u8_t Rn = (opcode >> 8) & 7;
	u8_t addr_mode = 0 /* IA */;
	char reg_names[40];
	char *reg_names_p;
	char *mnemonic;
	char ptr_name[7] = "";
	s32_t i;

	/*
	 * in ThumbEE mode, there are no LDM or STM instructions.
	 * The STMIA and LDMIA opcodes are used for other instructions.
	 */
	if ((opcode & 0xf000) == 0xc000)
	{
		/* generic load/store multiple */
		char *wback = "!";

		if (L)
		{
			instruction->type = ARM_LDM;
			mnemonic = "LDM";
			if (opcode & (1 << Rn))
				wback = "";
		}
		else
		{
			instruction->type = ARM_STM;
			mnemonic = "STM";
		}
		snprintf(ptr_name, sizeof ptr_name, (const char *)"r%i%s, ", Rn, wback);
	}
	else
	{
		/* push / pop */
		Rn = 13;
		if (L)
		{
			instruction->type = ARM_LDM;
			mnemonic = "POP";
			if (R)
				reg_list |= (1 << 15);
		}
		else
		{
			instruction->type = ARM_STM;
			mnemonic = "PUSH";
			addr_mode = 3;
			if (R)
				reg_list |= (1 << 14);
		}
	}

	reg_names_p = reg_names;
	for (i = 0; i <= 15; i++)
	{
		if (reg_list & (1 << i))
			reg_names_p += snprintf(reg_names_p, (reg_names + 40 - reg_names_p), (const char *)"r%i, ", (u8_t)i);
	}
	if (reg_names_p > reg_names)
		reg_names_p[-2] = '\0';
	else
		reg_names[0] = '\0';

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    %s %s{%s}",
			address, opcode, mnemonic, ptr_name, reg_names);

	instruction->info.load_store_multiple.register_list = reg_list;
	instruction->info.load_store_multiple.Rn = Rn;
	instruction->info.load_store_multiple.addressing_mode = addr_mode;

	return 0;
}

static s32_t evaluate_cond_branch_thumb(u16_t opcode, u32_t address, struct arm_instruction * instruction)
{
	u32_t offset = opcode  & 0xff;
	u8_t cond = (opcode >> 8) & 0xf;
	u32_t target_address;

	if (cond == 0xf)
	{
		instruction->type = ARM_SWI;
		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    SVC %#02lx",
				address, opcode, offset);
		return 0;
	}
	else if (cond == 0xe)
	{
		instruction->type = ARM_UNDEFINED_INSTRUCTION;
		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    UNDEFINED INSTRUCTION",
			address, opcode);
		return 0;
	}

	/* sign extend 8-bit offset */
	if (offset & 0x00000080)
		offset = 0xffffff00 | offset;

	target_address = address + 4 + (offset << 1);

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    B%s %#08lx",
			address, opcode, arm_condition_strings[cond], target_address);

	instruction->type = ARM_B;
	instruction->info.b_bl_bx_blx.reg_operand = -1;
	instruction->info.b_bl_bx_blx.target_address = target_address;

	return 0;
}

static s32_t evaluate_cb_thumb(u16_t opcode, u32_t address,	struct arm_instruction * instruction)
{
	u32_t offset;

	/* added in Thumb2 */
	offset = (opcode >> 3) & 0x1f;
	offset |= (opcode & 0x0200) >> 4;

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    CB%sZ r%d, %#08lx",
			address, opcode, (opcode & 0x0800) ? "N" : "",
			opcode & 0x7, address + 4 + (offset << 1));

	return 0;
}

static s32_t evaluate_extend_thumb(u16_t opcode, u32_t address,	struct arm_instruction * instruction)
{
	/* added in ARMv6 */
	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    %cXT%c r%d, r%d",
			address, opcode,
			(opcode & 0x0080) ? 'U' : 'S',
			(opcode & 0x0040) ? 'B' : 'H',
			opcode & 0x7, (opcode >> 3) & 0x7);

	return 0;
}

static s32_t evaluate_cps_thumb(u16_t opcode, u32_t address, struct arm_instruction * instruction)
{
	/* added in ARMv6 */
	if ((opcode & 0x0ff0) == 0x0650)
		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    SETEND %s",
				address, opcode,
				(opcode & 0x80) ? "BE" : "LE");
	/* ASSUME (opcode & 0x0fe0) == 0x0660 */
	else
		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    CPSI%c %s%s%s",
				address, opcode,
				(opcode & 0x0010) ? 'D' : 'E',
				(opcode & 0x0004) ? "A" : "",
				(opcode & 0x0002) ? "I" : "",
				(opcode & 0x0001) ? "F" : "");

	return 0;
}

static s32_t evaluate_byterev_thumb(u16_t opcode, u32_t address, struct arm_instruction * instruction)
{
	char *suffix;

	/* added in ARMv6 */
	switch ((opcode >> 6) & 3)
	{
	case 0:
		suffix = "";
		break;
	case 1:
		suffix = "16";
		break;
	default:
		suffix = "SH";
		break;
	}
	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    REV%s r%d, r%d",
			address, opcode, suffix,
			opcode & 0x7, (opcode >> 3) & 0x7);

	return 0;
}

static s32_t evaluate_hint_thumb(u16_t opcode, u32_t address, struct arm_instruction * instruction)
{
	char *hint;

	switch ((opcode >> 4) & 0x0f)
	{
	case 0:
		hint = "NOP";
		break;
	case 1:
		hint = "YIELD";
		break;
	case 2:
		hint = "WFE";
		break;
	case 3:
		hint = "WFI";
		break;
	case 4:
		hint = "SEV";
		break;
	default:
		hint = "HINT (UNRECOGNIZED)";
		break;
	}

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    %s",
			address, opcode, hint);

	return 0;
}

static s32_t evaluate_ifthen_thumb(u16_t opcode, u32_t address,	struct arm_instruction * instruction)
{
	u32_t cond = (opcode >> 4) & 0x0f;
	char *x = "", *y = "", *z = "";

	if (opcode & 0x01)
		z = (opcode & 0x02) ? "T" : "E";
	if (opcode & 0x03)
		y = (opcode & 0x04) ? "T" : "E";
	if (opcode & 0x07)
		x = (opcode & 0x08) ? "T" : "E";

	snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    IT%s%s%s %s",
			address, opcode,
			x, y, z, arm_condition_strings[cond]);

	/*
	 * NOTE:  strictly speaking, the next 1-4 instructions should
	 * now be displayed with the relevant conditional suffix...
	 */
	return 0;
}

static s32_t thumb_evaluate_opcode(u16_t opcode, u32_t address, struct arm_instruction * instruction)
{
	/*
	 * clear fields, to avoid confusion
	 */
	memset(instruction, 0, sizeof(struct arm_instruction));
	instruction->opcode = opcode;
	instruction->instruction_size = 2;

	if ((opcode & 0xe000) == 0x0000)
	{
		/* add/substract register or immediate */
		if ((opcode & 0x1800) == 0x1800)
			return evaluate_add_sub_thumb(opcode, address, instruction);
		/* shift by immediate */
		else
			return evaluate_shift_imm_thumb(opcode, address, instruction);
	}

	/* Add/substract/compare/move immediate */
	if ((opcode & 0xe000) == 0x2000)
	{
		return evaluate_data_proc_imm_thumb(opcode, address, instruction);
	}

	/* Data processing instructions */
	if ((opcode & 0xf800) == 0x4000)
	{
		return evaluate_data_proc_thumb(opcode, address, instruction);
	}

	/* Load from literal pool */
	if ((opcode & 0xf800) == 0x4800)
	{
		return evaluate_load_literal_thumb(opcode, address, instruction);
	}

	/* Load/Store register offset */
	if ((opcode & 0xf000) == 0x5000)
	{
		return evaluate_load_store_reg_thumb(opcode, address, instruction);
	}

	/* Load/Store immediate offset */
	if (((opcode & 0xe000) == 0x6000)
		||((opcode & 0xf000) == 0x8000))
	{
		return evaluate_load_store_imm_thumb(opcode, address, instruction);
	}

	/* Load/Store from/to stack */
	if ((opcode & 0xf000) == 0x9000)
	{
		return evaluate_load_store_stack_thumb(opcode, address, instruction);
	}

	/* Add to SP/PC */
	if ((opcode & 0xf000) == 0xa000)
	{
		return evaluate_add_sp_pc_thumb(opcode, address, instruction);
	}

	/* Misc */
	if ((opcode & 0xf000) == 0xb000)
	{
		switch ((opcode >> 8) & 0x0f)
		{
		case 0x0:
			return evaluate_adjust_stack_thumb(opcode, address, instruction);
		case 0x1:
		case 0x3:
		case 0x9:
		case 0xb:
			return evaluate_cb_thumb(opcode, address, instruction);
		case 0x2:
			return evaluate_extend_thumb(opcode, address, instruction);
		case 0x4:
		case 0x5:
		case 0xc:
		case 0xd:
			return evaluate_load_store_multiple_thumb(opcode, address,
						instruction);
		case 0x6:
			return evaluate_cps_thumb(opcode, address, instruction);
		case 0xa:
			if ((opcode & 0x00c0) == 0x0080)
				break;
			return evaluate_byterev_thumb(opcode, address, instruction);
		case 0xe:
			return evaluate_breakpoint_thumb(opcode, address, instruction);
		case 0xf:
			if (opcode & 0x000f)
				return evaluate_ifthen_thumb(opcode, address,
						instruction);
			else
				return evaluate_hint_thumb(opcode, address,
						instruction);
		}

		instruction->type = ARM_UNDEFINED_INSTRUCTION;
		snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    UNDEFINED INSTRUCTION", address, opcode);
		return 0;
	}

	/* Load/Store multiple */
	if ((opcode & 0xf000) == 0xc000)
	{
		return evaluate_load_store_multiple_thumb(opcode, address, instruction);
	}

	/* Conditional branch + SWI */
	if ((opcode & 0xf000) == 0xd000)
	{
		return evaluate_cond_branch_thumb(opcode, address, instruction);
	}

	if ((opcode & 0xe000) == 0xe000)
	{
		/* Undefined instructions */
		if ((opcode & 0xf801) == 0xe801)
		{
			instruction->type = ARM_UNDEFINED_INSTRUCTION;
			snprintf(instruction->text, 128, (const char *)"0x%08lx 0x%04x    UNDEFINED INSTRUCTION", address, opcode);
			return 0;
		}
		/* Branch to offset */
		else
		{
			return evaluate_b_bl_blx_thumb(opcode, address, instruction);
		}
	}

	return -1;
}

static int dasm(int argc, char ** argv)
{
	bool_t thumb = FALSE;
	u32_t address = 0x00000000;
	u32_t count = 1;
	char * filename = NULL;
	struct arm_instruction instruction;
	int fd = 0;
	char buf[256];
	s32_t len, i;

	if(argc < 2)
	{
		printk("usage:\r\n    dasm <address> [-a|-t] [-c count] [-f file]\r\n");
		return (-1);
	}

	address = strtoul((const char *)argv[1], NULL, 0);

	for(i=2; i<argc; i++)
	{
		if( !strcmp((const char *)argv[i], "-a") )
			thumb = FALSE;
		else if( !strcmp((const char *)argv[i], "-t") )
			thumb = TRUE;
		else if( !strcmp((const char *)argv[i], "-c") && (argc > i+1))
		{
			count = strtoul((const char *)argv[i+1], NULL, 0);
			i++;
		}
		else if( !strcmp((const char *)argv[i], "-f") && (argc > i+1))
		{
			filename = (char *)argv[i+1];
			i++;
		}
		else
		{
			printk("dasm: invalid option '%s'\r\n", argv[i]);
			printk("usage:\r\n    dasm [-a|-t] <address> [-c count] [-f file]\r\n");
			printk("try 'help dasm' for more information.\r\n");
			return (-1);
		}
	}

	if(filename != NULL)
	{
		fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH));
		if(fd < 0)
		{
			printk("can not to create file '%s'\r\n", filename);
			return -1;
		}
	}

	while(count--)
	{
		if(thumb == TRUE)
			thumb_evaluate_opcode(readw(address), address, &instruction);
		else
			arm_evaluate_opcode(readl(address), address, &instruction);

		len = sprintf(buf, (const char *)"%s\r\n", instruction.text);

		if(filename)
		{
			if( write(fd, (void *)buf, len) != len )
			{
				close(fd);
				unlink(filename);
				printk("failed to write file '%s'\r\n", filename);
			}
		}
		else
		{
			printk((const char *)buf);
		}

		address += instruction.instruction_size;
	}

	if(filename != NULL)
		close(fd);

	return 0;
}

static struct command dasm_cmd = {
	.name		= "dasm",
	.func		= dasm,
	.desc		= "arm instruction disassembler\r\n",
	.usage		= "dasm <address> [-a|-t] [-c count] [-f file]\r\n",
	.help		= "    disassemble tool for arm instruction\r\n"
				  "    -a    disassemble with arm instruction (default for arm)\r\n"
				  "    -t    disassemble with thumb instruction\r\n"
				  "    -c    the count of instruction (default is one instruction)\r\n"
				  "    -f    save to file with result\r\n"
};

static __init void dasm_cmd_init(void)
{
	if(!command_register(&dasm_cmd))
		LOG_E("register 'dasm' command fail");
}

static __exit void dasm_cmd_exit(void)
{
	if(!command_unregister(&dasm_cmd))
		LOG_E("unregister 'dasm' command fail");
}

command_initcall(dasm_cmd_init);
command_exitcall(dasm_cmd_exit);

#endif
