//------------------------------------------------------------
//------------------------------------------------------------
// Inclusions and Initializations

// SD Shield
#include <SPI.h>
#include <SD.h>
Sd2Card card;
SdVolume volume;
SdFile root;
File dataFile;
File Inputs;
File CalibrationFile;
int FSR, alpha;

// RTC
#include <Wire.h>
#include "RTClib.h"
RTC_DS1307 rtc;

// Time stamp variables
int MillisInt = 0;
int time1 = 0;
int setmillis = 0; // millis() is of type long, this will be subtracted from it to reset the millisecond counter.
String DateString, TimeString, HourString, MinuteString, SecondString, RecordString; // String that will be recorded to the SD Card

//------------------------------------------------------------

// LEDs
//Place LED ground into separate ground from the FSRs!
#include "LPD8806.h"
#include "SPI.h" // Comment out this line if using Trinket or Gemma
#ifdef __AVR_ATtiny85__
#include <avr/power.h>
#endif
int i;
const int nLEDs = 2;
const int dataPin = 4; // Green Wire
const int clockPin = 2; // Blue Wire
LPD8806 strip = LPD8806(nLEDs, dataPin, clockPin);

// Timing variables for LEDs
long lightTime = 0;
long showTime = 800;

//------------------------------------------------------------

// Force Sensors
// Assigns analog pins to read
const int fsrPin[6] = {13, 14, 15, 11, 8, 7};
// Raw FSR Readings
int fsrReading[6] = {0, 0, 0, 0, 0, 0};

// Force Calibration Values. Tares(i) is the tared value, applied post calibration.
long Calibs[6][2];
float e = 2.781;
float denom = 10000.0; // Denominator of the Calibs columns
int Tares[6];

//Forces Post Calibration
float fsrForce[6];
// Forces post calibration equation.
float Forces[2]; // 0 is front, 1 is back

// Data averaging and smoothing variables:
int blockSize = 11;
float sums[2][11]; // MUST be the same as blockSize!
float aveSum[3];
float positive = 0.0, deviation = 0.0;
int sumcounter = 0;
bool istared = false ;
int valuecount;

// Variables to call tare function
const int buttonPin = 5; // Digital Pin for the button reading
bool buttonState = false; // Button is initially unpressed
int buttonTimer = 0; // Counts number of cycles button is held for

//------------------------------------------------------------

// Variables determining an acceptable PWB step
float AccLow;
float AccHigh;
float Threshhold;
int StanceStart, SwingStart; // millis() stamps of respective events. Difference is reported.
String StanceStartTime; // Time stamp of when the Stance is Started.
boolean InThresh;
int threshCycles = 0;

// Writing values to SD Card
float TempMaxForce, TempMaxRear;
String TempMaxTime;
boolean writeCycle = false;

//------------------------------------------------------------
//------------------------------------------------------------

void setup() {

  // Start Serial Port communication
//Serial.begin(57600);

  //------------------------------------------------------------

  // RTC Setup
#ifdef AVR
  Wire.begin();
#else
  Wire1.begin();
#endif
  rtc.begin();

  if (! rtc.isrunning()) {
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time from the connected PC
  }
  // END RTC Setup

  //------------------------------------------------------------

  // SD Setup
  // make sure that the default chip select pin is set to
  // output, even if you don't use it:
  pinMode(10, OUTPUT);
  // see if the card is present and can be initialized:
  if (!SD.begin(10, 11, 12, 13)) {
    Serial.println("SD card initialization failed!");
    return;
  }
  // Read bodyweight and partial percent
  // from input text file "Inputs.txt" of format: int1, int2
  Inputs = SD.open("Inputs.txt", FILE_READ);
  int BW = Inputs.parseInt();
  int PP = Inputs.parseInt();
  Inputs.close();  // Close this file on the SD Card
  // Calculate threshold values for LED coordination
  AccLow = ((PP - 10) / 100.0) * BW;
  AccHigh = ((PP + 10) / 100.0) * BW;
  Threshhold = .05 * BW;
  // Get calibration information
  CalibrationFile = SD.open("Cal1.txt", FILE_READ);
  if (CalibrationFile.available()) {
    for (FSR = 0; FSR < 6; FSR ++) {
      for (alpha = 0; alpha < 2; alpha++) {
        Calibs[FSR][alpha] = CalibrationFile.parseInt();
      }
    }
    CalibrationFile.close();
  }
  // END SD Setup

  //------------------------------------------------------------

  // LED Setup - Do not connect LED Ground with any other grounds.
#if defined(__AVR_ATtiny85__) && (F_CPU == 16000000L)
  clock_prescale_set(clock_div_1); // Enable 16 MHz on Trinket
#endif

  strip.begin();
  strip.show();
  // END LED Setup
}

