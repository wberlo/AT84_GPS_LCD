/* ATtiny AT84 GPS display
   by: Wim v B
   
   GPS parser by:
   David Johnson-Davies - www.technoblogy.com - 10th December 2014
   
*/
#ifndef F_CPU
#define F_CPU 8000000ul
#endif
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdlib.h>
#include <string.h>
#include "LCD_AT84_C.h"


unsigned char ReverseByte (unsigned char x) {
  x = ((x >> 1) & 0x55) | ((x << 1) & 0xaa);
  x = ((x >> 2) & 0x33) | ((x << 2) & 0xcc);
  x = ((x >> 4) & 0x0f) | ((x << 4) & 0xf0);
  return x;    
}

// Initialise USI for UART reception.
void InitialiseUSI (void) {  
  DDRA &= ~(1<<PA6);			   // Define DI as input
  USICR = 0;                       // Disable USI.
  GIFR = 1<<PCIF0;                 // Clear pin change interrupt flag.
  GIMSK |= 1<<PCIE0;               // Enable pin change interrupts
  PCMSK0 |= 1<<PCINT6;             // Enable pin change on pin 6
}

// Pin change interrupt detects start of UART reception.
ISR (PCINT0_vect) {
  if (!(PINA & 1<<PINA6)) {        // Ignore if DI is high
    GIMSK &= ~(1<<PCIE0);          // Disable pin change interrupts
    TCCR0A = 2<<WGM00;             // CTC mode
    TCCR0B = 0<<WGM02 | 2<<CS00;   // Set prescaler to /8
    TCNT0 = 0;                     // Count up from 0
    OCR0A = 156;                   // Delay 156*8 cycles
    TIMSK0 |= 1<<OCIE0A;           // Enable output compare interrupt
  }
}

// COMPA interrupt indicates middle of bit 0
ISR (TIM0_COMPA_vect) {
  TIMSK0 &= ~(1<<OCIE0A);          // Disable COMPA interrupt
  TCNT0 = 0;                      // Count up from 0
  OCR0A = 104;                    // Shift every 104*8 cycles
  // Enable USI OVF interrupt, and select Timer0 compare match as USI Clock source:
  USICR = 1<<USIOIE | 0<<USIWM0 | 1<<USICS0;
  USISR = 1<<USIOIF | 8;          // Clear USI OVF flag, and set counter
}


// ParseGPS
// Example: $GPRMC,194509.000,A,4042.6142,N,07400.4168,W,2.03,221.11,160412,,,A*77
//char fmt[]="$GPRMC,dddtdd.ddm,A,eeae.eeee,l,eeeae.eeee,o,djdk,ddd.dc,dddy??,,,A*??";
//          $GPRMC,220209.000,A,5928.1590,N,01805.3657,E,0.37,339.13,300615,,,A*6B
char fmt[]="$GPRMC,dddtdd.ddm,A,eeae.ee??,l,eeeae.ee??,o,djdk,ddc.??,dddy??,,,A*??";

int state = 0;
volatile uint16_t temp;
volatile uint32_t ltmp;

// GPS variables
volatile unsigned int Time, Msecs, Knots, Course, Date;
volatile uint32_t Lat, Longt;
volatile uint8_t Fix = 0;
volatile char NS, EW;

void ParseGPS (unsigned char c) {
  if (c == '$') { state = 0; temp = 0; ltmp = 0; }
  char mode = fmt[state++];
  // If received character matches format string, or format is '?' - return
  if ((mode == c) || (mode == '?')) return;
  // d=decimal digit
  char d = c - '0';
  if (mode == 'd') temp = temp*10 + d;
  // e=long decimal digit
  else if (mode == 'e') ltmp = ltmp*10 + d;
  // a=angular measure
  else if (mode == 'a') ltmp = ltmp*6 + d;
  // t=Time - hhmm
  else if (mode == 't') { Time = temp*10 + d; temp = 0; }
  // m=Millisecs
  else if (mode == 'm') { Msecs = temp*10 + d; temp=0; }  // was ltmp = 0;
  // l=Latitude - in minutes*100
  else if (mode == 'l')
	{
	  if (c == 'N')  NS = 'N'; else NS = 'S';
	  Lat = ltmp;
	  ltmp = 0;
	}
  // o=Longitude - in minutes*100
  else if (mode == 'o')
	{
		if (c == 'E') EW = 'E'; else EW = 'W';
		Longt = ltmp;
		ltmp = 0;
	}  // was temp = 0;
  // j/k=Speed - in knots*100
  else if (mode == 'j') { if (c != '.') { temp = temp*10 + d; state--; } }
  else if (mode == 'k') { Knots = temp*10 + d; temp = 0; }
  // c=Course (Track) - in degrees
  else if (mode == 'c') { Course = temp*10 + d; temp = 0; }
  // y=Date - ddmm
  else if (mode == 'y') { Date = temp*10 + d ; Fix = 1; temp = 0;}
  else state = 0;
}

