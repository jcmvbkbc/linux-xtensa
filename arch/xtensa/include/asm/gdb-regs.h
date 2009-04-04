/*
 * arch/xtensa/include/asm/gdb-regs.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 - 2009 Pete Delaney, Tensilica Inc.
 */

#ifndef _XTENSA_GDB_REGS_H
#define _XTENSA_GDB_REGS_H

/*
 * Gdb offsets for gdb are defined in Software/libdb/xtensa-libdb-macros.h
 * Preliminary effort; actual structure is very overlay dependant.
 */

struct xtensa_gdb_registers {
#define XTENSA_DBREGN_A(n) (0x0000+(n))		/* address registers a0..a15 */
	unsigned long a0;
	union {
	  unsigned long a1;
	  unsigned long sp;
	};
	unsigned long a2;
	unsigned long a3;
	unsigned long a4;
	unsigned long a5;
	unsigned long a6;
	unsigned long a7;
	unsigned long a8;
	unsigned long a9;
	unsigned long a10;
	unsigned long a11;
	unsigned long a12;
	unsigned long a13;
	unsigned long a14;
	unsigned long a15;

#define XTENSA_DBREGN_B(n) (0x0010+(n))		/* boolean bits b0..b15 */	
	unsigned b0:1;			
	unsigned b1:1;
	unsigned b2:1;
	unsigned b3:1;
	unsigned b4:1;
	unsigned b5:1;
	unsigned b6:1;
	unsigned b7:1;
	unsigned b8:1;
	unsigned b9:1;
	unsigned b10:1;
	unsigned b11:1;
	unsigned b12:1;
	unsigned b13:1;
	unsigned b14:1;
	unsigned b15:1;

#define XTENSA_DBREGN_PC 0x0020         	/* program counter */
	unsigned long pc;			
	unsigned long reserved[0x100-0x21];

#define XTENSA_DBREGN_AR(n) (0x0100+(n))	/* Addr regs ar0..ar63 */
	unsigned long ar0;
	unsigned long ar1;
	unsigned long ar2;
	unsigned long ar3;
	unsigned long ar4;
	unsigned long ar5;
	unsigned long ar6;
	unsigned long ar7;
	unsigned long ar8;
	unsigned long ar9;
	unsigned long ar10;
	unsigned long ar11;
	unsigned long ar12;
	unsigned long ar13;
	unsigned long ar14;
	unsigned long ar15;
	unsigned long ar16;
	unsigned long ar17;
	unsigned long ar18;
	unsigned long ar19;
	unsigned long ar20;
	unsigned long ar21;
	unsigned long ar22;
	unsigned long ar23;
	unsigned long ar24;
	unsigned long ar25;
	unsigned long ar26;
	unsigned long ar27;
	unsigned long ar28;
	unsigned long ar29;
	unsigned long ar30;
	unsigned long ar31;
	unsigned long ar32;
	unsigned long ar33;
	unsigned long ar34;
	unsigned long ar35;
	unsigned long ar36;
	unsigned long ar37;
	unsigned long ar38;
	unsigned long ar39;
	unsigned long ar40;
	unsigned long ar41;
	unsigned long ar42;
	unsigned long ar43;
	unsigned long ar44;
	unsigned long ar45;
	unsigned long ar46;
	unsigned long ar47;
	unsigned long ar48;
	unsigned long ar49;
	unsigned long ar50;
	unsigned long ar51;
	unsigned long ar52;
	unsigned long ar53;
	unsigned long ar54;
	unsigned long ar55;
	unsigned long ar56;
	unsigned long ar57;
	unsigned long ar58;
	unsigned long ar59;
	unsigned long ar60;
	unsigned long ar61;

#define XTENSA_DBREGN_SREG(n) (0x0200+(n))    	/* special registers 0..255 */
	unsigned long lbeg;			/* LBEG:0 */
	unsigned long lend;			/* LEND:1 */
	unsigned long lcount;			/* LCOUNT:2 */
	unsigned long sar;			/* SAR:3 */
	unsigned long br;			/* BR:4 */
	unsigned long litbase;			/* LITBASE:5 */
	unsigned long fill_0[(11-6)+1];		/* 6 ... 11 */
	unsigned long scompare1;		/* SCOMPARE1:12 */
	unsigned long scompare2;		/* SCOMPARE2:13 */
	unsigned long scompare3;		/* SCOMPARE3:14 */
	unsigned long scompare4;		/* SCOMPARE4:15 */
	unsigned long acchi;			/* ACCHI:16 */
	unsigned long acclo;			/* ACCLO:17 */
	unsigned long fill_1[(31-18)+1];	/* 18 ... 31 */
	unsigned long mr;			/* MR:32 */
	unsigned long fill_2[(71-33)+1];	/* 33 ... 71 */
	unsigned long windowbase;		/* WINDOWBASE:72 */
	unsigned long windowstart;		/* WINDOWSTART:73 */
	unsigned long fill_3[(82-74)+1];	/* 74 ... 82 */
	unsigned long ptevaddr;			/* PTEVADDR:83 */
	unsigned long fill_4[(88-84)+1];	/* 84 ... 88 */
	unsigned long mmid;			/* MMID:89 */
	unsigned long rasid;			/* RASID:90 */
	unsigned long itlbcfg;			/* ITLBCFG:91 */
	unsigned long dtlbcfg;			/* DTLBCFG:92 */
	unsigned long fill_5[(95-93)+1];	/* 93, 94, 95 */
	unsigned long ibreakenable;		/* IBREAKENABLE:96 */
	unsigned long fill_6;			/* 97 */
	unsigned long cacheattr;		/* CACHEATTR:98 */
	unsigned long fill_7[(103-99)+1];       /* 99 ... 103  */
	unsigned long ddr;			/* DDR:104 */
	unsigned long ibreaka0;			/* IBREAKA:128 */
	unsigned long ibreaka1;			/* IBREAKA1:129 */
	unsigned long dbreaka0;			/* DBREAKA0:144 */
	unsigned long dbreaka1;			/* DBREAKA0:145 */
	unsigned long dbreaka_fill[159-146+1];	/* 146 ... 159 */
	unsigned long dbreakc0;			/* DBREAKC:160 */
	unsigned long dbreakc1;			/* DBREAKC:161 */
	unsigned long dbreakc_fill[159-146+1];  /* 162 ... 167 */
	unsigned long epc;			/* EPC:176 */
	unsigned long epc1;			/* EPC_1:177 */
	unsigned long epc2;			/* EPC_2:178 */
	unsigned long epc3;			/* EPC_3:179 */
	unsigned long epc4;			/* EPC_4:180 */
	unsigned long epc5;			/* EPC_5:181 */
	unsigned long epc6;			/* EPC_6:182 */
	unsigned long epc7;			/* EPC_7:183 */
	unsigned long fill_8[(191-184)+1];      /* 184 ... 191  */
	unsigned long depc;			/* DEPC:EPS:192 */
	unsigned long eps1;			/* EPS_1:193 */
	unsigned long eps2;			/* EPS_2:194 */
	unsigned long eps3;			/* EPS_3:195 */
	unsigned long eps4;			/* EPS_4:196 */
	unsigned long eps5;			/* EPS_5:197 */
	unsigned long eps6;			/* EPS_6:198 */
	unsigned long eps7;			/* EPS_7:199 */
	unsigned long fill_9[(208-200)+1];      /* 200 ... 208  */
	unsigned long excsave1;			/* EXCSAVE:209 */
	unsigned long excsave2;			/* EXCSAVE:210 */
	unsigned long excsave3;			/* EXCSAVE:211 */
	unsigned long excsave4;			/* EXCSAVE:212 */
	unsigned long excsave5;			/* EXCSAVE:213 */
	unsigned long excsave6;			/* EXCSAVE:214 */
	unsigned long excsave7;			/* EXCSAVE:215 */
	unsigned long fill_10[(223-216)+1];     /* 216 ... 223  */
	unsigned long cpenable;			/* CPENABLE:224 */
	unsigned long fill_11;			/* 225 */
	union {
	  unsigned long interrupt;		/* INTERRUPT:226 */
	  unsigned long intset;			/* INTSET:226 */
	};
	unsigned long intclear;			/* INTCLEAR:227 */
	unsigned long intenable;		/* INTENABLE:228 */
	unsigned long fill_12;			/* 229 */
	unsigned long ps;			/* PS:230 */
	union {
	  unsigned long vecbase;		/* VECBASE:231 */
	  unsigned long threadptr;		/* THREADPTR:231 */
	};
	unsigned long exccause;			/* EXCCAUSE:232 */
	unsigned long debugcause;		/* DEBUGCAUSE:233 */
	unsigned long ccount;			/* CCOUNT:234 */
	unsigned long prid;			/* PRID:235 */
	unsigned long icount;			/* ICOUNT:236 */
	unsigned long icountlevel;		/* ICOUNTLEVEL:237 */
	unsigned long excvaddr;			/* EXCVADDR: 238 */
	union {
	  unsigned long ccompare;		/* CCOMPARE:240 */
	  unsigned long ccompare0;		/* CCOMPARE_0:240 */
	};
	unsigned long ccompare1;		/* CCOMPARE_1:241 */
	unsigned long ccompare2;		/* CCOMPARE_2:242 */
	unsigned long fill_13;
	unsigned long misc0;			/* MISC_REG_0:244 */
	unsigned long misc1;			/* MISC_REG_1:245 */
	unsigned long misc2;			/* MISC_REG_2:246 */
	unsigned long misc3;			/* MISC_REG_3:247 */
	unsigned long fill_14[(255-248)+1];	/* 248 ... 255 */
};

#define XTENSA_GDB_REGISTERS_SIZE	((0x200+255) * sizeof(long))

#endif /* _XTENSA_GDB_REGS_H */