//------------------------------------------------------------
//------------------------------------------------------------

void loop() {

  //------------------------------------------------------------

  // DATE & TIME Stamping
  DateTime now = rtc.now(); // Access RTC

  MillisInt = int(millis() - setmillis);

  // Check & manipulate the milliseconds
  if (now.second() == 0 && time1 == 59) { // reset time1 every minute
    time1 = now.second();
    setmillis = millis();
  }
  else if (now.second() > time1 && MillisInt > 500) { // reset milliseconds for new second
    setmillis = millis();
    MillisInt = 0;
    time1 = now.second();
  }
  else if (now.second() == time1 && 1000 <= MillisInt)
    MillisInt -= 3;

  String MillisString = String(MillisInt);

  int milLength = MillisString.length();  // the length of the string of MillisString.
  if (milLength >= 3)
    MillisString = MillisString.substring(milLength - 3);
  // removes all other characters except the last three in the MillisString string.
  if (MillisString.length() < 2)
    MillisString = '0' + MillisString;
  if (MillisString.length() < 3)
    MillisString = '0' + MillisString;
  // End Millisecond Calcuations

  // Set Date String of Year/Month/Day
  DateString = String(now.month()) + '/' + String(now.day()) + '/' + String(now.year()) + ' ';

  // Time String. Inserting zeros where appropriate to keep the format: HH:MM:SS.XXX
  if (now.hour() < 10)
    HourString = '0' + String(now.hour()) ;
  else
    HourString = String(now.hour()) ;

  if (now.minute() < 10)
    MinuteString = '0' + String(now.minute());
  else
    MinuteString = String(now.minute());

  if (now.second() < 10)
    SecondString = '0' + String(now.second());
  else
    SecondString = String(now.second());

  TimeString = HourString + ':';
  TimeString += MinuteString + ':';
  TimeString += SecondString + '.' ;
  TimeString += MillisString + ' ';
  // END DATE & TIME Stamping - Do not combine Date and time strings into one string.

  //------------------------------------------------------------

  // Call Tare Function
  if (buttonState == false) {
    buttonTimer = 0;
  }
  else {
    buttonTimer++;
  }
  buttonState = digitalRead(buttonPin);
  if (buttonTimer >= 20) {
    Tare();
    buttonTimer = 0;
  }
  // End Call Tare Function

  //------------------------------------------------------------

  // FORCE DATA

  // Collect raw force data and apply calibrations & tares
  for (FSR = 0; FSR < 6; FSR++) {
    fsrReading[FSR] = analogRead(fsrPin[FSR]);
    fsrForce[FSR] = Calibs[FSR][0] / denom
                    * pow(e, Calibs[FSR][1] / denom * fsrReading[FSR])
                    - Calibs[FSR][0] / denom - Tares[FSR];
  }
  Forces[0] = fsrForce[2] + fsrForce[4] + fsrForce[5] ;
  Forces[1] = fsrForce[0] + fsrForce[1] + fsrForce[3] ;

  // find the average of the current mean block
  if (istared) {
    for (alpha = 0; alpha < 2; alpha++) {
      aveSum[alpha] -= sums[alpha][sumcounter] / blockSize;
      sums[alpha][sumcounter] = Forces[alpha];
      aveSum[alpha] += Forces[alpha] / blockSize;
    }
    
    sumcounter++;
    aveSum[2] = aveSum[0] + aveSum[1];

    if (sumcounter == blockSize)
      sumcounter = 0;
  }
//  Serial.print("AveSum:");
//  Serial.println(aveSum[2]);
  //    if (valuecount < blockSize)
  //      valuecount++;
  //    // Signal smoothing algorithm
  //    // Undo previous adjustment
  //    else {
  //      aveSum -= positive * deviation / (blockSize ^ 2);
  //      positive = 0; // net number of data points above or below mean
  //      deviation = 0; // sum of distances from the mean
  //      for (int i = 0; i < blockSize; i++) {
  //        positive += ((sums[i] - aveSum) >= 0) ? 1 : -1;
  //        deviation += abs(sums[i] - aveSum);
  //      }
  //      aveSum += (positive/abs(positive)) * deviation / (blockSize ^ 2);
  //      Serial.print("Positive");
  //      Serial.println(positive);
  //      Serial.print("aveSum:");
  //      Serial.println(aveSum);
  //    }

  // END FORCE DATA

  //-----------------------------------------------------------

  //  Begin resulting actions from force measurement
  // Writing to SD Card
  // Coloring LEDs
  if (aveSum[2] > Threshhold) {
    if (!InThresh)
      StanceStart = millis();
      StanceStartTime = TimeString;
    InThresh = true;
    threshCycles++;
    
//    if (aveSum[2] > TempMaxForce) {
//      TempMaxTime = TimeString;
//      TempMaxForce = aveSum[2];
//      TempMaxRear = aveSum[1];
//    }
  }
  else {
    if (InThresh && threshCycles > 20){ // Ensures that the force values are not wavering at threshhold.
      writeCycle = true;
      SwingStart = millis();
    }
    InThresh = false;
    threshCycles = 0;
  }

  if (!InThresh) {
    strip.show();
    if (writeCycle) {
      File dataFile = SD.open("data.txt", FILE_WRITE);
      if (dataFile) {
        dataFile.print(DateString);
        dataFile.print(StanceStartTime);
        dataFile.print(SwingStart - StanceStart);
        dataFile.close();
      }
      writeCycle = false;
//      if (TempMaxForce >= AccLow && TempMaxForce <= AccHigh) {
//        justRight();
//        lightTime = millis();
//
//      }
//      else if (TempMaxForce < AccLow) {
//        tooLittle();
//        lightTime = millis();
//      }
//      else if (TempMaxForce > AccHigh) {
//        tooMuch();
//        lightTime = millis();
//      }
//      TempMaxForce = 0;
    }
  }
  // Allows the LEDs to remain lit for a short time
//  if (millis() - lightTime > showTime || InThresh)
//    noColor();
}

