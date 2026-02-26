// CapMeter_LM555_Units_NoSTDIO.c
//  ~C51~
//
// LM555 astable -> EFM8 measures frequency on T0 (P0.0) -> computes C
// Ohmmeter on P1.4 (ADC0.10) using 950 ohm high-side resistor.
// Continuity Tester on P0.2 (Probe) and P0.1 (Buzzer).

#include <EFM8LB1.h>

#define SYSCLK 72000000L

// 555 Resistors:
#define RA_OHM_F 1647.0f
#define RB_OHM_F 1648.0f

// Ohmmeter Resistor:
#define R_KNOWN_OHMS 950.0f
#define ADC_MAX 4095.0f // 14-bit mode max. Change to 4095.0f if using 12-bit mode.

// LCD pins (lab mapping)
#define LCD_RS P1_7
#define LCD_E  P2_0
#define LCD_D4 P1_3
#define LCD_D5 P1_2
#define LCD_D6 P1_1
#define LCD_D7 P1_0

// Unit switches (active-low) -- your wiring
#define SW_F   P3_1   // SW1
#define SW_mF  P3_0   // SW2
#define SW_uF  P2_6   // SW3
#define SW_nF  P2_5   // SW4

// Continuity Tester Pins
#define BUZZER P0_1   // Buzzer Output
#define PROBE  P0_2   // Continuity Probe Input

// "Cap present" frequency window
#define F_MIN_HZ   5UL
#define F_MAX_HZ   500000UL

static unsigned char overflow_count;

// 16-char fixed messages in CODE memory (exactly 16 chars each)
code char MSG_PLACE1[17] = "no              ";
code char MSG_PLACE2[17] = "cap             ";
code char MSG_ERR1  [17] = "Unit switch err ";
code char MSG_ERR2  [17] = "Only 1 ON       ";

typedef enum {
    UNIT_AUTO = 0,
    UNIT_F,
    UNIT_mF,
    UNIT_uF,
    UNIT_nF,
    UNIT_ERR
} unit_t;

char _c51_external_startup (void)
{
    // Disable Watchdog
    SFRPAGE = 0x00;
    WDTCN = 0xDE;
    WDTCN = 0xAD;

    VDM0CN |= 0x80;
    RSTSRC = 0x02;

    // 72 MHz
    CLKSEL = 0x00; CLKSEL = 0x00;
    while ((CLKSEL & 0x80) == 0);
    SFRPAGE = 0x10; PFE0CN = 0x20; SFRPAGE = 0x00;
    CLKSEL = 0x03; CLKSEL = 0x03;
    while ((CLKSEL & 0x80) == 0);

    // LCD pins push-pull
    P1MDOUT |= 0b10001111; // P1.0-1.3 and P1.7
    P2MDOUT |= 0b00000001; // P2.0

    // Switch pins as digital inputs
    P2MDIN |= 0b01100000;  // P2.5, P2.6 digital
    P3MDIN |= 0b00000011;  // P3.0, P3.1 digital
    P3MDOUT &= ~0b00000011; // keep P3.0, P3.1 as inputs

    // --- Continuity & T0 Setup ---
    // P0.0 (T0), P0.1 (Buzzer), P0.2 (Probe) set to Digital
    P0MDIN |= 0b00000111; 
    
    // P0.1 Push-Pull for Buzzer driving
    P0MDOUT |= 0b00000010; 
    
    // P0.2 Open-Drain for Probe (default, but explicitly cleared)
    P0MDOUT &= ~0b00000100; 
    
    // Set P0.2 latch to enable weak pull-up resistor
    PROBE = 1; 

    // Enable T0 on P0.0
    XBR1 = 0x10;

    // Crossbar + weak pull-ups (so DIP to GND works)
    XBR2 = 0x40;

    // --- ADC Setup for P1.4 ---
    P1MDIN &= ~0x10; // Configure P1.4 as Analog Input
    P1SKIP |= 0x10;  // Skip P1.4 in crossbar routing

    ADC0MX = 0x0A;   // Select P1.4 (ADC0.10) as ADC input
    ADC0CF2 = 0x3F;   // Set VREF to VDD (crucial for ratio calculation)
    ADC0CF0 = 0x18;   // Default ADC clock
    ADC0CN0 = 0x80;  // Enable ADC0

    return 0;
}

