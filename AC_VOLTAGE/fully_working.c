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
#define CH2 QFP32_MUX_P2_2  // ADC input on P2.2 (REFERENCE signal)

#define SYSCLK       72000000L
#define TIMER_0_FREQ 1000L
#define TIMER_1_FREQ 2000L

// ----------------------------------------------------------------
// LCD pin definitions (from working capacitance meter - DO NOT CHANGE)
// P2.0 = LCD_E, P1.7 = LCD_RS, P1.0-P1.3 = LCD data
// ----------------------------------------------------------------
#define LCD_RS P1_7
#define LCD_E  P2_0
#define LCD_D4 P1_3
#define LCD_D5 P1_2
#define LCD_D6 P1_1
#define LCD_D7 P1_0
#define CHARS_PER_LINE 16

#define THRESH1 0.05  // CH1 threshold (large ~2.1V peak)
#define THRESH2 0.02  // CH2 threshold (smaller ~0.77V peak)
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

	P0MDOUT |= 0x10;           // UART TX push-pull
	P1MDOUT |= 0b_1000_1111;   // LCD: RS=P1.7, D4-D7=P1.0-P1.3
	P2MDOUT |= 0b_0000_0001;   // LCD: E=P2.0

	XBR0 = 0x01;  // UART on P0.4/P0.5
	XBR1 = 0x00;
	XBR2 = 0x40;

	// Timer 0 - SYSCLK/12, 16-bit
	// Used for: (1) CH1 valley timing, (2) phase delta-t timing
	// NO pin toggling in ISR - P2.0 is LCD_E
	TR0 = 0; TF0 = 0;
	CKCON0 &= 0b_11001111; // SYSCLK/12
	TMOD &= 0xf0;
	TMOD |= 0x01;          // Mode 1: 16-bit
	TMR0 = 0;
	ET0 = 0;               // Disabled by default

	// Timer 1 - UART baud rate (8-bit auto-reload)
	TR1 = 0; TF1 = 0;
	SCON0 = 0x10;
	TH1 = 0x100 - ((SYSCLK/BAUDRATE)/(2L*12L));
	TL1 = TH1;
	TMOD &= ~0xf0;
	TMOD |= 0x20;
	TI = 1;
	ET1 = 0;
	TR1 = 1;

	// Timer 2 - SYSCLK/12, 16-bit
	// Used for phase delta-t measurement (CH1 rise -> CH2 rise)
	TMR2CN0 = 0x00;
	TMR2RL  = 0x0000;
	TMR2    = 0x0000;
	ET2     = 0;

	EA = 1;
	return 0;
}


void Timer0_ISR (void) interrupt INTERRUPT_TIMER0
{
	SFRPAGE = 0x0;
	TMR0 = 0;
	// No pin toggling - P2.0=LCD_E, P1.7=LCD_RS
}

void Timer1_ISR (void) interrupt INTERRUPT_TIMER1
{
	SFRPAGE = 0x0;
	TMR1 = 0;
}


// ----------------------------------------------------------------
// LCD functions
// ----------------------------------------------------------------