//------------------------------------------------------------
//------------------------------------------------------------

// LED functions
void tooLittle() {
  for (i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(0, 0, 127)); // All Blue
  }
}

void justRight() {
  for (i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(0, 127, 0)); // All Green
  }
}

void tooMuch() {
  for (i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(127, 0, 0)); // All Red
  }
}

void noColor() {
  for (i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(0, 0, 0)); // All Black
  }
}
// END LED functions

//------------------------------------------------------------
//------------------------------------------------------------

// TARE Function
// Resets the intercepts of the calibration equations to tare the force values.
// A 5-second measurement is taken and averaged, then subtracted off.

void Tare() {
  tooMuch();
  strip.show();
  long TareStart = millis();
  long TareEnd = TareStart + 5000; // 5 seconds of data
  int AverageCounter = 0;
  float Tare[6] = {};
  InThresh = false;
  istared = true;
  valuecount = 0;
  // Reset moving average array to 0
  for (alpha = 0; alpha < 2 ; alpha ++) {
    for (int c; c < 10; c++)
      sums[alpha][c] = 0;
  }
  while (millis() <= TareEnd) {
    AverageCounter++;
    for (FSR = 0; FSR < 6; FSR++) {
      fsrReading[FSR] = analogRead(fsrPin[FSR]);
      Tare[FSR] += fsrForce[FSR] = Calibs[FSR][0] / denom
                                   * pow(e, Calibs[FSR][1] / denom * fsrReading[FSR])
                                   - Calibs[FSR][0] / denom;
    }
  }
  for (FSR = 0; FSR < 6; FSR++) 
    Tares[FSR] = Tare[FSR] / AverageCounter ;

  noColor();
  strip.show();
}
// END TARE Function

//------------------------------------------------------------
//------------------------------------------------------------
