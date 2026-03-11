#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../Common/Include/stm32l051xx.h"
#include "../Common/Include/serial.h"
#include "lcd.h"  
#include "adc.h"

// LQFP32 pinout for RLC Meter
//              ----------
//        VDD -|1       32|- VSS
//       PC14 -|2       31|- BOOT0
//       PC15 -|3       30|- PB7
//       NRST -|4       29|- PB6 (CONTINUITY PROBE)
//       VDDA -|5       28|- PB5 (BUZZER/LED OUT)
// LCD_RS PA0 -|6       27|- PB4
// LCD_E  PA1 -|7       26|- PB3
// LCD_D4 PA2 -|8       25|- PA15
// LCD_D5 PA3 -|9       24|- PA14
// LCD_D6 PA4 -|10      23|- PA13
// LCD_D7 PA5 -|11      22|- PA12
// IND_IN PA6 -|12      21|- PA11 (CONNECT CMOS DRAIN HERE)
// BTN_C  PA7 -|13      20|- PA10 (Reserved for RXD)
// BTN_R  PB0 -|14      19|- PA9  (Reserved for TXD)
// RES_IN PB1 -|15      18|- CAP_IN (PA8)
//        VSS -|16      17|- VDD
//              ----------

void Configure_Pins (void)
{
    // Enable clocks for Port A and Port B
    RCC->IOPENR |= (BIT0 | BIT1); 
    
    // 1. LCD Pins (PA0-PA5) - Output mode
    GPIOA->MODER = (GPIOA->MODER & ~0xFFF) | 0x555; 
    GPIOA->OTYPER &= ~0x3F; 

    // 2. Capacitance Input (PA8 / Pin 18) - Input Pull-up
    GPIOA->MODER &= ~(BIT16 | BIT17); 
    GPIOA->PUPDR |= BIT16;           

    // 3. Inductance Input (PA6 / Pin 12) - Input Pull-up
    GPIOA->MODER &= ~(BIT12 | BIT13); 
    GPIOA->PUPDR |= BIT12;           

    // 4. Unit Buttons (PA7 and PB0) - Input Pull-up
    GPIOA->MODER &= ~(BIT14 | BIT15); // BTN_C on PA7
    GPIOA->PUPDR |= BIT14; 
    GPIOB->MODER &= ~(BIT0 | BIT1);   // BTN_R on PB0
    GPIOB->PUPDR |= BIT0;

    // 5. Resistance Analog Input (PB1 / Pin 15)
    GPIOB->MODER |= (BIT2 | BIT3);    

    // 6. Continuity Probe (PB6 / Pin 29) - Input Pull-up
    GPIOB->MODER &= ~(BIT12 | BIT13); 
    GPIOB->PUPDR = (GPIOB->PUPDR & ~(BIT12 | BIT13)) | BIT12; 

    // 7. Buzzer/LED Output (PB5 / Pin 28) - Output Push-Pull
    GPIOB->MODER = (GPIOB->MODER & ~(BIT10 | BIT11)) | BIT10;
}

#define F_CPU 32000000L

// Instant continuity checker - Call this constantly
void CheckContinuity(void)
{
    if ((GPIOB->IDR & BIT6) == 0) {
        // Pin 29 is Grounded! Turn ON Pin 28
        GPIOB->ODR |= BIT5;
    } else {
        // Pin 29 is Open. Turn OFF Pin 28
        GPIOB->ODR &= ~BIT5;
    }
}

// Frequency measurement logic (Upgraded for Instant Continuity)
long int GetPeriod (int n, uint32_t pin_mask)
{
    int i;
    SysTick->LOAD = 0xffffff;  
    SysTick->VAL = 0xffffff; 
    SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; 
    
    while ((GPIOA->IDR & pin_mask) != 0) {
        if(SysTick->CTRL & BIT16) return 0;
        CheckContinuity(); // Listen for probes while waiting!
    }
    SysTick->CTRL = 0x00; 

    SysTick->LOAD = 0xffffff;  
    SysTick->VAL = 0xffffff; 
    SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; 
    while ((GPIOA->IDR & pin_mask) == 0) {
        if(SysTick->CTRL & BIT16) return 0;
        CheckContinuity();
    }
    SysTick->CTRL = 0x00; 
    
    SysTick->LOAD = 0xffffff;  
    SysTick->VAL = 0xffffff; 
    SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; 
    for(i=0; i<n; i++) 
    {
        while ((GPIOA->IDR & pin_mask) != 0) {
            if(SysTick->CTRL & BIT16) return 0;
            CheckContinuity();
        }
        while ((GPIOA->IDR & pin_mask) == 0) {
            if(SysTick->CTRL & BIT16) return 0;
            CheckContinuity();
        }
    }
    SysTick->CTRL = 0x00; 

    return 0xffffff - SysTick->VAL;
}