// -------- ADC Read --------
unsigned int Read_ADC(void)
{
    SFRPAGE = 0x00;
    ADC0CN0 &= ~0x20; // Clear ADINT flag
    ADC0CN0 |= 0x10;  // Set ADBUSY to start conversion
    while ((ADC0CN0 & 0x20) == 0); // Wait for conversion to complete
    return ((ADC0H << 8) | ADC0L); // Return combined 14/12-bit value
}

// -------- delays (Timer3) --------
static void Timer3us(unsigned char us)
{
    unsigned char i;
    CKCON0 |= 0x40;
    TMR3RL  = (-(SYSCLK)/1000000L);
    TMR3    = TMR3RL;
    TMR3CN0 = 0x04;

    for (i = 0; i < us; i++)
    {
        while (!(TMR3CN0 & 0x80));
        TMR3CN0 &= ~0x80;

        if (TF0)
        {
            TF0 = 0;
            overflow_count++;
        }
    }
    TMR3CN0 = 0;
}

static void waitms(unsigned int ms)
{
    unsigned int j;
    for (j = ms; j != 0; j--)
    {
        Timer3us(249); Timer3us(249); Timer3us(249); Timer3us(250);
        
        // Instant Continuity Check injected into the delay loop
        if (PROBE == 0) {
            BUZZER = 1; // Probes are touching -> Beep
        } else {
            BUZZER = 0; // Probes apart -> Silent
        }
    }
}

// -------- LCD (4-bit) --------
static void LCD_pulse(void)
{
    LCD_E = 1;
    Timer3us(40);
    LCD_E = 0;
}

static void LCD_byte(unsigned char x)
{
    ACC = x;
    LCD_D7 = ACC_7; LCD_D6 = ACC_6; LCD_D5 = ACC_5; LCD_D4 = ACC_4;
    LCD_pulse();
    Timer3us(40);

    ACC = x;
    LCD_D7 = ACC_3; LCD_D6 = ACC_2; LCD_D5 = ACC_1; LCD_D4 = ACC_0;
    LCD_pulse();
}

static void WriteCommand(unsigned char x)
{
    LCD_RS = 0;
    LCD_byte(x);
    waitms(5);
}

static void WriteData(unsigned char x)
{
    LCD_RS = 1;
    LCD_byte(x);
    waitms(2);
}

static void LCD_Init(void)
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

static void LCD_GotoLine(unsigned char line2)
{
    WriteCommand(line2 ? 0xC0 : 0x80);
    waitms(2);
}

static void LCD_Print16_code(const char code *s, unsigned char line2)
{
    unsigned char i;
    LCD_GotoLine(line2);
    for (i = 0; i < 16; i++) WriteData(s[i]);
}

static void LCD_Print16_xdata(char xdata *s, unsigned char line2)
{
    unsigned char i;
    LCD_GotoLine(line2);
    for (i = 0; i < 16; i++) WriteData(s[i]);
}

// -------- Timer0 frequency counter on P0.0 (T0) --------
static void TIMER0_InitCounter(void)
{
    TMOD &= 0xF0;
    TMOD |= 0x05; // T0 = 16-bit COUNTER
    TR0 = 0;
}

static unsigned long measure_frequency_hz_250ms(void)
{
    unsigned long count;

    TL0 = 0; TH0 = 0;
    overflow_count = 0;
    TF0 = 0;

    TR0 = 1;
    waitms(250);
    TR0 = 0;

    count = ((unsigned long)overflow_count << 16) | ((unsigned long)TH0 << 8) | TL0;
    return count * 4UL;
}

