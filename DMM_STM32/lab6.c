#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../Common/Include/stm32l051xx.h"
#include "../Common/Include/serial.h"
#include "lcd.h"  
#include "adc.h"

// LQFP32 pinout for LCD
//              ----------
//        VDD -|1       32|- VSS
//       PC14 -|2       31|- BOOT0
//       PC15 -|3       30|- PB7
//       NRST -|4       29|- PB6
//       VDDA -|5       28|- PB5
// LCD_RS PA0 -|6       27|- PB4
// LCD_E  PA1 -|7       26|- PB3
// LCD_D4 PA2 -|8       25|- PA15
// LCD_D5 PA3 -|9       24|- PA14
// LCD_D6 PA4 -|10      23|- PA13
// LCD_D7 PA5 -|11      22|- PA12
//        PA6 -|12      21|- PA11
//        PA7 -|13      20|- PA10 (Reserved for RXD)
//        PB0 -|14      19|- PA9  (Reserved for TXD)
//        PB1 -|15      18|- PA8  (Measure the period at this pin)
//        VSS -|16      17|- VDD
//              ----------

void Configure_Pins (void)
{
    // Enable clocks for Port A and Port B
    RCC->IOPENR |= (BIT0 | BIT1); 
    
    // 1. Configure PA0 to PA5 as Outputs for the LCD
    GPIOA->MODER = (GPIOA->MODER & ~(BIT0|BIT1)) | BIT0; // PA0
    GPIOA->OTYPER &= ~BIT0; 
    
    GPIOA->MODER = (GPIOA->MODER & ~(BIT2|BIT3)) | BIT2; // PA1
    GPIOA->OTYPER &= ~BIT1; 
    
    GPIOA->MODER = (GPIOA->MODER & ~(BIT4|BIT5)) | BIT4; // PA2
    GPIOA->OTYPER &= ~BIT2; 
    
    GPIOA->MODER = (GPIOA->MODER & ~(BIT6|BIT7)) | BIT6; // PA3
    GPIOA->OTYPER &= ~BIT3; 
    
    GPIOA->MODER = (GPIOA->MODER & ~(BIT8|BIT9)) | BIT8; // PA4
    GPIOA->OTYPER &= ~BIT4; 
    
    GPIOA->MODER = (GPIOA->MODER & ~(BIT10|BIT11)) | BIT10; // PA5
    GPIOA->OTYPER &= ~BIT5; 

    // 2. Configure PA8 (pin 18) strictly as INPUT for the 555 Timer
    GPIOA->MODER &= ~(BIT16 | BIT17); // 00 = Input Mode
    GPIOA->PUPDR |= BIT16;            // Enable pull-up
    GPIOA->PUPDR &= ~(BIT17); 

    // 3. Configure PB1 (pin 15) as ANALOG for the ADC
    GPIOB->MODER |= (BIT2|BIT3);      // 11 = Analog mode

    // 4. Configure PA7 (pin 13) as INPUT with Pull-Up for the nF Button
    GPIOA->MODER &= ~(BIT14 | BIT15); // 00 = Input Mode for PA7
    GPIOA->PUPDR |= BIT14;            // Enable pull-up
    GPIOA->PUPDR &= ~(BIT15); 
    
    // 5. Configure PB0 (pin 14) as INPUT with Pull-Up for the uF Button
    // WARNING: We cannot use Pin 15 because it's the ADC!
    GPIOB->MODER &= ~(BIT0 | BIT1);   // 00 = Input Mode for PB0
    GPIOB->PUPDR |= BIT0;             // Enable pull-up
    GPIOB->PUPDR &= ~(BIT1);
}

#define F_CPU 32000000L

void delay(int dly)
{
    while( dly--);
}

#define PIN_PERIOD (GPIOA->IDR&BIT8)