void main(void)
{
    char buff[17];
    char str_r[16];
    char str_c[16];
    
    float ra = 3250.0, rb = 3245.0; // 555 Timer calibration
    const float pi = 3.14159265;

    // --- OSCILLATOR CALIBRATION ---
    // Two 1nF caps in series = 0.5nF
    float c_physical = 0.5e-9; 
    
    // Calibrated Stray Capacitance based on 900uH testing:
    // This compensates for MOSFET gate capacitance and breadboard parasitics!
    float c_stray = 0.431e-9;    
    
    float c_total = c_physical + c_stray; 

    float r_ref = 330.0;
    float rx;
    int adc_raw;

    int c_mode = 1; 
    int r_mode = 1; 
    int last_btn_c = 1, last_btn_r = 1;

    Configure_Pins();
    LCD_4BIT();
    initADC(); 
    
    waitms(500);

    while(1)
    {
        // 1. Trap loop for holding continuity probes together
        CheckContinuity();
        while ((GPIOB->IDR & BIT6) == 0) {
            CheckContinuity(); 
        }

        // 2. Button Handling
        int current_btn_c = (GPIOA->IDR & BIT7) ? 1 : 0;
        int current_btn_r = (GPIOB->IDR & BIT0) ? 1 : 0;
        if (current_btn_c == 0 && last_btn_c == 1) c_mode = (c_mode == 1) ? 2 : 1;
        if (current_btn_r == 0 && last_btn_r == 1) r_mode = (r_mode == 1) ? 2 : 1;
        last_btn_c = current_btn_c; last_btn_r = current_btn_r;

        // 3. Read Resistance (Row 1 Left)
        adc_raw = readADC(ADC_CHSELR_CHSEL9);
        rx = r_ref * ((float)adc_raw / (4096.0 - adc_raw));
        
        if (adc_raw >= 4090) {
            snprintf(str_r, sizeof(str_r), "R:Open");
        } else {
            if (r_mode == 1) snprintf(str_r, sizeof(str_r), "R:%d", (int)rx);
            else snprintf(str_r, sizeof(str_r), "R:%d.%01dk", (int)rx/1000, ((int)rx%1000)/100);
        }

        // 4. Read Capacitance (Row 1 Right)
        long int count_c = GetPeriod(10, BIT8);
        if(count_c > 0) 
        {
            float freq = 1.0 / (count_c / (F_CPU * 10.0)); 
            float c_val = 1.44 / (freq * (ra + 2*rb));
            if (c_mode == 1) snprintf(str_c, sizeof(str_c), "C:%dnF", (int)(c_val * 1e9));
            else snprintf(str_c, sizeof(str_c), "C:%d.%02duF", (int)(c_val*1e6)/1000, ((int)(c_val*1e6)%1000)/10);
        }
        else 
        {
            snprintf(str_c, sizeof(str_c), "C:None");
        }

        // Display Row 1 (Formats strictly to 8 characters each so they share the row perfectly)
        snprintf(buff, sizeof(buff), "%-8.8s%-8.8s", str_r, str_c);
        LCDprint(buff, 1, 1);

        // 5. Read Inductance (Row 2)
        long int count_l = GetPeriod(10, BIT6);
        if (count_l > 0)
        {
            float freq = 1.0 / (count_l / (F_CPU * 10.0));
            float l_val = 1.0 / (4.0 * pi * pi * freq * freq * c_total);
            int l_uH = (int)((l_val * 1e6) + 0.5);
            
            if (c_mode == 1) snprintf(buff, sizeof(buff), "L:%duH          ", l_uH);
            else snprintf(buff, sizeof(buff), "L:%d.%03dmH       ", l_uH/1000, l_uH%1000);
        }
        else 
        {
            snprintf(buff, sizeof(buff), "L:None          ");
        }
        LCDprint(buff, 2, 1);
        
        waitms(50); 
    }
}
