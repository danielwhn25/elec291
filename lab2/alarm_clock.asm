; DANIEL NG 38327466 
; ELEC 291 LAB 2 ALARM CLOCK JANUARY 20 2026

; ISR_example.asm: a) Increments/decrements a BCD variable every half second using
; an ISR for timer 2; b) Generates a 2kHz square wave at pin P1.7 using
; an ISR for timer 0; and c) in the 'main' loop it displays the variable
; incremented/decremented using the ISR for timer 2 on the LCD.  Also resets it to 
; zero if the 'CLEAR' push button connected to P1.5 is pressed.
$NOLIST
$MODN76E003
$LIST

;  N76E003 pinout:
;                               -------
;       PWM2/IC6/T0/AIN4/P0.5 -|1    20|- P0.4/AIN5/STADC/PWM3/IC3
;               TXD/AIN3/P0.6 -|2    19|- P0.3/PWM5/IC5/AIN6
;               RXD/AIN2/P0.7 -|3    18|- P0.2/ICPCK/OCDCK/RXD_1/[SCL]
;                    RST/P2.0 -|4    17|- P0.1/PWM4/IC4/MISO
;        INT0/OSCIN/AIN1/P3.0 -|5    16|- P0.0/PWM3/IC3/MOSI/T1
;               INT1/AIN0/P1.7 -|6    15|- P1.0/PWM2/IC2/SPCLK
;                         GND -|7    14|- P1.1/PWM1/IC1/AIN7/CLO
;[SDA]/TXD_1/ICPDA/OCDDA/P1.6 -|8    13|- P1.2/PWM0/IC0
;                         VDD -|9    12|- P1.3/SCL/[STADC]
;            PWM5/IC7/SS/P1.5 -|10   11|- P1.4/SDA/FB/PWM1
;                               -------
;

CLK           EQU 16600000 ; Microcontroller system frequency in Hz
TIMER0_RATE   EQU 4096     ; 2048Hz squarewave (peak amplitude of CEM-1203 speaker)
TIMER0_RELOAD EQU ((65536-(CLK/TIMER0_RATE)))
TIMER2_RATE   EQU 1000     ; 1000Hz, for a timer tick of 1ms
TIMER2_RELOAD EQU ((65536-(CLK/TIMER2_RATE)))

ALARM_BUTTON  equ P0.4 
UPDOWN        equ P1.1
CLEAR_BUTTON  equ P1.5
SOUND_OUT     equ P1.7
AMPM_BUTTON   equ P1.2
;555_rst 	  equ P1.0

; Reset vector
org 0x0000
    ljmp main
    
; External interrupt 0
org 0x0003
    ljmp HOURS_ISR

; Timer0 overflow interrupt vector
org 0x000B
    ljmp Timer0_ISR

; Timer2 overflow interrupt vector
org 0x002B
    ljmp Timer2_ISR

org 0x003B
    ljmp PINS_ISR

;------------------------DATA SEGMENT----------------------------------------------------------;

; In the 8051 we can define direct access variables starting at location 0x30 up to location 0x7F
dseg at 0x30        ; datasegment
Count1ms:      ds 2 ; 16-bit counter for the 1ms ticks
seconds:       ds 1 ; Current BCD seconds
minutes:       ds 1 ; Current BCD minutes
hours:         ds 1 ; Current BCD hours
alarm_minutes: ds 1 ; Alarm BCD minutes
alarm_hours:   ds 1 ; Alarm BCD hours


;-----------------------BINARY SEGMENT---------------------------------------------------------;
bseg
seconds_flag: dbit 1 ; UPDATED FROM EXAMPLE: Set to one in the ISR every time 1000 ms had passed
alarm_toggle: dbit 1 ; turn on/off alarm -- initialize it to 0 in main
alarm_AMPM_toggle: dbit 1 ; 0 = AM, 1 = PM
clock_AMPM_toggle: dbit 1 ; 0 = AM, 1 = PM


;-----------------------CODE SEGMENT----------------------------------------------------------;
cseg                                                                                           
; These 'equ' must match the hardware wiring                                    
LCD_RS equ P1.3
;LCD_RW equ PX.X ; Not used in this code, connect the pin to GND
LCD_E  equ P1.4
LCD_D4 equ P0.0
LCD_D5 equ P0.1
LCD_D6 equ P0.2
LCD_D7 equ P0.3

$NOLIST
$include(LCD_4bit.inc) ; A library of LCD related functions and utility macros
$LIST

;                     1234567890123456    <- This helps determine the location of the counter
Initial_time:  db 'Time  12:00:00', 0
Alarm_time:    db 'Alarm 12:00', 0
alarm_on:      db ' ON', 0
alarm_off:     db 'OFF', 0
clock_AM:      db 'AM',0
clock_PM:      db 'PM',0
alarm_AM:      db 'AM', 0
alarm_PM:      db 'PM', 0

