// ADC.c:  AC Peak and Phase Measurement with LCD display
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
#define SARCLK   18000000L

#define CH1 QFP32_MUX_P2_1  // ADC input on P2.1
#define CH2 QFP32_MUX_P2_2  // ADC input on P2.2

#define SYSCLK       72000000L
#define TIMER_0_FREQ 1000L
#define TIMER_1_FREQ 2000L

// ----------------------------------------------------------------
// LCD pin definitions (from working capacitance meter — DO NOT CHANGE)
// P2.0 = LCD_E, P1.7 = LCD_RS, P1.0-P1.3 = LCD data
// NOTE: These pins were previously TIMER_OUT_0 and TIMER_OUT_1.
//       The square wave output has been removed from the ISRs
//       to prevent LCD_E and LCD_RS from being corrupted.
// ----------------------------------------------------------------
#define LCD_RS P1_7
#define LCD_E  P2_0
#define LCD_D4 P1_3
#define LCD_D5 P1_2
#define LCD_D6 P1_1
#define LCD_D7 P1_0
#define CHARS_PER_LINE 16

// Detection thresholds
// CH1 = 0.05V  (large ~2.1V peak signal)
// CH2 = 0.02V  (smaller ~0.77V peak signal, lower threshold catches zero-crossings reliably)
#define THRESH1 0.05
#define THRESH2 0.02

#define VDD 3.3


char _c51_external_startup (void)
{
	SFRPAGE = 0x00;
	WDTCN = 0xDE;
	WDTCN = 0xAD;

	VDM0CN = 0x80;
	RSTSRC = 0x02|0x04;

	#if (SYSCLK == 48000000L)
		SFRPAGE = 0x10; PFE0CN = 0x10; SFRPAGE = 0x00;
	#elif (SYSCLK == 72000000L)
		SFRPAGE = 0x10; PFE0CN = 0x20; SFRPAGE = 0x00;
	#endif

	#if (SYSCLK == 72000000L)
		CLKSEL = 0x00; CLKSEL = 0x00;
		while ((CLKSEL & 0x80) == 0);
		CLKSEL = 0x03; CLKSEL = 0x03;
		while ((CLKSEL & 0x80) == 0);
	#else
		#error SYSCLK must be 72000000L
	#endif

	// P0: UART TX push-pull
	P0MDOUT |= 0x10;

	// P1: LCD pins (RS=P1.7, D4-D7=P1.0-P1.3) all push-pull
	P1MDOUT |= 0b_1000_1111;

	// P2: LCD_E (P2.0) push-pull; P2.1 and P2.2 are ADC analog inputs (set below)
	P2MDOUT |= 0b_0000_0001;

	// UART on P0.4/P0.5
	XBR0 = 0x01;
	XBR1 = 0x00;
	XBR2 = 0x40;

	// Timer 0 — SYSCLK/12, 16-bit, used for CH1 valley timing
	// ISR is enabled/disabled around the measurement window only.
	// NO pin toggling in ISR — P2.0 is LCD_E and must not be disturbed.
	TR0 = 0; TF0 = 0;
	CKCON0 &= 0b_11001111; // Timer0: SYSCLK/12
	TMOD &= 0xf0;
	TMOD |= 0x01;          // Mode 1: 16-bit
	TMR0 = 0;
	ET0 = 0;               // Keep disabled by default; enabled only during CH1 measurement

	// Timer 1 — UART baud rate (8-bit auto-reload)
	TR1 = 0; TF1 = 0;
	SCON0 = 0x10;
	TH1 = 0x100 - ((SYSCLK/BAUDRATE)/(2L*12L));
	TL1 = TH1;
	TMOD &= ~0xf0;
	TMOD |= 0x20;
	TI = 1;
	ET1 = 0;
	TR1 = 1;

	// Timer 2 — SYSCLK/12, 16-bit, dedicated to CH2 full-period measurement
	// Overflow flag TF2H polled manually — no ISR needed
	TMR2CN0 = 0x00;
	TMR2RL  = 0x0000;
	TMR2    = 0x0000;
	ET2     = 0;

	EA = 1;
	return 0;
}


// Timer0 ISR — kept minimal, NO pin toggling.
// P2.0 is LCD_E: toggling it here would corrupt the display.
void Timer0_ISR (void) interrupt INTERRUPT_TIMER0
{
	SFRPAGE = 0x0;
	TMR0 = 0;
	// Square wave output intentionally removed — P2.0/P1.7 are LCD pins
}

