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
//       NRST -|4       29|- PB6 (SPEAKER OUT)
//       VDDA -|5       28|- PB5 (CONTINUITY PROBE)
// LCD_RS PA0 -|6       27|- PB4
// LCD_E  PA1 -|7       26|- PB3
// LCD_D4 PA2 -|8       25|- PA15
// LCD_D5 PA3 -|9       24|- PA14
// LCD_D6 PA4 -|10      23|- PA13
// LCD_D7 PA5 -|11      22|- PA12
// IND_IN PA6 -|12      21|- PA11 (CONNECT DRAIN HERE)
// BTN_C  PA7 -|13      20|- PA10 (Reserved for RXD)
// BTN_R  PB0 -|14      19|- PA9  (Reserved for TXD)
// RES_IN PB1 -|15      18|- CAP_IN (PA8)
//        VSS -|16      17|- VDD
//              ----------

void Configure_Pins (void)
{
    // Enable clocks for Port A and Port B
    RCC->IOPENR |= (BIT0 | BIT1); 
    
    // 1. LCD Pins (PA0-PA5)
    GPIOA->MODER = (GPIOA->MODER & ~0xFFF) | 0x555; 
    GPIOA->OTYPER &= ~0x3F; 

    // 2. Capacitance Input (PA8 / Pin 18)
    GPIOA->MODER &= ~(BIT16 | BIT17); 
    GPIOA->PUPDR |= BIT16; // Pull-up           

    // 3. Inductance Input (PA6 / Pin 12) - Connected to CMOS Drain
    GPIOA->MODER &= ~(BIT12 | BIT13); 
    GPIOA->PUPDR |= BIT12; // Pull-up           

    // 4. Unit Buttons (PA7 and PB0)
    GPIOA->MODER &= ~(BIT14 | BIT15); // BTN_C on PA7
    GPIOA->PUPDR |= BIT14; 
    GPIOB->MODER &= ~(BIT0 | BIT1);   // BTN_R on PB0
    GPIOB->PUPDR |= BIT0;

    // 5. Resistance Analog Input (PB1 / Pin 15)
    GPIOB->MODER |= (BIT2 | BIT3);    

    // 6. Continuity Probe (PB5 / Pin 28)
    GPIOB->MODER &= ~(BIT10 | BIT11); 
    GPIOB->PUPDR |= BIT10; 

    // 7. Speaker Output (PB6 / Pin 29)
    GPIOB->MODER = (GPIOB->MODER & ~(BIT12 | BIT13)) | BIT12;
}

#define F_CPU 32000000L

// Software delay for beeper
void software_wait_us(int us)
{
    volatile int i;
    for (i = 0; i < us * 10; i++); 
}

// Frequency measurement logic
long int GetPeriod (int n, uint32_t pin_mask)
{
    int i;
    SysTick->LOAD = 0xffffff;  
    SysTick->VAL = 0xffffff; 
    SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; 
    while ((GPIOA->IDR & pin_mask) != 0) if(SysTick->CTRL & BIT16) return 0;
    SysTick->CTRL = 0x00; 

    SysTick->LOAD = 0xffffff;  
    SysTick->VAL = 0xffffff; 
    SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; 
    while ((GPIOA->IDR & pin_mask) == 0) if(SysTick->CTRL & BIT16) return 0;
    SysTick->CTRL = 0x00; 
    
    SysTick->LOAD = 0xffffff;  
    SysTick->VAL = 0xffffff; 
    SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; 
    for(i=0; i<n; i++) 
    {
        while ((GPIOA->IDR & pin_mask) != 0) if(SysTick->CTRL & BIT16) return 0;
        while ((GPIOA->IDR & pin_mask) == 0) if(SysTick->CTRL & BIT16) return 0;
    }
    SysTick->CTRL = 0x00; 

    return 0xffffff - SysTick->VAL;
}

void PlayBeep(int duration_ms)
{
    int i;
    for(i = 0; i < duration_ms * 5; i++)
    {
        GPIOB->ODR |= BIT6;
        software_wait_us(200);
        GPIOB->ODR &= ~BIT6;
        software_wait_us(200);
    }
}

void main(void)
{
    char buff[17];
    long int count;
    
    // Capacitance Constants (555 Timer)
    float ra = 3250.0, rb = 3245.0; 
    
    // CMOS Colpitts Constants
    // Ct = (C1*C2)/(C1+C2) = (1nF*1nF)/(1nF+1nF) = 0.5nF
    float c_ref_ind = 0.5e-9;           
    const float pi = 3.14159265;

    // Resistance Variables
    float r_ref = 330.0;
    float rx;
    int adc_raw;

    // Display State
    int c_mode = 1; // 1 = nF/uH, 2 = uF/mH
    int r_mode = 1; // 1 = Ohms, 2 = kOhms
    int last_btn_c = 1, last_btn_r = 1;

    Configure_Pins();
    LCD_4BIT();
    initADC(); 
    
    waitms(500);

    while(1)
    {
        // 1. Continuity Probe
        if ((GPIOB->IDR & BIT5) == 0) PlayBeep(50);

        // 2. Button Handling
        int current_btn_c = (GPIOA->IDR & BIT7) ? 1 : 0;
        int current_btn_r = (GPIOB->IDR & BIT0) ? 1 : 0;
        if (current_btn_c == 0 && last_btn_c == 1) c_mode = (c_mode == 1) ? 2 : 1;
        if (current_btn_r == 0 && last_btn_r == 1) r_mode = (r_mode == 1) ? 2 : 1;
        last_btn_c = current_btn_c; last_btn_r = current_btn_r;

        // 3. Resistance (Row 2 Display - shared with Inductance)
        adc_raw = readADC(ADC_CHSELR_CHSEL9);
        rx = r_ref * ((float)adc_raw / (4096.0 - adc_raw));

        // 4. L or C Measurement
        count = GetPeriod(10, BIT8); // Check 555 timer first
        if(count > 0) 
        {
            float freq = 1.0 / (count / (F_CPU * 10.0)); 
            float c_val = 1.44 / (freq * (ra + 2*rb));
            if (c_mode == 1) snprintf(buff, sizeof(buff), "C=%dnF          ", (int)(c_val * 1e9));
            else snprintf(buff, sizeof(buff), "C=%d.%03duF     ", (int)(c_val*1e6)/1000, (int)(c_val*1e6)%1000);
            LCDprint(buff, 1, 1);
        }

        // Inductance Measurement from PA6 (Drain Row)
        count = GetPeriod(10, BIT6);
        if (count > 0)
        {
            float freq = 1.0 / (count / (F_CPU * 10.0));
            // L = 1 / (4 * pi^2 * f^2 * Ct)
            float l_val = 1.0 / (4.0 * pi * pi * freq * freq * c_ref_ind);
            int l_uH = (int)((l_val * 1e6) + 0.5);
            
            if (c_mode == 1) snprintf(buff, sizeof(buff), "L=%duH          ", l_uH);
            else snprintf(buff, sizeof(buff), "L=%d.%03dmH       ", l_uH/1000, l_uH%1000);
            LCDprint(buff, 2, 1);
        }
        else 
        {
            // If no inductance signal, show Resistance on Row 2
            if (adc_raw >= 4090) snprintf(buff, sizeof(buff), "R=Open Circuit  ");
            else {
                if (r_mode == 1) snprintf(buff, sizeof(buff), "R=%d Ohms       ", (int)rx);
                else snprintf(buff, sizeof(buff), "R=%d.%03dk      ", (int)rx/1000, (int)rx%1000);
            }
            LCDprint(buff, 2, 1);
        }
        
        waitms(50); 
    }
}
