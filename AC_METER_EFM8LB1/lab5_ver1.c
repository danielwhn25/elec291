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
#define SARCLK 18000000L
#define CH1 QFP32_MUX_P2_1
#define CH2 QFP32_MUX_P2_2

#define SYSCLK 72000000L
#define TIMER_0_FREQ 1000L
#define TIMER_1_FREQ 2000L

#define TIMER_OUT_0 P2_0
#define TIMER_OUT_1 P1_7
#define MAIN_OUT    P0_1

#define THRESH1 0.02
#define THRESH2 0.02


char _c51_external_startup (void)
{
	// Disable Watchdog with key sequence
	SFRPAGE = 0x00;
	WDTCN = 0xDE;
	WDTCN = 0xAD;
  
	VDM0CN=0x80;
	RSTSRC=0x02|0x04;

	#if (SYSCLK == 48000000L)	
		SFRPAGE = 0x10;
		PFE0CN  = 0x10;
		SFRPAGE = 0x00;
	#elif (SYSCLK == 72000000L)
		SFRPAGE = 0x10;
		PFE0CN  = 0x20;
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
		CLKSEL = 0x00;
		CLKSEL = 0x00;
		while ((CLKSEL & 0x80) == 0);
		CLKSEL = 0x07;
		CLKSEL = 0x07;
		while ((CLKSEL & 0x80) == 0);
	#elif (SYSCLK == 72000000L)
		CLKSEL = 0x00;
		CLKSEL = 0x00;
		while ((CLKSEL & 0x80) == 0);
		CLKSEL = 0x03;
		CLKSEL = 0x03;
		while ((CLKSEL & 0x80) == 0);
	#else
		#error SYSCLK must be either 12250000L, 24500000L, 48000000L, or 72000000L
	#endif

	P0MDOUT|=0b_1100_0010;
	P1MDOUT|=0b_1111_1111;
	P2MDOUT|=0b_0000_0001;
	
	P0MDOUT |= 0x10;
	XBR0     = 0x01;
	XBR1     = 0x00;
	XBR2     = 0x40;

	// Timer 0 - SYSCLK/12, 16-bit, CH1 valley measurement + square wave on TIMER_OUT_0
	TR0=0;
	TF0=0;
	CKCON0 &= 0b_11001111; // Timer0 uses SYSCLK/12
	TMOD&=0xf0;
	TMOD|=0x01; // Mode 1: 16-bit
	TMR0=0;
	ET0=1;
	TR0=1;

	// Timer 1 - UART baud rate generator (8-bit auto-reload)
	TR1=0;
	TF1=0;
	SCON0 = 0x10;
	TH1 = 0x100-((SYSCLK/BAUDRATE)/(2L*12L));
	TL1 = TH1;
	TMOD &= ~0xf0;
	TMOD |=  0x20;
	TI = 1;
	ET1=0;
	TR1=1;

	// Timer 2 - SYSCLK/12, 16-bit, dedicated to CH2 full-period measurement
	// Overflow flag TF2H polled manually so no ISR needed
	TMR2CN0 = 0x00;
	TMR2RL  = 0x0000;
	TMR2    = 0x0000;
	ET2     = 0;

	EA=1;
	
	return 0;
}


// Timer0 ISR: square wave on TIMER_OUT_0
// ET0 is disabled during CH1 measurement to prevent TMR0 corruption
void Timer0_ISR (void) interrupt INTERRUPT_TIMER0
{
	SFRPAGE=0x0;
	TMR0=0;
	TIMER_OUT_0=!TIMER_OUT_0;
}

void Timer1_ISR (void) interrupt INTERRUPT_TIMER1
{
	SFRPAGE=0x0;
	TMR1=0;
	TIMER_OUT_1=!TIMER_OUT_1;
}


void InitADC (void)
{
	SFRPAGE = 0x00;
	ADEN=0;
	
	ADC0CN1=
		(0x2 << 6) | // 14-bit
        (0x0 << 3) | // No shift
		(0x0 << 0) ; // 1 conversion

	ADC0CF0=
	    ((SYSCLK/SARCLK) << 3) |
		(0x0 << 2);
	
	ADC0CF1=
		(0 << 7)   |
		(0x1E << 0);
	
	ADC0CN0 =
		(0x0 << 7) |
		(0x0 << 6) |
		(0x0 << 5) |
		(0x0 << 4) |
		(0x0 << 3) |
		(0x0 << 2) |
		(0x0 << 0) ;

	ADC0CF2= 
		(0x0 << 7) |
		(0x1 << 5) | // Reference = VDD
		(0x1F << 0);
	
	ADC0CN2 =
		(0x0 << 7) |
		(0x0 << 0) ;

	ADEN=1;
}


void Timer3us(unsigned char us)
{
	unsigned char i;
	CKCON0|=0b_0100_0000;
	TMR3RL = (-(SYSCLK)/1000000L);
	TMR3 = TMR3RL;
	TMR3CN0 = 0x04;
	for (i = 0; i < us; i++)
	{
		while (!(TMR3CN0 & 0x80));
		TMR3CN0 &= ~(0x80);
	}
	TMR3CN0 = 0;
}

