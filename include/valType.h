//不要随便改动，很多包含文件都没有声明依赖性
#ifndef VALTYPE_H
#define VALTYPE_H

#define NULL 0
#define true 1
#define false 0

typedef int boolean;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;
typedef signed short s16;
typedef signed int s32;

/**
 * the following data-type is used in some special modules,we never use them in daily kernel programming.
 */
typedef unsigned __le32;
typedef unsigned __u32;
typedef unsigned short __le16;
typedef unsigned short __u16;
typedef unsigned char __u8;
typedef unsigned Elf32_Word;
typedef unsigned Elf32_Off;
typedef unsigned Elf32_Addr;
typedef unsigned short Elf32_Half;
typedef struct descriptorr{
	u16 limit_01;
	u16 base_01;
	u8 base_2;
	u8 attr1;
	u8 limit_attr;
	u8 base_3;
		
}DESCRIPTOR;
#endif