// Timer1 ISR — not used (Timer1 in auto-reload mode for UART)
void Timer1_ISR (void) interrupt INTERRUPT_TIMER1
{
	SFRPAGE = 0x0;
	TMR1 = 0;
	// Square wave output intentionally removed — P1.7 is LCD_RS
}


// ----------------------------------------------------------------
// LCD functions (ported from working capacitance meter)
// ----------------------------------------------------------------

void Timer3us (unsigned char us)
{
	unsigned char i;
	CKCON0 |= 0b_0100_0000;
	TMR3RL   = (-(SYSCLK)/1000000L);
	TMR3     = TMR3RL;
	TMR3CN0  = 0x04;
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
	for (j = 0; j < ms; j++)
		for (k = 0; k < 4; k++) Timer3us(250);
}

void LCD_pulse (void)
{
	LCD_E = 1;
	Timer3us(40);
	LCD_E = 0;
}

void LCD_byte (unsigned char x)
{
	ACC = x;
	LCD_D7 = ACC_7; LCD_D6 = ACC_6; LCD_D5 = ACC_5; LCD_D4 = ACC_4;
	LCD_pulse();
	Timer3us(40);
	ACC = x;
	LCD_D7 = ACC_3; LCD_D6 = ACC_2; LCD_D5 = ACC_1; LCD_D4 = ACC_0;
	LCD_pulse();
}

void WriteData (unsigned char x)
{
	LCD_RS = 1;
	LCD_byte(x);
	waitms(2);
}

void WriteCommand (unsigned char x)
{
	LCD_RS = 0;
	LCD_byte(x);
	waitms(5);
}

void LCD_4BIT (void)
{
	LCD_E = 0;
	waitms(20);
	WriteCommand(0x33);
	WriteCommand(0x33);
	WriteCommand(0x32); // switch to 4-bit mode
	WriteCommand(0x28); // 2-line, 5x8 font
	WriteCommand(0x0C); // display on, cursor off
	WriteCommand(0x01); // clear display
	waitms(20);
}

void LCDprint (char *string, unsigned char line, bit clear)
{
	int j;
	WriteCommand(line == 2 ? 0xC0 : 0x80);
	waitms(5);
	for (j = 0; string[j] != 0; j++) WriteData(string[j]);
	if (clear) for (; j < CHARS_PER_LINE; j++) WriteData(' ');
}


// ----------------------------------------------------------------
// ADC functions
// ----------------------------------------------------------------

void InitADC (void)
{
	SFRPAGE = 0x00;
	ADEN = 0;

	ADC0CN1 =
		(0x2 << 6) | // 14-bit
		(0x0 << 3) | // no shift
		(0x0 << 0) ; // 1 conversion

	ADC0CF0 = ((SYSCLK/SARCLK) << 3) | (0x0 << 2);

	ADC0CF1 = (0 << 7) | (0x1E << 0);

	ADC0CN0 = 0x00;

	ADC0CF2 =
		(0x0 << 7) |
		(0x1 << 5) | // reference = VDD
		(0x1F << 0);

	ADC0CN2 = (0x0 << 7) | (0x0 << 0);

	ADEN = 1;
}