void Timer3us (unsigned char us)
{
	unsigned char i;
	CKCON0 |= 0b_0100_0000;
	TMR3RL  = (-(SYSCLK)/1000000L);
	TMR3    = TMR3RL;
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
	WriteCommand(0x32);
	WriteCommand(0x28);
	WriteCommand(0x0C);
	WriteCommand(0x01);
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
		(0x0 << 3) |
		(0x0 << 0) ;

	ADC0CF0 = ((SYSCLK/SARCLK) << 3) | (0x0 << 2);
	ADC0CF1 = (0 << 7) | (0x1E << 0);
	ADC0CN0 = 0x00;

	ADC0CF2 =
		(0x0 << 7) |
		(0x1 << 5) | // reference = VDD
		(0x1F << 0);

	ADC0CN2 = 0x00;
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
 *
 * PHASE CALCULATION OVERVIEW:
 *
 * CH2 is the REFERENCE signal. Phase is reported as how much CH1
 * leads (+) or lags (-) CH2.
 *
 *   CH1 rises --> START phase timer --> CH2 rises --> STOP = delta_t
 *   phase (deg) = (delta_t / T0) * 360
 *   Normalize:  if phase > 180  -> phase -= 360  (CH1 lags CH2)
 *               if phase < -180 -> phase += 360
 *
 * MEASUREMENT SEQUENCE each loop:
 *   1. Wait for both signals in valley (synchronized start)
 *   2. CH1 hump  -> collect v1max
 *   3. CH1 valley -> Timer0 -> T0 = 2 * valley_time
 *   4. CH1 rises again -> START Timer2 (phase timer)
 *   5. CH2 rises       -> STOP  Timer2 -> delta_t -> phase
 *   6. Ride CH2 hump   -> collect v2max
 *   (T1 = T0 since both channels are the same frequency)
 *
 **********************************************************************/

void main (void)
{
	float v1, v2;
	float v1max, v2max;
	float v1rms, v2rms;
	float T0, f0;
	float delta_t, phase;
	unsigned int  tmr2_val;
	unsigned char overflow2;
	unsigned long dt_ticks;
	char lcd1[17];
	char lcd2[17];

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

	LCD_4BIT();
	LCDprint("Lab5: AC Signal", 1, 1);
	LCDprint("Initializing...", 2, 1);
	waitms(1000);

	while (1)
	{
		/***************************************************************
		 * STEP 1: Synchronize — wait for both signals in their valleys
		 ***************************************************************/
		while (Volts_at_Pin(CH1) > THRESH1 || Volts_at_Pin(CH2) > THRESH2);

		/***************************************************************
		 * STEP 2: Collect CH1 peak during its hump
		 ***************************************************************/
		v1max = 0;

		v1 = Volts_at_Pin(CH1);
		while (v1 < THRESH1)
			v1 = Volts_at_Pin(CH1);        // wait for CH1 rising edge

		while (v1 > THRESH1)               // ride the hump
		{
			if (v1 > v1max) v1max = v1;
			v1 = Volts_at_Pin(CH1);
		}

		/***************************************************************
		 * STEP 3: Time the CH1 valley -> get T0
		 *
		 * The valley is the flat 0V half-period, immune to sine-arch
		 * clipping. T0 = 2 * valley_time.
		 * ET0 disabled so ISR can't reset TMR0 mid-measurement.
		 ***************************************************************/
		ET0 = 0;
		TR0 = 0; TMR0 = 0; TR0 = 1;

		v1 = Volts_at_Pin(CH1);
		while (v1 < THRESH1)               // wait for CH1 next rising edge
			v1 = Volts_at_Pin(CH1);

		TR0 = 0;
		// CH1 just rose — this exact moment is the start of the phase measurement

		T0 = 2.0 * (float)TMR0 * ((float)12 / SYSCLK);
		f0 = 1.0 / T0;

		/***************************************************************
		 * STEP 4 & 5: Measure phase
		 *
		 * Timer2 starts NOW at CH1's rising edge.
		 * We wait for CH2 to rise and stop Timer2.
		 * delta_t = time from CH1 rise to CH2 rise.
		 *
		 * Overflow counter handles cases where CH2 lags CH1 by more
		 * than ~10.9ms (Timer2's 16-bit limit at 6MHz).
		 *
		 * Sign convention:
		 *   delta_t small  -> CH1 leads CH2 -> phase POSITIVE
		 *   delta_t large  -> CH1 lags  CH2 -> phase NEGATIVE
		 *                     (detected by normalizing > 180 -> -= 360)
		 ***************************************************************/
		TMR2H = 0;
		TMR2L = 0;
		TF2H  = 0;
		overflow2 = 0;
		TR2 = 1;  // START phase timer at CH1 rising edge

		// Wait for CH2 rising edge, counting overflows
		v2 = Volts_at_Pin(CH2);
		while (v2 < THRESH2)
		{
			if (TF2H) { TF2H = 0; overflow2++; }
			v2 = Volts_at_Pin(CH2);
		}
		TR2 = 0;  // STOP at CH2 rising edge

		tmr2_val = ((unsigned int)TMR2H << 8) | (unsigned int)TMR2L;
		dt_ticks = (unsigned long)overflow2 * 65536UL + (unsigned long)tmr2_val;
		delta_t  = (float)dt_ticks * ((float)12 / SYSCLK);

		phase = (delta_t / T0) * 360.0;

		// Normalize to -180 to +180
		// > 180 means CH1 appears to "lead" by a lot but actually lags
		if (phase > 180.0)  phase -= 360.0;
		if (phase < -180.0) phase += 360.0;

		/***************************************************************
		 * STEP 6: Collect CH2 peak
		 *
		 * We're sitting right at CH2's rising edge from step 5.
		 * Just ride the hump to get v2max — no extra sync needed.
		 ***************************************************************/
		v2max = 0;
		v2 = Volts_at_Pin(CH2);
		while (v2 > THRESH2)
		{
			if (v2 > v2max) v2max = v2;
			v2 = Volts_at_Pin(CH2);
		}

		// RMS values
		v1rms = v1max / 1.41421356237;
		v2rms = v2max / 1.41421356237;

		/***************************************************************
		 * SERIAL OUTPUT (PuTTY)
		 ***************************************************************/
		printf("\x1b[H");
		printf("CH1 (measured):\n");
		printf("  Period:    %7.5f s       \n", T0);
		printf("  Frequency: %7.3f Hz      \n", f0);
		printf("  V_PEAK:    %7.4f V       \n", v1max);
		printf("  V_RMS:     %7.4f V       \n", v1rms);
		printf("  Phase:     %+7.2f deg    \n\n", phase);
		printf("CH2 (reference):\n");
		printf("  Frequency: %7.3f Hz      \n", f0);
		printf("  V_PEAK:    %7.4f V       \n", v2max);
		printf("  V_RMS:     %7.4f V       \n", v2rms);
		printf("  Phase:      0.00 deg (ref)\n");

		/***************************************************************
		 * LCD OUTPUT (16 chars per line, no CH1/CH2 labels)
		 *
		 * Line 1 (CH1): "60.0Hz 1.51V+30d"
		 *                 freq   Vrms  phase
		 * Line 2 (CH2): "60.0Hz 0.75V ref"
		 *                 freq   Vrms  (reference)
		 *
		 * Format breakdown (16 chars exactly):
		 *  %4.0fHz  = 6 chars  (e.g. " 60Hz" — space padded)
		 *  %5.2fV   = 6 chars  (e.g. " 1.51V")
		 *  %+4.0fd  = 4 chars  (e.g. "+30d" or "-15d") for line 1
		 *  " ref"   = 4 chars                           for line 2
		 ***************************************************************/
		sprintf(lcd1, "%4.0fHz%5.2fV%+4.0fd", f0, v1rms, phase);
		sprintf(lcd2, "%4.0fHz%5.2fV ref",     f0, v2rms);
		LCDprint(lcd1, 1, 1);
		LCDprint(lcd2, 2, 1);

		waitms(500);
	}
}
