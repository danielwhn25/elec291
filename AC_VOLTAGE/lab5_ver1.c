// ADC.c:  Shows how to use the 14-bit ADC.  This program
// measures the voltage from some pins of the EFM8LB1 using the ADC.
//
// (c) 2008-2023, Jesus Calvino-Fraga
//

/***************************************
   NAMES DANIEL NG, KRISH VASHIST 
   DATE  7 MARCH 2026
   PHASE MEASUREMENT AND PEAK DETECTION
***************************************/


#include <stdio.h>
#include <stdlib.h>
#include <EFM8LB1.h>

// ~C51~  

#define BAUDRATE 115200L
#define SARCLK 18000000L // 
#define CH1 QFP32_MUX_P2_1
#define CH2 QFP32_MUX_P2_2

// add in timer frequencies here from "All_timers.c"
#define SYSCLK 72000000L // SYSCLK frequency in Hz
#define TIMER_0_FREQ 1000L
#define TIMER_1_FREQ 2000L

#define TIMER_OUT_0 P2_0
#define TIMER_OUT_1 P1_7

#define MAIN_OUT    P0_1 // Updated in the main program


char _c51_external_startup (void)
{
	// Disable Watchdog with key sequence
	SFRPAGE = 0x00;
	WDTCN = 0xDE; //First key
	WDTCN = 0xAD; //Second key
  
	VDM0CN=0x80;       // enable VDD monitor
	RSTSRC=0x02|0x04;  // Enable reset on missing clock detector and VDD

	#if (SYSCLK == 48000000L)	
		SFRPAGE = 0x10;
		PFE0CN  = 0x10; // SYSCLK < 50 MHz.
		SFRPAGE = 0x00;
	#elif (SYSCLK == 72000000L)
		SFRPAGE = 0x10;
		PFE0CN  = 0x20; // SYSCLK < 75 MHz.
		SFRPAGE = 0x00;
	#endif
	
	#if (SYSCLK == 12250000L)
		CLKSEL = 0x10;
		CLKSEL = 0x10;
		while ((CLKSEL & 0x80) == 0);
	#elif (SYSCLK == 24500000L)
		CLKSEL = 0x00;
		CLKSEL = 0x00;
		while ((CLKSEL & 0x80) == 0);
	#elif (SYSCLK == 48000000L)	
		// Before setting clock to 48 MHz, must transition to 24.5 MHz first
		CLKSEL = 0x00;
		CLKSEL = 0x00;
		while ((CLKSEL & 0x80) == 0);
		CLKSEL = 0x07;
		CLKSEL = 0x07;
		while ((CLKSEL & 0x80) == 0);
	#elif (SYSCLK == 72000000L)
		// Before setting clock to 72 MHz, must transition to 24.5 MHz first
		CLKSEL = 0x00;
		CLKSEL = 0x00;
		while ((CLKSEL & 0x80) == 0);
		CLKSEL = 0x03;
		CLKSEL = 0x03;
		while ((CLKSEL & 0x80) == 0);
	#else
		#error SYSCLK must be either 12250000L, 24500000L, 48000000L, or 72000000L
	#endif

	// Configure the pins used for square output
	P0MDOUT|=0b_1100_0010;
	P1MDOUT|=0b_1111_1111;
	P2MDOUT|=0b_0000_0001;
	
	P0MDOUT |= 0x10; // Enable UART0 TX as push-pull output
	XBR0     = 0x01; // Enable UART0 on P0.4(TX) and P0.5(RX)                     
	XBR1     = 0X00;
	XBR2     = 0x40; // Enable crossbar and weak pull-ups

	// Initialize timer 0 for periodic interrupts
	TR0=0;
	TF0=0;
	CKCON0 &= 0b_00000000; // Timer 0 uses clk divided by 12
	TMOD&=0xf0;
	TMOD|=0x01; // Timer 0 in mode 1: 16-bit timer
	// Initialize reload value
	TMR0=0; // reload value =0
	TR1 = 1;

	// Initialize timer 1 for periodic interrupts
	TR1=0;
	TF1=0;
	CKCON0 &= 0b_00000000; // Timer 1 uses clk divided by 12
	TMOD&=0x0f;
	TMOD|=0x10; // Timer 1 in mode 1: 16-bit timer
	// Initialize reload value
	TMR1=0; // reload value =0

		// Configure Uart 0
	#if (((SYSCLK/BAUDRATE)/(2L*12L))>0xFFL)
		#error Timer 0 reload value is incorrect because (SYSCLK/BAUDRATE)/(2L*12L) > 0xFF
	#endif
	SCON0 = 0x10;
	TH1 = 0x100-((SYSCLK/BAUDRATE)/(2L*12L));
	TL1 = TH1;      // Init Timer1
	TMOD &= ~0xf0;  // TMOD: timer 1 in 8-bit auto-reload
	TMOD |=  0x20;                       
	TI = 1;  // Indicate TX0 ready
  	
	ET1=0;     // Enable Timer1 interrupts
	TR1=1;     // Start Timer1
	
	EA=1; // Enable interrupts
	
	return 0;
	
}


