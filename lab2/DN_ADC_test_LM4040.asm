$NOLIST
$MODN76E003
$LIST

CLK               EQU 16600000
BAUD              EQU 115200
TIMER1_RELOAD     EQU (0x100-(CLK/(16*BAUD)))
TIMER0_RELOAD_1MS EQU (0x10000-(CLK/1000))
TEMP_THRESHOLD    EQU 3000

ORG 0x0000
    ljmp main

cseg
LCD_RS equ P1.3
LCD_E  equ P1.4
LCD_D4 equ P0.0
LCD_D5 equ P0.1
LCD_D6 equ P0.2
LCD_D7 equ P0.3

$NOLIST
$include(LCD_4bit.inc)
$include(math32.inc)
$LIST

DSEG at 30H
x:          ds 4
y:          ds 4
bcd:        ds 5
VAL_LM4040: ds 2
last_temp:  ds 4

BSEG
mf: dbit 1

test_message:     db '*** ADC TEST ***', 0
value_message:    db 'Temp=      ', 0

Init_All:
    mov P3M1, #0x00
    mov P3M2, #0x00
    mov P1M1, #0x00
    mov P1M2, #0x00
    mov P0M1, #0x00
    mov P0M2, #0x00

    orl P0M2, #0b00010000
    orl P1M2, #0b01100000

    orl CKCON, #0x10
    orl PCON, #0x80
    mov SCON, #0x52
    anl TMOD, #0x0F
    orl TMOD, #0x20
    mov TH1, #TIMER1_RELOAD
    setb TR1

    clr TR0
    orl CKCON,#0x08
    anl TMOD,#0xF0
    orl TMOD,#0x01

    orl P1M1, #0b10000010
    mov AINDIDS, #0x00
    orl AINDIDS, #0b10000001
    orl ADCCON1, #0x01
    ret

wait_1ms:
    clr TR0
    clr TF0
    mov TH0, #high(TIMER0_RELOAD_1MS)
    mov TL0,#low(TIMER0_RELOAD_1MS)
    setb TR0
    jnb TF0, $
    ret

waitms:
    lcall wait_1ms
    djnz R2, waitms
    ret

Read_ADC:
    clr ADCF
    setb ADCS
    jnb ADCF, $
    mov a, ADCRL
    anl a, #0x0f
    mov R0, a
    mov a, ADCRH
    swap a
    push acc
    anl a, #0x0f
    mov R1, a
    pop acc
    anl a, #0xf0
    orl a, R0
    mov R0, A
    ret

main:
    mov sp, #0x7f
    lcall Init_All
    lcall LCD_4BIT

    Set_Cursor(1, 1)
    Send_Constant_String(#test_message)
    Set_Cursor(2, 1)
    Send_Constant_String(#value_message)

Forever:
    anl ADCCON0, #0xF0
    orl ADCCON0, #0x00
    lcall Read_ADC
    mov VAL_LM4040+0, R0
    mov VAL_LM4040+1, R1

    anl ADCCON0, #0xF0
    orl ADCCON0, #0x07
    lcall Read_ADC
    mov x+0, R0
    mov x+1, R1
    mov x+2, #0
    mov x+3, #0
    Load_y(41290)
    lcall mul32
    mov y+0, VAL_LM4040+0
    mov y+1, VAL_LM4040+1
    mov y+2, #0
    mov y+3, #0
    lcall div32
    Load_y(27314)
    lcall sub32

    Load_y(TEMP_THRESHOLD)
    lcall x_gt_y
    jb mf, Drive_High
    clr P0.4
    sjmp Check_Trend
Drive_High:
    setb P0.4

Check_Trend:
    mov y+0, last_temp+0
    mov y+1, last_temp+1
    mov y+2, last_temp+2
    mov y+3, last_temp+3
    lcall x_gt_y
    jb mf, Temp_Rising
    lcall x_lt_y
    jb mf, Temp_Falling
    clr P1.5
    clr P1.6
    sjmp Save_History
Temp_Rising:
    setb P1.5
    clr P1.6
    sjmp Save_History
Temp_Falling:
    clr P1.5
    setb P1.6

Save_History:
    lcall hex2bcd
    Set_Cursor(2, 7)
    Display_BCD(bcd+1)
    Display_char(#'.')
    Display_BCD(bcd+0)

    mov last_temp+0, x+0
    mov last_temp+1, x+1
    mov last_temp+2, x+2
    mov last_temp+3, x+3

    lcall Send_PC

    mov R2, #250
    lcall waitms
    mov R2, #250
    lcall waitms
    ljmp Forever

Send_PC:
    mov a, bcd+1
    swap a
    anl a, #0x0f
    add a, #'0'
    lcall putchar
    mov a, bcd+1
    anl a, #0x0f
    add a, #'0'
    lcall putchar
    mov a, #0x0a
    lcall putchar
    ret

putchar:
    jnb TI, putchar
    clr TI
    mov SBUF, a
    ret

END