;---------------------------------;
; Routine to initialize the ISR   ;
; for timer 0                     ;
;---------------------------------;
Timer0_Init:
    orl CKCON, #0b00001000 ; Input for timer 0 is sysclk/1
    mov a, TMOD
    anl a, #0xf0 ; 11110000 Clear the bits for timer 0
    orl a, #0x01 ; 00000001 Configure timer 0 as 16-timer
    mov TMOD, a
    mov TH0, #high(TIMER0_RELOAD)
    mov TL0, #low(TIMER0_RELOAD)
    ; Enable the timer and interrupts
    setb ET0  ; Enable timer 0 interrupt
    setb TR0  ; Start timer 0
    ret

;---------------------------------;
; ISR for timer 0.  Set to execute;
; every 1/4096Hz to generate a    ;
; 2048 Hz wave at pin SOUND_OUT   ;
;---------------------------------;
Timer0_ISR:
    ;clr TF0  ; According to the data sheet this is done for us already.
    ; Timer 0 doesn't have 16-bit auto-reload, so
    clr TR0
    mov TH0, #high(TIMER0_RELOAD)
    mov TL0, #low(TIMER0_RELOAD)
    setb TR0
    cpl SOUND_OUT ; Connect speaker the pin assigned to 'SOUND_OUT'!
    reti

;---------------------------------;
; Routine to initialize the ISR   ;
; for timer 2                     ;
;---------------------------------;
Timer2_Init:
    mov     T2CON, #0 ; Stop timer/counter.  Autoreload mode.
    mov     TH2, #high(TIMER2_RELOAD)
    mov     TL2, #low(TIMER2_RELOAD)
    ; Set the reload value
    orl     T2MOD, #0x80 ; Enable timer 2 autoreload
    mov     RCMP2H, #high(TIMER2_RELOAD)
    mov     RCMP2L, #low(TIMER2_RELOAD)
    ; Init One millisecond interrupt counter.  It is a 16-bit variable made with two 8-bit parts
    clr     a
    mov     Count1ms+0, a
    mov     Count1ms+1, a
    ; Enable the timer and interrupts
    orl     EIE, #0b10000010B ; Enable timer 2 interrupt ET2=1 AND enable pin interrupt: EPI = 1

    setb    TR2  ; Enable timer 2
    ret

; ----------------------------------------TIMER 2 ISR---------------------------------------;
Timer2_ISR:
    clr TF2  ; Timer 2 doesn't clear TF2 automatically. Do it in the ISR.  It is bit addressable.
    
    ; The two registers used in the ISR must be saved in the stack
    push acc
    push psw
    
    ; Increment the 16-bit one mili second counter
    inc Count1ms+0    ; Increment the low 8-bits first
    mov a, Count1ms+0 ; If the low 8-bits overflow, then increment high 8-bits
    jnz Inc_Done
    inc Count1ms+1

Inc_Done:
    ; Check if half second has passed
    mov a, Count1ms+0
    cjne a, #low(1000), Timer2_ISR_done ; Warning: this instruction changes the carry flag!
    mov a, Count1ms+1
    cjne a, #high(1000), Timer2_ISR_done
    
    ; 1000 milliseconds have passed.  Set a flag so the main program knows
    setb seconds_flag ; Let the main program know half second had passed
    cpl TR0 ; Enable/disable timer/counter 0. This line creates a beep-silence-beep-silence sound.
    ; Reset to zero the milli-seconds counter, it is a 16-bit variable
    clr a
    mov Count1ms+0, a
    mov Count1ms+1, a
    ; Increment the BCD counter
    mov a, seconds
    jnb UPDOWN, Timer2_ISR_decrement
    add a, #0x01
    sjmp Timer2_ISR_da
Timer2_ISR_decrement:
    add a, #0x99 ; Adding the 10-complement of -1 is like subtracting 1.
Timer2_ISR_da:
    da a ; Decimal adjust instruction.  Check datasheet for more details!
    mov seconds, a
    
    cjne a, #0x60, seconds_done ; Fixed: Clock resets at 60
    
    mov seconds, #0x00  ;
    
    ; Manual Increment of Minutes
    mov a, minutes
    add a, #0x01
    da  a
    mov minutes, a
    cjne a, #0x60, seconds_done
    mov minutes, #0x00
    
    ; Manual Increment of Hours with AM/PM Logic
    mov a, hours
    add a, #0x01
    da  a
    mov hours, a
    
    ; Flip AM/PM when moving 11 -> 12
    cjne a, #0x12, check_13_auto
    cpl clock_AMPM_toggle
    
check_13_auto:
    cjne a, #0x13, seconds_done
    mov hours, #0x01
    
seconds_done:
    
