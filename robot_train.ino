/*
	Copyright 2017 Mario Pascucci <mpascucci@gmail.com>

	This is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This software is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this software.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "Arduino.h"
#include "IRremote.h"
#include <Adafruit_NeoPixel.h>

// motors
#define BLUE	1
#define RED 	0



/********************************************
 *
 *   			USER SETTINGS
 *
 *		(do not touch anything outside)
 *
 *********************************************/

// RED or BLUE, all caps
#define MOTOR		BLUE

// channel can be 0,1,2,3
#define IRCHANNEL 	0

// good ratio between running time and charging time is:
// running_time <= 2 * charging time

// running time for train in seconds
#define TR_RUNTIME	50

// train stop/charging time in seconds
#define TR_CHARGE	30

// change values below only if train is too slow/too fast

// running maximum speed (recommended 6-7)
#define TR_FULLSP	6

// Search speed (recommended 4-6)
#define TR_SRCSP		5

// slow/locate charging pad speed (recommended 2-3)
#define TR_SLOWSP	2

// change values below only if you know what you doing

// startup time in seconds
#define TR_STARTUP 	15

// time to constant voltage full charge in seconds (usually 30')
#define TIME_FULLCHARGE	1800

// time to LOCATE for charging pad in seconds
#define TR_PADSRC	15

/********************************************
 * END OF USER SERVICEABLE PART
 * (do not touch anything outside)
 ********************************************/



// some values

// battery low and ok values (~3.6V 3.75V 3.9V 4.1V)
#define BATTWARN	740
#define BATTLOW		770
#define BATTHIGH	800
#define BATTFULL	840

// speeds
#define TR_STOP		0

// max number of missed "LOCATE"
#define MAXMISSED	3

// if you use a Digispark
#ifdef __AVR_ATtiny85__
// inputs

// battery voltage
#define VBAT	A1

// charge present
#define VCHARGE	4

// Hall sensor input
#define MAGSENS	3

// outputs

// IR LED
// in Digispark must be 1
#define IROUT	1

// NeoPixel drive
#define NEOPIXELPIN	0

#else
// If you use Arduino Nano
// battery voltage
#define VBAT	A7

// charge present
#define VCHARGE	4

// Hall sensor input
#define MAGSENS	2

// outputs

// IR LED
// in Arduino Uno/Nano must be 3
#define IROUT	3

// NeoPixel drive
#define NEOPIXELPIN	5



#endif

// status
#define STARTUP			0
#define RUN				1
#define SEARCH			2
#define LOCATE			3
#define CHARGE			4
#define CHARGE_FULL		5
#define CHARGE_FULL2	6
#define	MANUAL			7
#define ALARM			100

// every 5 minutes
#define BATT_CHECK		300

// ten seconds
#define CHARGE_INCREMENT	10


int vunload = 0;
int vu[4];
int status;
long lastchange;
int missedPad = 0;
long battcheck;
long charge_time = TR_CHARGE;

IRsend irsend;

Adafruit_NeoPixel strip = Adafruit_NeoPixel(1, NEOPIXELPIN, NEO_GRB + NEO_KHZ800);


// send command to train, using current settings
// speed: train speed (see IR protocol documentation)
void ircommand(int speed) {
	// use "single output mode, PWM"
	// so, no toggle, address == 0,
	int nibble1 = IRCHANNEL;
	int nibble2 = 4 + MOTOR;
	int nibble3 = speed;
	int nibble4 = 0xf ^ nibble1 ^ nibble2 ^ nibble3;
	unsigned int command = nibble1 << 12 | nibble2 << 8 | nibble3 << 4 | nibble4;
	irsend.sendLegoPowerFunctions(command, false);
	delay(20);
	irsend.sendLegoPowerFunctions(command, false);
}


// smooth change train speed
// ss: initial speed
// es: final speed
void trainChangeSpeed(int ss, int es) {

	if (ss == es)
		return;
	int inc;
	if (ss < es) {
		inc = 1;
	}
	else {
		inc = -1;
	}
	for (int v=ss; v<es;v+=inc) {
		ircommand(v);
		delay(800);
	}
}


void setup()
{
	// inputs
	pinMode(VCHARGE, INPUT);
	pinMode(MAGSENS,INPUT_PULLUP);
	pinMode(VBAT,INPUT);

	// outputs
	pinMode(IROUT,OUTPUT);

	// set initial values
	vunload = (analogRead(VBAT) + analogRead(VBAT)) / 2;
	vu[0] = vu[1] = vu[2] = vu[3] = vunload;

	strip.begin();

	changeStatus(STARTUP);

}