void Timer0_ISR (void) interrupt INTERRUPT_TIMER0
{
	SFRPAGE=0x0;
	// Timer 0 in 16-bit mode doesn't have auto reload
	TMR0=0; // technically not necessary since it naturally overflows to 0
	TIMER_OUT_0=!TIMER_OUT_0;
}

void Timer1_ISR (void) interrupt INTERRUPT_TIMER1
{
	SFRPAGE=0x0;
	// Timer 1 in 16-bit mode doesn't have auto reload
	TMR1=0;
	TIMER_OUT_1=!TIMER_OUT_1;
}




void InitADC (void)
{
	SFRPAGE = 0x00;
	ADEN=0; // Disable ADC
	
	ADC0CN1=
		(0x2 << 6) | // 0x0: 10-bit, 0x1: 12-bit, 0x2: 14-bit
        (0x0 << 3) | // 0x0: No shift. 0x1: Shift right 1 bit. 0x2: Shift right 2 bits. 0x3: Shift right 3 bits.		
		(0x0 << 0) ; // Accumulate n conversions: 0x0: 1, 0x1:4, 0x2:8, 0x3:16, 0x4:32
	
	ADC0CF0=
	    ((SYSCLK/SARCLK) << 3) | // SAR Clock Divider. Max is 18MHz. Fsarclk = (Fadcclk) / (ADSC + 1)
		(0x0 << 2); // 0:SYSCLK ADCCLK = SYSCLK. 1:HFOSC0 ADCCLK = HFOSC0.
	
	ADC0CF1=
		(0 << 7)   | // 0: Disable low power mode. 1: Enable low power mode.
		(0x1E << 0); // Conversion Tracking Time. Tadtk = ADTK / (Fsarclk)
	
	ADC0CN0 =
		(0x0 << 7) | // ADEN. 0: Disable ADC0. 1: Enable ADC0.
		(0x0 << 6) | // IPOEN. 0: Keep ADC powered on when ADEN is 1. 1: Power down when ADC is idle.
		(0x0 << 5) | // ADINT. Set by hardware upon completion of a data conversion. Must be cleared by firmware.
		(0x0 << 4) | // ADBUSY. Writing 1 to this bit initiates an ADC conversion when ADCM = 000. This bit should not be polled to indicate when a conversion is complete. Instead, the ADINT bit should be used when polling for conversion completion.
		(0x0 << 3) | // ADWINT. Set by hardware when the contents of ADC0H:ADC0L fall within the window specified by ADC0GTH:ADC0GTL and ADC0LTH:ADC0LTL. Can trigger an interrupt. Must be cleared by firmware.
		(0x0 << 2) | // ADGN (Gain Control). 0x0: PGA gain=1. 0x1: PGA gain=0.75. 0x2: PGA gain=0.5. 0x3: PGA gain=0.25.
		(0x0 << 0) ; // TEMPE. 0: Disable the Temperature Sensor. 1: Enable the Temperature Sensor.

	ADC0CF2= 
		(0x0 << 7) | // GNDSL. 0: reference is the GND pin. 1: reference is the AGND pin.
		(0x1 << 5) | // REFSL. 0x0: VREF pin (external or on-chip). 0x1: VDD pin. 0x2: 1.8V. 0x3: internal voltage reference.
		(0x1F << 0); // ADPWR. Power Up Delay Time. Tpwrtime = ((4 * (ADPWR + 1)) + 2) / (Fadcclk)
	
	ADC0CN2 =
		(0x0 << 7) | // PACEN. 0x0: The ADC accumulator is over-written.  0x1: The ADC accumulator adds to results.
		(0x0 << 0) ; // ADCM. 0x0: ADBUSY, 0x1: TIMER0, 0x2: TIMER2, 0x3: TIMER3, 0x4: CNVSTR, 0x5: CEX5, 0x6: TIMER4, 0x7: TIMER5, 0x8: CLU0, 0x9: CLU1, 0xA: CLU2, 0xB: CLU3

	ADEN=1; // Enable ADC
}

// Uses Timer3 to delay <us> micro-seconds. 
void Timer3us(unsigned char us)
{
	unsigned char i;               // usec counter
	
	// The input for Timer 3 is selected as SYSCLK by setting T3ML (bit 6) of CKCON0:
	CKCON0|=0b_0100_0000;
	
	TMR3RL = (-(SYSCLK)/1000000L); // Set Timer3 to overflow in 1us.
	TMR3 = TMR3RL;                 // Initialize Timer3 for first overflow
	
	TMR3CN0 = 0x04;                 // Sart Timer3 and clear overflow flag
	for (i = 0; i < us; i++)       // Count <us> overflows
	{
		while (!(TMR3CN0 & 0x80));  // Wait for overflow
		TMR3CN0 &= ~(0x80);         // Clear overflow indicator
	}
	TMR3CN0 = 0 ;                   // Stop Timer3 and clear overflow flag
}