Timer2_ISR_done:
    pop psw
    pop acc
    reti


;-----------------------HOURS BUTTON ISR-----------------------;
HOURS_ISR:
    push    ACC
    push    psw

    ; check if we're modifying alarm_hours or not 
    jb      alarm_toggle, alarm_hours_routine ; if alarm_toggle = 1, go do the alarm hours routine
    ; this ISR will set the hours
    mov     a, hours
    add     a, #0x01
    da      a
    mov     hours, a
    
    cjne    a, #0x12, clock_13_man ; Check for AM/PM flip at 12
    cpl     clock_AMPM_toggle

clock_13_man:
    cjne    a, #0x13, HOURS_ISR_done ; if hours == 13, we've rolled over so reset to 1
    mov     hours, #0x01
    sjmp    HOURS_ISR_done

alarm_hours_routine:
    mov     a, alarm_hours
    add     a, #0x01
    da      a
    mov     alarm_hours, a

    cjne    a, #0x12, alarm_13_man ; Check for AM/PM flip at 12
    cpl     alarm_AMPM_toggle

alarm_13_man:
    cjne    a, #0x13, HOURS_ISR_done
    mov     alarm_hours, #0x01

HOURS_ISR_done:
    pop psw
    pop ACC
    reti



;---------------------------------;
; Main program. Includes hardware ;
; initialization and 'forever'    ;
; loop.                           ;
;---------------------------------;
main:
    mov         SP  , #0x7F
    mov         P0M1, #0x00
    mov         P0M2, #0x00
    mov         P1M1, #0x00
    mov         P1M2, #0x00
    mov         P3M2, #0x00
    mov         P3M1, #0x00
	anl         P1M1, #01111110B 
    orl         P1M2, #10000001B


          
    lcall Timer0_Init
    lcall Timer2_Init

    ;initailize the time to 12:00:00 PM
    mov         seconds         , #0x00
    mov         minutes         , #0x00
    mov         hours           , #0x12
    mov         alarm_minutes   , #0x00
    mov         alarm_hours     , #0x12
    clr         alarm_toggle    ; Initialize edit mode bit to zero
    clr         alarm_AMPM_toggle ; 0 = AM
    clr         clock_AMPM_toggle ; 0 = AM
   ; setb			555_rst			;, #0x00 ; extra feature!!!!!!!
   setb			P1.0

    setb EA   

;----------------INITIALIZE EXTERNAL INTERRUPT 0-----------------;
    setb EX0
    setb IT0