// show status as color if a NeoPixel is connected to status pin
// see schematics
void printStatus(int stat) {

	switch (stat) {
	case STARTUP:
		// black
		strip.setPixelColor(0, 0, 0, 0);
		break;
	case RUN:
		// green
		strip.setPixelColor(0, 0, 96, 0);
		break;
	case SEARCH:
		// blue
		strip.setPixelColor(0, 0, 0, 96);
		break;
	case LOCATE:
		// light blue
		strip.setPixelColor(0, 0, 96, 96);
		break;
	case CHARGE:
		// purple
		strip.setPixelColor(0, 96, 0, 96);
		break;
	case CHARGE_FULL:
		// orange
		strip.setPixelColor(0, 96, 32, 0);
		break;
	case CHARGE_FULL2:
		// yellow
		strip.setPixelColor(0, 96, 80, 0);
		break;
	case MANUAL:
		// dark green
		strip.setPixelColor(0, 0, 32, 0);
		break;
	case ALARM:
		// full red
		strip.setPixelColor(0, 96, 0, 0);
	}
	strip.show();

}


// change system status
void changeStatus(int s) {

	lastchange = millis();
	status = s;
	printStatus(status);
}



void loop()
{

	long runtime = millis() - lastchange;
	switch (status) {
	case STARTUP:
		if (digitalRead(MAGSENS) == LOW) {
			strip.setPixelColor(0, 97, 95, 96);
			strip.show();
			delay(50);
		}
		else {
			strip.setPixelColor(0, 0, 0, 0);
			strip.show();
			delay(50);
		}
		if (runtime > TR_STARTUP*1000L) {
			battcheck = millis();
			// if is over Qi pad, do a full charge
			if (digitalRead(VCHARGE) == HIGH) {
				changeStatus(CHARGE_FULL);
			}
			else if (digitalRead(MAGSENS) == LOW) {
				// if over magnet, go in MANUAL mode
				changeStatus(MANUAL);
			}
			else if (vunload > BATTWARN) {
				// normal run, smooth start
				changeStatus(RUN);
				trainChangeSpeed(TR_STOP,TR_FULLSP);
			}
			else {
				// low battery, no charging pad, so all stop
				changeStatus(ALARM);
				ircommand(TR_STOP);
			}
		}
		break;
	case RUN:
		if (runtime > TR_RUNTIME*1000L) {
			changeStatus(SEARCH);
			// going to reduced speed, forward
			ircommand(TR_SRCSP);
		}
		break;
	case SEARCH:
		if (digitalRead(MAGSENS) == LOW) {
			changeStatus(LOCATE);
			// going to low speed, forward
			ircommand(TR_SLOWSP);
		}
		else if (runtime > TR_RUNTIME*2000L) {
			// no magsens found, going to alarm mode
			// without magnet detection cannot have Vunload measurement
			// so no magnet->alarm
			ircommand(TR_STOP);
			changeStatus(ALARM);
		}
		break;
	case LOCATE:
		delay(80);
		ircommand(TR_STOP);
		delay(800);
		vu[3] = vu[2];
		vu[2] = vu[1];
		vu[1] = vu[0];
		vu[0] = analogRead(VBAT);
		vunload = (vu[0] + vu[1] + vu[2] + vu[3]) >> 2;
		if ((millis() - battcheck) > BATT_CHECK) {
			battcheck = millis();
			// time to verify battery
			if (vunload < BATTLOW) {
				if (charge_time < 3*TR_CHARGE)
				charge_time += CHARGE_INCREMENT;
			}
			else if (vunload > BATTHIGH) {
				if (charge_time > TR_CHARGE) {
					charge_time -= CHARGE_INCREMENT;
				}
			}
		}
		if (digitalRead(VCHARGE) == HIGH) {
			delay(843);
			if (digitalRead(VCHARGE) == HIGH) {
				// stop train
				if (vunload < BATTWARN) {
					changeStatus(CHARGE_FULL);
				}
				else {
					changeStatus(CHARGE);
				}
				missedPad = 0;
				ircommand(TR_STOP);
			}
		}
		else if (runtime > TR_PADSRC*1000L) {
			// missed charging pad, return to SEARCH
			missedPad++;
			if (missedPad > MAXMISSED && vunload < BATTWARN) {
				// too many missed pad and low battery-> ALARM
				missedPad = 0;
				ircommand(TR_STOP);
				changeStatus(ALARM);
			}
			else {
				changeStatus(SEARCH);
				// going to reduced speed, forward
				ircommand(TR_SRCSP);
			}
		}
		else {
			// no charging pad, try again
			ircommand(TR_SLOWSP);
		}
		break;
	case CHARGE:
	case CHARGE_FULL:
	case CHARGE_FULL2:
		if (status == CHARGE && runtime > charge_time*1000L) {
			// start train at full speed
			changeStatus(RUN);
			trainChangeSpeed(TR_STOP,TR_FULLSP);
		}
		if (status == CHARGE_FULL){
			int v = analogRead(VBAT);
			if (v >= BATTFULL) {
				changeStatus(CHARGE_FULL2);
			}
		}
		else if (runtime >= TIME_FULLCHARGE*1000L) {
			changeStatus(RUN);
			trainChangeSpeed(TR_STOP,TR_FULLSP);
		}
		break;
	case MANUAL:
		// TODO: how to resume "automatic"?
		break;
	case ALARM:
		if (digitalRead(VCHARGE) == HIGH) {
			changeStatus(CHARGE_FULL);
		}
		break;
	}

}