// USI overflow interrupt indicates we've received a serial byte
ISR (USI_OVF_vect) {
	USICR  =  0;                    // Disable USI
	GIFR = 1<<PCIF0;                 // Clear pin change interrupt flag.
	GIMSK |= 1<<PCIE0;               // Enable pin change interrupts again
	unsigned char c = USIDR;
	c = ReverseByte(c);
	ParseGPS(c);
}

void LCD_message( char *stringmsg)
{
	int ii = 0;
	while (ii < strlen(stringmsg))
	{
		LCD_write(stringmsg[ii]);
		ii++;
	}
}

int main(void)
{
	char *intromsg = "No Fix";
	char *lbl1 = "GMT    ";
	char *lbl2 = "Lat    ";
	char *lbl3 = "Long   ";
	//char *lbl4 = "Speed  ";
	char *lbl4 = "Heading";
	char tmmsg[12];
	char lamsg[13];
	char lomsg[13];
	char spmsg[12];
	cli();
	LCD_init();
	LCD_begin(20,4, LCD_5x8DOTS);
	LCD_clear();
	LCD_setCursor(0,0);
	LCD_message(intromsg);

	LCD_setCursor(0,0);
	sei();
	InitialiseUSI();
	do; while(!Fix);
	LCD_setCursor(0,0);
	LCD_message(lbl1);
	LCD_setCursor(0,1);
	LCD_message(lbl2);
	LCD_setCursor(0,2);
	LCD_message(lbl3);
	LCD_setCursor(0,3);
	LCD_message(lbl4);

	while (1)
	{
		int tmpt, tmphd;
		long tmplt, tmplg, tmpkn;
		char tmpns, tmpew;
		// get  values from GPS parser
		cli();
		tmpt = Time;
		tmplt = Lat;
		tmplg = Longt;
		tmphd = Course;
		tmpkn = Knots;
		tmpns = NS;
		tmpew = EW;
		sei();
		
		// convert time to hh:mm
		uint8_t Timeh = tmpt/100;
		uint8_t Timemin = tmpt - Timeh*100;
		
		// convert latitude and longitude to dd.mm.ss
		long Latsec = tmplt;
		tmplt = tmplt/100;
		Latsec = Latsec - tmplt*100;
		Latsec = Latsec*6/10;

		int Latdeg = tmplt / 60;
		int Latmin = tmplt - Latdeg*60;
		
		long Longsec = tmplg;
		tmplg = tmplg / 100;
		Longsec = Longsec - tmplg*100;
		Longsec = Longsec*6/10;

		int Longdeg = tmplg / 60;
		int Longmin = tmplg - Longdeg*60;
		
		// convert Knots to Km/h
		int Kmh = 37*tmpkn/2000;
		
		// format into string
		sprintf(tmmsg, " %02d:%02d", Timeh, Timemin);
		
		int n2 = sprintf(lamsg, " %2d.%02d.%02ld %c", Latdeg, Latmin, Latsec, tmpns);
		if (n2>11)
		{
			sprintf(lamsg, " %2d.%02d %c", Latdeg, Latmin, tmpns);
		}
		
		int n3 = sprintf(lomsg, "%3d.%02d.%02ld %c", Longdeg, Longmin, Longsec, tmpew);
		if (n3>11)
		{
			sprintf(lomsg, "%3d.%02d %c", Longdeg, Longmin, tmpew);
		}
		
		if ((tmphd > 360) || (Kmh < 3))		// don't display unreliable heading
		// using the gps, i discovered that values < 100 degrees are not shown. One bug left ...
		{
			strcpy(spmsg, " --- deg ");
		}
		else
		{
			int n4 = sprintf(spmsg, " %3d deg", tmphd);
			if (n4 > 9)
			{
				sprintf(spmsg, "%3d deg ", tmphd);
			}
		}
/*
		int n4 = sprintf(spmsg, "%3d Km/h", Kmh);
		if (n4>9)
		{
			sprintf(spmsg, "%3d", Kmh);
		}
*/
		// display  values
		// line 0: time (GMT)
		// line 1: latitude
		// line 2: longitude
		// line 3: heading

		LCD_setCursor(9,0);
		LCD_message(tmmsg);
		LCD_setCursor(9,1);
		LCD_message(lamsg);
		LCD_setCursor(9,2);
		LCD_message(lomsg);
		LCD_setCursor(9,3);
		LCD_message(spmsg);
	}
}