;----------------INITIALIZE PIN INTERRUPTS--------------;

    ; pins .1, 2, 6
    mov     PICON, #10011001B
    mov     PINEN, #01000110B

    orl     EIE, #00000010B



    lcall LCD_4BIT
    WriteCommand(#0x0C)

    Set_Cursor(1, 1)
    Send_Constant_String(#Initial_time)

    Set_Cursor(2,1)
    Send_Constant_String(#Alarm_time)
    Set_Cursor(2, 14)
    Send_Constant_String(#alarm_off) ; Initial status

    setb seconds_flag
    mov seconds, #0x00
    

loop:


;-------------------------------ALARM TOGGLE CHECK (polling)----------------------------------;
alarm_check_routine:
    ; toggle the edit mode bit. 

    jb      ALARM_BUTTON, AMPM_check_routine 
    Wait_Milli_Seconds(#50)
    jb      ALARM_BUTTON, AMPM_check_routine
    jnb     ALARM_BUTTON, $ ; wait for release

    cpl     alarm_toggle ; compliment the variable
    
    jb      alarm_toggle, show_alarm_on
    Set_Cursor(2,14)
    Send_Constant_String(#alarm_off)
    sjmp    AMPM_check_routine

show_alarm_on:
    Set_Cursor(2,14)
    Send_Constant_String(#alarm_on)


;-------------------------------AMPM TOGGLE CHECK (polling)------------------------;

AMPM_check_routine:
    jb      AMPM_BUTTON, check_clear 
    Wait_Milli_Seconds(#50)
    jb      AMPM_BUTTON, check_clear
    jnb     AMPM_BUTTON, $ ; wait for release

    jb      alarm_toggle, toggle_alarm_ampm_man
    cpl     clock_AMPM_toggle
    sjmp    check_clear

toggle_alarm_ampm_man:
    cpl     alarm_AMPM_toggle


;--------------------------------CLEAR BUTTON CHECK (polling)-----------------------------------------------;

check_clear:

    jb 		CLEAR_BUTTON, loop_a  ; if (CLEAR_BUTTON) skip
    Wait_Milli_Seconds(#50) ; Debounce delay.  
    jb 		CLEAR_BUTTON, loop_a  ; another check
    jnb 	CLEAR_BUTTON, $     ; Wait for button release.  
    ; A valid press of the 'CLEAR' button has been detected, reset the BCD counter.
    clr 	TR2                 ; Stop timer 2
    clr 	a
    mov 	Count1ms+0, a
    mov 	Count1ms+1, a
    ; Now clear the BCD counter
    mov 	seconds, a
    setb 	TR2                ; Start timer 2
    sjmp 	loop_b              ; Display the new value
loop_a:
    jb 		seconds_flag, loop_b  ; If flag is set, jump over the long jump to the display code
    ljmp 	loop                ; Otherwise, use a Long Jump to reach the distant top
loop_b:

    clr 	seconds_flag     ; We clear this flag in the main loop, but it is set in the ISR for timer 2

    Set_Cursor(1, 7)
    Display_BCD(hours)

    Set_Cursor(1, 10)
    Display_BCD(minutes)

    Set_Cursor(1, 13)    
    Display_BCD(seconds) 

    Set_Cursor(1, 15)
    jb clock_AMPM_toggle, show_clock_pm_str
    Send_Constant_String(#clock_AM)
    sjmp display_alarm_data

show_clock_pm_str:
    Send_Constant_String(#clock_PM)

display_alarm_data:
    Set_Cursor(2, 7)
    Display_BCD(alarm_hours)

    Set_Cursor(2, 10)
    Display_BCD(alarm_minutes)

    Set_Cursor(2, 12)
    jb alarm_AMPM_toggle, show_alarm_pm_str
    Send_Constant_String(#alarm_AM)
    sjmp alarm_compare_logic_label
show_alarm_pm_str:
    Send_Constant_String(#alarm_PM)

;-----------------------ALARM COMPARISON-----------------------------------;
alarm_compare_logic_label:
    mov         a, hours
    cjne        a, alarm_hours, alarm_beep_stop 
    
    mov         a, minutes
    cjne        a, alarm_minutes, alarm_beep_stop 

    jb          clock_AMPM_toggle, check_pm_match
    jb          alarm_AMPM_toggle, alarm_beep_stop  
    sjmp        check_alarm_enabled                    

check_pm_match:
    jnb         alarm_AMPM_toggle, alarm_beep_stop  

check_alarm_enabled:
    ; If alarm_toggle is 0 (OFF), we jump to stop beeping immediately
    ; even though the hours/minutes/AMPM match perfectly.
    jnb         alarm_toggle, alarm_beep_stop

start_the_beep:
    setb        ET0 ; Enable sound interrupt
    setb        P1.0 ; Enable LED Blinker
    ljmp        loop

alarm_beep_stop:  
    clr         ET0 ; Disable sound interrupt 
    clr         SOUND_OUT ; Force speaker pin low to stop buzz
    clr         P1.0 ; Turn off LED Blinker
    ljmp        loop
        
;----------------------PINS (1.1, 1.2, and 1.6) GENERAL INTERRUPT ISR---------------;
; all pin interrupts will jump here and so then we'll use a controller to poll which pin it was using PIF register

PINS_ISR:
    push    ACC
    push    psw

    mov     a, PIF     ; copy the flags into a
    mov     PIF, #0x00 ; clear flags via software as stated in DS
    
    ; checking for PIF6, P1.6, which is pin 8, for the minutes 
    mov     b, a ; Keep a copy for ELIF_seconds
    anl     a, #01000000B
    jz      ELIF_seconds ; if PIF6 is 0, skip to seconds check

; fall through if equal
IF_minutes:
    ; if alarm mode bit = 1, then we want to change the alarm
    jb      alarm_toggle, IF_alarm_minutes

    mov     a, minutes 
    add     a, #0x01
    da      a
    mov     minutes, a

    ; overflow check
    cjne    a, #0x60, minutes_done ; if minutes == 60, we've rolled over so reset to 0
    mov     minutes, #0x00

minutes_done:
    sjmp    PINS_ISR_DONE

IF_alarm_minutes:
    ; first increment the minutes and check for rollover
    mov     a, alarm_minutes
    add     a, #0x01    
    da      a
    mov     alarm_minutes, a

    cjne    a, #0x60, alarm_minutes_done
    mov     alarm_minutes, #0x0

alarm_minutes_done:
    sjmp    PINS_ISR_DONE


ELIF_seconds: ; poll pin 15 (seconds adjust button)
    mov     a, b
    anl     a, #00000010B ; Check Bit 1 (P1.1)
    jz      PINS_ISR_DONE

    mov     a, seconds
    add     a, #0x01        ; add 1 to the seconds counter
    da      a               ; decima adjust
    mov     seconds, a      ; put the updated value back into seconds variable 

    cjne    a, #0x60, PINS_ISR_DONE ; Check rollover at 60
    mov     seconds, #0x00
    
PINS_ISR_DONE:
    pop 	psw
    pop 	ACC
    reti

END