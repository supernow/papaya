; this file defines some constants and macros such as Descriptor

pgDirBase equ 200000h
pgTblBase equ 200000h+4096

DA_32     equ 4000h ; 32 bit segment

DA_DPL0   equ   00h ; DPL = 0
DA_DPL1   equ   20h ; DPL = 1
DA_DPL2   equ   40h ; DPL = 2
DA_DPL3   equ   60h ; DPL = 3
;----------------------------------------------------------------------------
; data segment descriptor type value specification
;----------------------------------------------------------------------------
DA_DR     equ   90h ; read-only value
DA_DRW    equ   92h ; readable and writeable value
DA_DRWA   equ   93h ; readable and writeable value which has been accessed
DA_C      equ   98h ; execute-only value
DA_CR     equ   9Ah ; executable and readable 
DA_CCO    equ   9Ch ; execute-only conforming code segment attribute value
DA_CCOR   equ   9Eh ; executable and readable conforming code segment attribute value
;----------------------------------------------------------------------------
; system segment descriptor type value specification 
;----------------------------------------------------------------------------
DA_LDT      equ   82h ; local descriptor value
DA_TaskGate equ   85h ; task gate value
DA_386TSS   equ   89h ; 386 tss value
DA_386CGate equ   8Ch ; 386 call gate value
DA_386IGate equ   8Eh ; 386 interrupt gate value
DA_386TGate equ   8Fh ; 386 trap gate value
DA_LIMIT_4K equ 8000h
;----------------------------------------------------------------------------

;----------------------------------------------------------------------------
PG_P    equ  1b
PG_USU  equ  100b
PG_RWW  equ  10b
;----------------------------------------------------------------------------

; selector :
;         ┏━━┳━━┳━━┳━━┳━━┳━━┳━━┳━━┳━━┳━━┳━━┳━━┳━━┳━━┳━━┳━━┓
;         ┃15┃14┃13┃12┃11┃10┃9 ┃8 ┃7 ┃6 ┃5 ┃4 ┃3 ┃2 ┃1 ┃0 ┃
;         ┣━━┻━━┻━━┻━━┻━━┻━━┻━━┻━━┻━━┻━━┻━━┻━━┻━━╋━━╋━━┻━━┫
;         ┃         descriptor index             ┃TI┃ RPL ┃
;         ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┻━━┻━━━━━┛
;
; RPL(Requested Privilege Level): used for privilege check
;
; TI(Table Indicator): descriptor table indicator bit
; TI=0, read descriptor from GDT
; TI=1, read descriptor from LDT
;

;----------------------------------------------------------------------------
; selector type value specification
; SA_ : Selector Attribute

SA_RPL0   equ 0 ; ┓
SA_RPL1   equ 1 ; ┣ RPL
SA_RPL2   equ 2 ; ┃
SA_RPL3   equ 3 ; ┛

SA_TIG    equ 0 ; ┓TI
SA_TIL    equ 4 ; ┛
;----------------------------------------------------------------------------


; Macro ------------------------------------------------------------------------------------------------------
;
; Descriptor 
; usage: Descriptor Base, Limit, Attr
;        Base : dd
;        Limit: dd (low 20 bits available)
;        Attr : dw (lower 4 bits of higher byte are always 0)
%macro Descriptor 3
dw         %2 & 0FFFFh                 ; segment limit 1(2 bytes)
dw         %1 & 0FFFFh                 ; segment base 1 (1 byte)
db (%1 >> 16) & 0FFh                   ; segment base 2 (1 byte)
dw ((%2 >> 8) & 0F00h) | (%3 & 0F0FFh) ; attibute 1 + segment limit 2 + attribute 2  (2 bytes)
db (%1 >> 24) & 0FFh                   ; segment base 3 (1 byte)    
%endmacro ; total 8 bytes
;
; gate 
; usage: Gate Selector, Offset, DCount, Attr
;        Selector:  dw
;        Offset  :  dd
;        DCount  :  db
;        Attr    :  db
%macro Gate 4
dw (%2 & 0FFFFh)                     ; offset 1(2 bytes)
dw  %1                               ; selector(2 bytes)
dw (%3 & 1Fh) | ((%4 << 8) & 0FF00h) ; attribute(2 bytes)
dw ((%2 >> 16) & 0FFFFh)             ; offset 2(2 bytes)
%endmacro ; total 8 bytes