void InitPinADC (unsigned char portno, unsigned char pinno)
{
	unsigned char mask = 1 << pinno;
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

unsigned int ADC_at_Pin (unsigned char pin)
{
	ADC0MX = pin;
	ADINT  = 0;
	ADBUSY = 1;
	while (!ADINT);
	return (ADC0);
}

float Volts_at_Pin (unsigned char pin)
{
	return ((ADC_at_Pin(pin) * VDD) / 0b_0011_1111_1111_1111);
}


/**********************************************************************
 *                         MAIN PROGRAM
 **********************************************************************/

void main (void)
{
	float v1, v2;
	float v1max, v2max;
	float T0, T1, f0, f1;
	unsigned int  tmr2_val;
	unsigned char overflow2;
	unsigned long total_ticks;

	// LCD line buffers (16 chars + null terminator)
	char lcd1[17];
	char lcd2[17];

	waitms(500);
	printf("\x1b[2J");
	printf("Lab 5: AC Peak and Phase\n"
	       "File: %s\n"
	       "Compiled: %s, %s\n\n",
	       __FILE__, __DATE__, __TIME__);

	InitPinADC(2, 1); // P2.1 = CH1
	InitPinADC(2, 2); // P2.2 = CH2
	InitPinADC(2, 3);
	InitPinADC(2, 4);
	InitPinADC(2, 5);
	InitADC();

	LCD_4BIT();
	LCDprint("Lab5: AC Signal", 1, 1);
	LCDprint("Initializing...", 2, 1);
	waitms(1000);

/**********************************************************************
 *                         INFINITE LOOP
 **********************************************************************/
	while (1)
	{
		/***************************************************************
		 * CHANNEL 1: time the valley (flat 0V region)
		 *
		 * Half-wave rectified signal is clamped at 0V for exactly T/2.
		 * Timing this flat region is immune to sine-arch threshold
		 * clipping. Valley = T/2, so period T = 2 * valley_time.
		 ***************************************************************/

		// Wait until both signals are in their valleys simultaneously
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

		// Time the valley — disable ET0 so the Timer0 ISR cannot
		// reset TMR0 (or toggle P2.0=LCD_E) mid-measurement
		ET0 = 0;
		TR0 = 0; TMR0 = 0; TR0 = 1;

		v1 = Volts_at_Pin(CH1);
		while (v1 < THRESH1)
			v1 = Volts_at_Pin(CH1);

		TR0 = 0;
		// ET0 stays 0 — no square wave output needed on this pin (it's LCD_E)

		T0 = 2.0 * (float)TMR0 * ((float)12 / SYSCLK);
		f0 = 1.0 / T0;


		/***************************************************************
		 * CHANNEL 2: single-pass full period measurement
		 *
		 * Measures rising edge -> next rising edge so sine-arch
		 * clipping cancels symmetrically on both sides.
		 *
		 * THRESH2 = 0.02V (lower than CH1's 0.05V) to reliably detect
		 * zero-crossings on the smaller CH2 signal.
		 *
		 * Overflow counter extends Timer2's 16-bit range from ~91Hz
		 * minimum down to ~0.4Hz (same technique as prof's code).
		 ***************************************************************/

		v2max = 0;

		// Make sure we start from a valley
		v2 = Volts_at_Pin(CH2);
		while (v2 > THRESH2) v2 = Volts_at_Pin(CH2);

		// Wait for FIRST rising edge
		while (Volts_at_Pin(CH2) < THRESH2);

		// Start Timer2 at the rising edge
		TMR2H   = 0;
		TMR2L   = 0;
		TF2H    = 0;
		overflow2 = 0;
		TR2 = 1;

		// Ride the hump, collect peak, count overflows
		v2 = Volts_at_Pin(CH2);
		while (v2 > THRESH2)
		{
			if (TF2H) { TF2H = 0; overflow2++; }
			if (v2 > v2max) v2max = v2;
			v2 = Volts_at_Pin(CH2);
		}

		// Wait through the valley, keep counting overflows
		v2 = Volts_at_Pin(CH2);
		while (v2 < THRESH2)
		{
			if (TF2H) { TF2H = 0; overflow2++; }
			v2 = Volts_at_Pin(CH2);
		}

		// Stop at the NEXT rising edge — exactly one full period elapsed
		TR2 = 0;

		// 32-bit tick count (same formula as prof's overflow_count * 0x10000 + timer)
		tmr2_val    = ((unsigned int)TMR2H << 8) | (unsigned int)TMR2L;
		total_ticks = (unsigned long)overflow2 * 65536UL + (unsigned long)tmr2_val;
		T1 = (float)total_ticks * ((float)12 / SYSCLK); // full period, no x2
		f1 = 1.0 / T1;


		/***************************************************************
		 * SERIAL OUTPUT (PuTTY)
		 ***************************************************************/
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

		/***************************************************************
		 * LCD OUTPUT
		 * Line 1: CH1 frequency and RMS voltage
		 * Line 2: CH2 frequency and RMS voltage
		 * Format: "CH1 59.9Hz 1.51V"  (exactly 16 chars)
		 *         "CH2 59.9Hz 0.75V"
		 ***************************************************************/
		sprintf(lcd1, "CH1%5.1fHz%5.2fV", f0, v1max / 1.41421356237);
		sprintf(lcd2, "CH2%5.1fHz%5.2fV", f1, v2max / 1.41421356237);
		LCDprint(lcd1, 1, 1);
		LCDprint(lcd2, 2, 1);

		waitms(500);
	}
}
