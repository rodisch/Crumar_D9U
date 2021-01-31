////////////////////////////////////////////////////////////////////
// Crumar Drawbar Controller D9U
// by Guido Scognamiglio 
// Runs on Atmel ATmega32U4 Arduino Leonardo (with MIDI USB Library)
// Reads 9 analog inputs from internal ADCs
// Sends MIDI CC numbers 12-20 or 21-29 according to selected mode
// 
// July 2018 - Last Update by Crumar
// 
// Jan. 2021 - Updates by RoDi
//   - replace 'int' to some more specific types (byte, int8_t,...)
//   - button action in own subroutine DoButton()
//   - added USB-MIDI library for proper sysex handling over USB
//   - sending SysEx for Roland FA-06/07/08, SN Organ; Mode switch part 1,2
//   - selective compilation for CCs/Sysex to serial/USB (see 'what goes where')
// Feb. 2021
//   - CC to sysex conversion  
//

////////////////////////////////////////////////////////////////////
// This is where you can define your CC numbers for the Bank 0 or 1
int CC_db[2][9] = {
  { 12, 13, 14, 15, 16, 17, 18, 19, 20 }, // Upper drawbars, channel 1
  { 21, 22, 23, 24, 25, 26, 27, 28, 29 }  // Lower drawbars, channel 2
};
byte cc_dbmin = 12; // defines CC window for OnControlChange()
byte cc_dbmax = 29;

////////////////////////////////////////////////////////////////////
// You should not modify anything else below this line 
// unless you know what you're doing.
////////////////////////////////////////////////////////////////////

// --- what goes where ----
// CC_*: CCs, SX_*: Sysex, *_OUT: to Serial, *_USB: to USB
//#define CC_OUT
#define SX_OUT
#define CC_USB
//#define SX_USB

//#define _DEBUG_   // init serial in setup, use serial.print() for debugging 

// --- Include libraries
#include <EEPROM.h> // default Arduino
#include <MIDI.h>  // MIDI Library by Francois Best, lathoub v5.0.2
#include <USB-MIDI.h> //USB-MIDI by lathoub v1.1.2

USBMIDI_CREATE_INSTANCE(0,MIDIUSB); // USB MIDI
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDIOUT); // serial MIDI-output

// --- Define global modes
#define DEBOUNCE    50
#define RESEND_TIME 50
#define DEADBAND    8

// --- Init global variables
byte ctl_mode; // Should be either 0 or 1, stored in EEPROM
int16_t prev_val[9] = { 10, 10, 10, 10, 10, 10, 10, 10, 10 }; //10bit ADC-values

byte db_pos[2][9] = { 
  { 8, 8, 8, 0, 0, 0, 0, 0, 0 }, //drawbar positions 0..8
  { 8, 8, 8, 0, 0, 0, 0, 0, 0 }
} ;

boolean resend[2]={false,false};
unsigned long sx_time=0; // time of last sysex transmission, to prevent queue overruns 
byte sx_mode = 0;

uint16_t debounce_cnt = DEBOUNCE; //counter for button debounce

// --- Hardware Pins -----------------------------------
// Define I/O pins
#define LED_RED     15
#define LED_GREEN   16
#define BUTTON       5

// ADC reference map
byte ADCmap[9] = { A0, A1, A2, A3, A6, A7, A8, A9, A10 };

// ======================================================================================= 

// Called then the pushbutton is depressed
void set_mode() {
    digitalWrite(LED_RED, ctl_mode ? LOW : HIGH);
    digitalWrite(LED_GREEN, ctl_mode ? HIGH : LOW);
    EEPROM.write(0x01, ctl_mode);
}

void DoButton(byte button) {
    if (digitalRead(button) == LOW) {
        if (debounce_cnt > 0)
            --debounce_cnt;
    }
    else {
        debounce_cnt = DEBOUNCE;
    }
    if (debounce_cnt == 2) {
        ctl_mode = !ctl_mode; // Reverse
        set_mode();   // set and strore
    }
}

// Called to generate the MIDI CC message
void SendMidiCC(byte channel, byte CCnum, byte value) {   
  #ifdef CC_OUT
  MIDIOUT.sendControlChange(CCnum, value, channel+1); // Midi lib wants channels 1~16
  #endif
  #ifdef CC_USB
  MIDIUSB.sendControlChange(CCnum, value, channel+1);
  #endif
}