// -------- line formatting helpers (no stdio) --------
static void line_fill_spaces(char xdata *line)
{
    unsigned char i;
    for (i = 0; i < 16; i++) line[i] = ' ';
}

static unsigned char put_uint_dec(char xdata *line, unsigned char idx, unsigned long v)
{
    char buf[10];
    char n = 0;
    char i;

    if (idx >= 16) return idx;

    if (v == 0)
    {
        line[idx++] = '0';
        return idx;
    }

    while (v > 0 && n < 10)
    {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (i = n - 1; i >= 0; i--)
    {
        if (idx >= 16) break;
        line[idx++] = buf[i];
        if (i == 0) break;
    }
    return idx;
}

static unsigned char put_3digits(char xdata *line, unsigned char idx, unsigned long v)
{
    if (idx + 3 > 16) return 16;
    line[idx++] = (char)('0' + ((v / 100) % 10));
    line[idx++] = (char)('0' + ((v /  10) % 10));
    line[idx++] = (char)('0' + ( v        % 10));
    return idx;
}

static void format_fixed_3dp(char xdata *line, float value, char u1, char u2)
{
    unsigned char idx = 0;
    unsigned long v1000, ip, fp;

    if (value < 0.0f) value = 0.0f;

    v1000 = (unsigned long)(value * 1000.0f + 0.5f);
    ip = v1000 / 1000UL;
    fp = v1000 % 1000UL;

    line_fill_spaces(line);

    // "C=" (Will be overwritten by format_res_line if used for Resistance)
    line[idx++] = 'C';
    line[idx++] = '=';

    idx = put_uint_dec(line, idx, ip);
    if (idx < 16) line[idx++] = '.';
    idx = put_3digits(line, idx, fp);

    if (idx < 16) line[idx++] = ' ';
    if (idx < 16) line[idx++] = u1;
    if (u2 && idx < 16) line[idx++] = u2;
}

static void format_sci(char xdata *line, float value, char u1, char u2)
{
    int exp = 0;
    float m = value;
    unsigned long mant1000;
    unsigned char idx = 0;
    unsigned int ae;

    line_fill_spaces(line);

    if (m <= 0.0f)
    {
        line[idx++] = 'C'; line[idx++] = '='; line[idx++] = '0';
        if (idx < 16) line[idx++] = ' ';
        if (idx < 16) line[idx++] = u1;
        if (u2 && idx < 16) line[idx++] = u2;
        return;
    }

    while (m >= 10.0f && exp < 99) { m /= 10.0f; exp++; }
    while (m <  1.0f && exp > -99) { m *= 10.0f; exp--; }

    mant1000 = (unsigned long)(m * 1000.0f + 0.5f);
    if (mant1000 >= 10000UL)
    {
        mant1000 /= 10UL;
        if (exp < 99) exp++;
    }

    line[idx++] = 'C';
    line[idx++] = '=';

    line[idx++] = (char)('0' + (mant1000 / 1000UL));
    line[idx++] = '.';
    idx = put_3digits(line, idx, mant1000 % 1000UL);

    if (idx < 16) line[idx++] = 'e';
    if (idx < 16) line[idx++] = (exp < 0) ? '-' : '+';

    ae = (unsigned int)((exp < 0) ? -exp : exp);
    if (ae > 99) ae = 99;

    if (idx < 16) line[idx++] = (char)('0' + (ae / 10));
    if (idx < 16) line[idx++] = (char)('0' + (ae % 10));

    if (idx < 16) line[idx++] = u1;
    if (u2 && idx < 16) line[idx++] = u2;
}

// -------- Ohmmeter Formatter --------
static void format_res_line(char xdata *line, float rx)
{
    unsigned char idx = 0;
    line_fill_spaces(line);

    if (rx < 0.0f || rx > 10000000.0f) {
        // Open circuit or extremely high resistance
        line[idx++] = 'R'; line[idx++] = '='; 
        line[idx++] = 'O'; line[idx++] = 'P'; line[idx++] = 'E'; line[idx++] = 'N';
    }
    else if (rx >= 1000.0f) {
        // Format as kOhms (using 'k' and 'R' as shorthand)
        format_fixed_3dp(line, rx / 1000.0f, 'k', 'R'); 
        line[0] = 'R'; // Override the 'C' placed by format_fixed_3dp
    } else {
        // Format as Ohms
        format_fixed_3dp(line, rx, ' ', 'R');
        line[0] = 'R';
    }
}

// -------- Unit switch logic --------
static unit_t read_unit_mode(void)
{
    unsigned char mask = 0;

    if (SW_F  == 0) mask |= 0x01;
    if (SW_mF == 0) mask |= 0x02;
    if (SW_uF == 0) mask |= 0x04;
    if (SW_nF == 0) mask |= 0x08;

    if (mask == 0) return UNIT_AUTO;
    if (mask & (mask - 1)) return UNIT_ERR;

    if (mask == 0x01) return UNIT_F;
    if (mask == 0x02) return UNIT_mF;
    if (mask == 0x04) return UNIT_uF;
    return UNIT_nF;
}

void main(void)
{
    unit_t mode;
    unsigned long f1, f2, f_hz;
    unsigned int adc_val;
    float Rsum, C_F, rx_ohms;

    xdata char line1[16];
    xdata char line2[16];

    TIMER0_InitCounter();
    LCD_Init();

    Rsum = RA_OHM_F + 2.0f * RB_OHM_F; // (RA + 2RB)

    while (1)
    {
        mode = read_unit_mode();

        if (mode == UNIT_ERR)
        {
            LCD_Print16_code(MSG_ERR1, 0);
            LCD_Print16_code(MSG_ERR2, 1);
            continue;
        }

        f1 = measure_frequency_hz_250ms();
        f2 = measure_frequency_hz_250ms();
        f_hz = (f1 + f2) / 2UL;

        // ---- Measure Ohmmeter Resistance ----
        adc_val = Read_ADC();
        
        // Check for open circuit (ADC reads near VCC)
        if ((float)adc_val > (ADC_MAX * 0.98f)) { 
            rx_ohms = -1.0f; // Flag as OPEN
        } else {
            rx_ohms = R_KNOWN_OHMS * ((float)adc_val / (ADC_MAX - (float)adc_val));
        }

        if ((f_hz < F_MIN_HZ) || (f_hz > F_MAX_HZ))
        {
            LCD_Print16_code(MSG_PLACE1, 0);
            LCD_Print16_code(MSG_PLACE2, 1);
            continue;
        }

        // C(F) = 1.44 / ((RA+2RB)*f)
        C_F = 1.44f / (Rsum * (float)f_hz);

        // Line 1: Capacitance
        if (mode == UNIT_AUTO)
        {
            if (C_F >= 1e-6f)      format_fixed_3dp(line1, C_F * 1e6f, 'u', 'F');
            else if (C_F >= 1e-9f) format_fixed_3dp(line1, C_F * 1e9f, 'n', 'F');
            else                   format_fixed_3dp(line1, C_F * 1e12f,'p', 'F');
        }
        else if (mode == UNIT_uF)
        {
            format_fixed_3dp(line1, C_F * 1e6f, 'u', 'F');
        }
        else if (mode == UNIT_nF)
        {
            format_fixed_3dp(line1, C_F * 1e9f, 'n', 'F');
        }
        else if (mode == UNIT_mF)
        {
            format_sci(line1, C_F * 1e3f, 'm', 'F');
        }
        else // UNIT_F
        {
            format_sci(line1, C_F, 'F', 0);
        }

        // Line 2: Resistance
        format_res_line(line2, rx_ohms);

        LCD_Print16_xdata(line1, 0);
        LCD_Print16_xdata(line2, 1);
    }
}