/* ADC code starts here */

void waitms (unsigned int ms)
{
	unsigned int j;
	unsigned char k;
	for(j=0; j<ms; j++)
		for (k=0; k<4; k++) Timer3us(250);
}

#define VDD 3.284 // The measured value of 3.3V in volts

void InitPinADC (unsigned char portno, unsigned char pinno)
{
	unsigned char mask;
	
	mask=1<<pinno;

	SFRPAGE = 0x20;
	switch (portno)
	{
		case 0:
			P0MDIN &= (~mask); // Set pin as analog input
			P0SKIP |= mask; // Skip Crossbar decoding for this pin
		break;
		case 1:
			P1MDIN &= (~mask); // Set pin as analog input
			P1SKIP |= mask; // Skip Crossbar decoding for this pin
		break;
		case 2:
			P2MDIN &= (~mask); // Set pin as analog input
			P2SKIP |= mask; // Skip Crossbar decoding for this pin
		break;
		default:
		break;
	}
	SFRPAGE = 0x00;
}

unsigned int ADC_at_Pin(unsigned char pin)
{
	ADC0MX = pin;   // Select input from pin
	ADINT = 0;
	ADBUSY = 1;     // Convert voltage at the pin
	while (!ADINT); // Wait for conversion to complete
	return (ADC0);
}

float Volts_at_Pin(unsigned char pin)
{
	 return ((ADC_at_Pin(pin)*VDD)/0b_0011_1111_1111_1111);
}

/**********************************************************************
********************************MAIN PROGRAM***************************
**********************************************************************/
 
void main (void)
{
	float v[4];

    float v1, v2;
	float v1max = 0;
	float v2max = 0;
	float T0, T1, f0, f1;

    waitms(500); 		// Give PuTTy a chance to start before sending
	printf("\x1b[2J"); 	// Clear screen using ANSI escape sequence.
	
	printf ("Lab 5: AC Peak and Phase\n"
	        "File: %s\n"
	        "Compiled: %s, %s\n\n",
	        __FILE__, __DATE__, __TIME__);
	
    InitPinADC(2, 1); // Configure P2.1 as analog input
	InitPinADC(2, 2); // Configure P2.2 as analog input
	InitPinADC(2, 3); // Configure P2.3 as analog input
	InitPinADC(2, 4); // Configure P2.4 as analog input
	InitPinADC(2, 5); // Configure P2.5 as analog input
    InitADC();

/**********************************************************************
******************************INFINITE LOOP****************************
**********************************************************************/
	while(1)
	{

		v1max = 0;

		v1 = Volts_at_Pin(CH1);

		while (v1 < 0.05)
		{
			v1 = Volts_at_Pin(CH1);
		}

		while (v1 > 0.05) // measure it at the pos hump
		{
			if (v1 > v1max)
				v1max = v1;

			v1 = Volts_at_Pin(CH1);
		}

		TR0 = 0;
		TMR0 = 0;
		TR0 = 1;

		while (v1 < 0.05)
		{
			v1 = Volts_at_Pin(CH1);
		}

		TR0 = 0;

		T0 = 2.0 * TMR0 * ((float)12 / SYSCLK);
		f0 = 1.0 / T0;

		printf("\x1b[H"); 
//
		

		printf("CH1 Period:    %7.5f s       \n", T0);
		printf("CH1 V_PEAK:      %7.5f V       \n", v1max);
		printf("CH1 V_RMS:	   %7.5f V       \n", v1max / 1.41421356237);
		printf("CH1 Frequency: %7.5f Hz      \n", f0);

		waitms(500);
	 
		/*
		v[0] = Volts_at_Pin(QFP32_MUX_P2_2);
		v[1] = Volts_at_Pin(QFP32_MUX_P2_3);
		v[2] = Volts_at_Pin(QFP32_MUX_P2_4);
		v[3] = Volts_at_Pin(QFP32_MUX_P2_5);

		printf ("V@P2.2=%7.5fV, V@P2.3=%7.5fV, V@P2.4=%7.5fV, V@P2.5=%7.5fV\r", v[0], v[1], v[2], v[3]);
		printf("CH1 period = %7.5f seconds, CH1 max = %7.5fV\r", T0, v1max);
		waitms(500);
		printf("CH2 period = %7.5f seconds, CH2 max = %7.5fV\r", T1, v2max);
		waitms(500);
		*/
	}
}	