void setRolandChecksum(byte *sysex, uint16_t size, uint16_t dta) {
  // dta: begin of data bytes, if dta==0 do search 
  // skip model ID, can be 'XX', '00 XX' or '00 00 XX'
  if (dta==0) {
    dta = 2; //start of model ID
    while (sysex[dta] == 0) {
      ++dta; 
    }      // dta = last byte model ID
    ++dta; // dta = command byte
    ++dta; // dta = first data byte
  }

  // checksum algo: sum all data bytes, do some modulo 128 stuff
  uint16_t sum=0; // sum of data bytes
  for (sum=0; dta < size - 1; dta++) { 
    sum += sysex[dta]; 
  }
  sysex[size-1] = 128 - (sum % 128); // apply modulo 128 and store cs
}

void SendDbSysex(byte part) {    
  // Roland FA-06/07/08: 
  //  drawbar sysex:  41 10 00 00 77 12 19 pp 00 22 vv1 vv2 ... vv9 cs 
  //    pp {02,22}: part1,part2 (baseadr 19 02 00 22 or 19 22 00 22)
  //    vv1..vv9: draw position
  //    cs: Roland checksum
  byte db_sysex[20] = {0x41, 0x10, 0x00, 0x00, 0x77, 0x12, // header
                       0x19, byte(part>0 ? 0x22:0x02), 0x00, 0x22,  // adress
                       0,0,0,0,0,0,0,0,0,0}; // dummy for vv.., cs

  for (uint8_t ii=0; ii<sizeof(db_pos[part]); ++ii) {
     db_sysex[10+ii]=db_pos[part][ii]; //copy current db position to sysex
  };

  byte first_dta = 6; // 6: start of data bytes    
  setRolandChecksum(db_sysex,sizeof(db_sysex), first_dta); 

  #ifdef SX_OUT
  MIDIOUT.sendSysEx(sizeof(db_sysex),db_sysex,false); // add F0..F7
  #endif
  #ifdef SX_USB
  MIDIUSB.sendSysEx(sizeof(db_sysex),db_sysex,false);
  #endif
  resend[part]=false;
}

// Called to check whether a drawbar has been moved
void DoDrawbar() {
  for (byte db = 0; db < 9; db++) {
    int16_t value = analogRead(ADCmap[db]);
    int16_t diff = abs(value - prev_val[db]); // distance current to previous value
    if (diff <= DEADBAND)
      continue; // skip if the new value is within the deadband

    prev_val[db] = value;                                                    // store new adc-value (10bit)
    SendMidiCC(ctl_mode > 0 ? 1 : 0, CC_db[ctl_mode][db], byte(value >> 3)); // Send as CC

    byte pos = map(value + 36, 0, 1110, 0, 9); // scale to 0..8, adapted to drawbar marks
    if (pos != db_pos[ctl_mode][db])
    {
      db_pos[ctl_mode][db] = pos;
      resend[ctl_mode] = true; //flag new value(s)
      //Send drawbar-sysex is done in loop() for better timing
    }
  }
}

void OnControlChange(byte channel, byte control, byte value) {
	byte cc_md; // local mode index 
  if (channel>2) //channel 1..16
		return;
  else
		cc_md=channel-1; 

	if (control >= cc_dbmin && control <= cc_dbmax) {
		for (byte db = 0; db < 9; db++) {
			if (control == CC_db[cc_md][db]) {
				byte pos = map(value, 0, 128, 0, 9); // map to 0..8
        if (pos!=db_pos[ctl_mode][db]) { 
          db_pos[ctl_mode][db] = pos;
          resend[ctl_mode] =true; 
        }
			  return; //
			}
		}
	}
}

// The setup routine runs once when you press reset:
void setup()  {  
  MIDIOUT.begin(); // Initialize serial MIDI
  
  // MIDIUSB.begin(MIDI_CHANNEL_OMNI);
  MIDIUSB.setHandleControlChange(OnControlChange);

  // Set up digital I/Os
  pinMode(BUTTON, INPUT_PULLUP); // Button
  pinMode(LED_RED, OUTPUT);      // Led 1
  pinMode(LED_GREEN, OUTPUT);    // Led 2
  
  // Recall mode from memory and set
  // Make sure mode is either 0 or 1
  ctl_mode = EEPROM.read(0x01) > 0 ? 1 : 0;
  set_mode();

  #if defined _DEBUG_
  Serial.begin(9600);
  while (!Serial);  
  Serial.println();
  Serial.println(" ==== Drawbar Controller v0.1 ==== ");
  #endif
}

// The loop routine runs over and over again forever:
void loop() 
{    
  MIDIUSB.read();  // process MIDI Input -> OnControlChange

  DoButton(BUTTON); //switch mode ?
  DoDrawbar(); // scan ADCs
  
  unsigned long now = millis();
  if ( (now-sx_time)>RESEND_TIME ) {         
    if (resend[sx_mode]) {
      SendDbSysex(sx_mode); //Send Sysex 
      sx_time=now;
    }
    sx_mode = !sx_mode; // 
  }
}