void waitms (unsigned int ms)
{
	unsigned int j;
	unsigned char k;
	for(j=0; j<ms; j++)
		for (k=0; k<4; k++) Timer3us(250);
}

#define VDD 3.3

void InitPinADC (unsigned char portno, unsigned char pinno)
{
	unsigned char mask;
	mask=1<<pinno;
	SFRPAGE = 0x20;
	switch (portno)
	{
		case 0: P0MDIN &= (~mask); P0SKIP |= mask; break;
		case 1: P1MDIN &= (~mask); P1SKIP |= mask; break;
		case 2: P2MDIN &= (~mask); P2SKIP |= mask; break;
		default: break;
	}
	SFRPAGE = 0x00;
}

unsigned int ADC_at_Pin(unsigned char pin)
{
	ADC0MX = pin;
	ADINT = 0;
	ADBUSY = 1;
	while (!ADINT);
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
	float v1, v2;
	float v1max, v2max;
	float T0, T1, f0, f1;
	unsigned int tmr2_val;
	unsigned char overflow2;
	unsigned long total_ticks;

	waitms(500);
	printf("\x1b[2J");
	printf("Lab 5: AC Peak and Phase\n"
	       "File: %s\n"
	       "Compiled: %s, %s\n\n",
	       __FILE__, __DATE__, __TIME__);
	
	InitPinADC(2, 1);
	InitPinADC(2, 2);
	InitPinADC(2, 3);
	InitPinADC(2, 4);
	InitPinADC(2, 5);
	InitADC();

/**********************************************************************
******************************INFINITE LOOP****************************
**********************************************************************/
	while(1)
	{
		// Measure channel 1 during the half-period when it's in the valley
		// wait until both signals are in their valleys
		while (Volts_at_Pin(CH1) > THRESH1 || Volts_at_Pin(CH2) > THRESH2);

		v1max = 0;

		// Wait for CH1 rising edge
		v1 = Volts_at_Pin(CH1);
		while (v1 < THRESH1)
			v1 = Volts_at_Pin(CH1);

		// Ride the hump, collect peak
		while (v1 > THRESH1)
		{
			if (v1 > v1max) v1max = v1;
			v1 = Volts_at_Pin(CH1);
		}

		// Disable ET0 so Timer0 ISR cannot reset TMR0 mid-measurement
		ET0 = 0;
		TR0 = 0;
		TMR0 = 0;
		TR0 = 1;

		// wait for next rising edge ==> T/2 elapsed
		v1 = Volts_at_Pin(CH1);
		while (v1 < THRESH1)
			v1 = Volts_at_Pin(CH1);

		TR0 = 0;
		ET0 = 1; // Re-enable Timer0 ISR for square wave

		T0 = 2.0 * (float)TMR0 * ((float)12 / SYSCLK);
		f0 = 1.0 / T0;

		/**********************************************************************************	
		// repeat for channel 2, but measure the full period from rising edge to rising edge
		***********************************************************************************/	

		v2max = 0;


		v2 = Volts_at_Pin(CH2);
		while (v2 > THRESH2) v2 = Volts_at_Pin(CH2);

		// wait for first rising edge
		while (Volts_at_Pin(CH2) < THRESH2);

		// start timer2 and count overflows manually
		TMR2H = 0;
		TMR2L = 0;
		TF2H = 0;
		overflow2 = 0;
		TR2 = 1;

		// ride the wave + collect peak + count overflows
		v2 = Volts_at_Pin(CH2);
		while (v2 > THRESH2)
		{
			if (TF2H) { TF2H = 0; overflow2++; }
			if (v2 > v2max) v2max = v2;
			v2 = Volts_at_Pin(CH2);
		}

		// wait through the valley, keep counting overflows
		v2 = Volts_at_Pin(CH2);
		while (v2 < THRESH2)
		{
			if (TF2H) { TF2H = 0; overflow2++; }
			v2 = Volts_at_Pin(CH2);
		}

		// stop at the NEXT rising edge 
		TR2 = 0;

		// Build 32-bit tick count from overflow count + remaining timer value
		// (same formula as prof's: overflow_count * 0x10000 + timer value)
		tmr2_val = ((unsigned int)TMR2H << 8) | (unsigned int)TMR2L;
		total_ticks = (unsigned long)overflow2 * 65536UL + (unsigned long)tmr2_val;
		T1 = (float)total_ticks * ((float)12 / SYSCLK); // Convert ticks to seconds
		f1 = 1.0 / T1;


		printf("\x1b[H");
		printf("CH1 Period:    %7.5f s       \n", T0);
		printf("CH1 V_PEAK:    %7.5f V       \n", v1max);
		printf("CH1 V_RMS:     %7.5f V       \n", v1max / 1.41421356237);
		printf("CH1 Frequency: %7.5f Hz      \n\n", f0);
		printf("============================\n\n");
		printf("CH2 Period:    %7.5f s       \n", T1);
		printf("CH2 V_PEAK:    %7.5f V       \n", v2max);
		printf("CH2 V_RMS:     %7.5f V       \n", v2max / 1.41421356237);
		printf("CH2 Frequency: %7.5f Hz      \n", f1);

		waitms(500);
	}
}