// GetPeriod() measures the time of 'n' periods; this increases accuracy.
long int GetPeriod (int n)
{
    int i;
    SysTick->LOAD = 0xffffff;  
    SysTick->VAL = 0xffffff; 
    SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; 
    while (PIN_PERIOD!=0) 
    {
        if(SysTick->CTRL & BIT16) return 0;
    }
    SysTick->CTRL = 0x00; 

    SysTick->LOAD = 0xffffff;  
    SysTick->VAL = 0xffffff; 
    SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; 
    while (PIN_PERIOD==0) 
    {
        if(SysTick->CTRL & BIT16) return 0;
    }
    SysTick->CTRL = 0x00; 
    
    SysTick->LOAD = 0xffffff;  
    SysTick->VAL = 0xffffff; 
    SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; 
    for(i=0; i<n; i++) 
    {
        while (PIN_PERIOD!=0) 
        {
            if(SysTick->CTRL & BIT16) return 0;
        }
        while (PIN_PERIOD==0) 
        {
            if(SysTick->CTRL & BIT16) return 0;
        }
    }
    SysTick->CTRL = 0x00; 

    return 0xffffff-SysTick->VAL;
}

void main(void)
{
    char buff[17];
    long int count;
    
    // Variables for Capacitance
    float vcc = 3.11;
    float T, f;
    float ra = 3250.0;  // Ra measured value in OHMS
    float rb = 3245.0;  // Rb measured value in OHMS
    float c;            
    float rx;
    float r_ref = 330.0; 
    
    // Variables for ADC reading
    int adc_raw;
    float adc_voltage;
    
    // Variables for Unit Buttons
    int display_mode = 1; // 1 = nF mode, 2 = uF mode
    
    Configure_Pins();
    LCD_4BIT();
    initADC(); 
    
    waitms(500); // Wait for putty to start.
    printf("Capacitance and ADC Reader Started.\r\n");

    while(1)
    {
        // --- BUTTON LOGIC ---
        // If Pin 13 (PA7) is grounded, switch to nF
        if ((GPIOA->IDR & BIT7) == 0) {
            display_mode = 1; 
        }
        // If Pin 14 (PB0) is grounded, switch to uF
        if ((GPIOB->IDR & BIT0) == 0) {
            display_mode = 2; 
        }

        // 1. Read the ADC voltage from PB1
        adc_raw = readADC(ADC_CHSELR_CHSEL9);
        
        // Use the actual VCC (3.11V) for voltage calculation
        adc_voltage = (adc_raw * vcc) / 4096.0; 
        
        int v_whole = (int)adc_voltage;
        int v_dec = (int)((adc_voltage - v_whole) * 100);

        // 2. Read the frequency from PA8 (reduced to 10 periods to prevent timeouts on big caps)
        count=GetPeriod(10);
        if(count>0)
        {
            T=count/(F_CPU*10.0); 
            f=1.0/T;
            c = 1.44 / (f * (ra+2*rb));
            
            int c_nF = (int)((c * 1e9) + 0.5);
            int uF_whole = c_nF / 1000;
            int uF_dec = c_nF % 1000;
            
            // Print to PuTTY
            printf("C=%inF, ADC: %d.%02dV      \r", c_nF, v_whole, v_dec);
            
            // Update LCD Row 1 (Capacitance) purely based on button selection
            if (display_mode == 1) { 
                snprintf(buff, sizeof(buff), "C=%dnF          ", c_nF);
            } 
            else if (display_mode == 2) { 
                snprintf(buff, sizeof(buff), "C=%d.%03duF     ", uF_whole, uF_dec);
            }
            
            LCDprint(buff, 1, 1);
        }
        else
        {
            printf("NO SIGNAL, ADC: %d.%02dV      \r", v_whole, v_dec);
            LCDprint("No signal       ", 1, 1);
        }

        // 3. Calculate Resistance using the accurate ratiometric formula
        // R_x = R_ref * (raw / (4096 - raw))
        if (adc_raw >= 4090) {
            rx = 999999.0; // Prevent divide-by-zero if resistor is pulled out
        } else {
            rx = r_ref * ((float)adc_raw / (4096.0 - adc_raw));
        }

        if (rx > 900000) {
            snprintf(buff, sizeof(buff), "R=Open Circuit  ");
        } 
        else {
            snprintf(buff, sizeof(buff), "R=%d Ohms       ", (int)rx);
        }

        LCDprint(buff, 2, 1);
        
        fflush(stdout); 
        waitms(200);
    }
}
