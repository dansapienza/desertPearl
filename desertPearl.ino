/* A basic STARTER script from the Cave Pearl Project 
that sleeps the datalogger and wakes from DS3231 RTC alarms*/

#include <Wire.h>
#include <SPI.h>
#include <RTClib.h>     // https://github.com/MrAlvin/RTClib
#include <LowPower.h>   // https://github.com/rocketscream/Low-Power
#include <SdFat.h>      // https://github.com/greiman/SdFat

SdFat sd;
SdFile file;
RTC_DS3231 RTC;

//Define Pins
#define CHIP_SELECT 10
#define RTC_INTERRUPT_PIN 2
#define BATTERY_PIN A0
#define WATER_POWER_PIN 4
#define WATER_PIN 3

#define DS3231_I2C_ADDRESS 0x68
#define SAMPLE_INTERVAL_MIN 1  // Integer 1-30, divisor of 60 - 1,2,3,4,5,6,10,15,20,30
#define CUTOFF_VOLTAGE 3400  // With Voltage regulator - 3400 mV, with no reg and 2 AA batteries - 2850mV (or more)

char CycleTimeStamp[ ] = "0000/00/00,00:00"; //16 ascii characters (without seconds)
float batteryVoltage;
volatile boolean clockInterrupt = false;  // interrupt flag
bool dailyToggle = false;

char FileName[12] = "data000.csv"; 
char FileName2[12] = "daly000.csv"; 
const char codebuild[] PROGMEM = __FILE__;  // loads the compiled source code directory & filename into a variable
const char header[] PROGMEM = "Timestamp, RTC temp(C),voltage,WaterOrNo"; //gets written to second line datalog.txt in setup

void setup() {
  pinMode(RTC_INTERRUPT_PIN,INPUT_PULLUP);  //RTC alarms low, so need pullup on the D2 line 
  pinMode(CHIP_SELECT, OUTPUT); digitalWrite(CHIP_SELECT, HIGH); //ALWAYS pullup the ChipSelect pin with the SD library
  pinMode(11, OUTPUT);digitalWrite(11, HIGH); //pullup the MOSI pin on the SD card module
  pinMode(12, INPUT_PULLUP); //pullup the MISO pin on the SD card module
  pinMode(13, OUTPUT);digitalWrite(13, LOW); //pull DOWN the 13scl pin on the SD card (IDLES LOW IN MODE0)
  delay(1);

  Wire.begin();           // start the i2c interface for the RTC
  TWBR = 2;//speeds up I2C bus to 400 kHz bus - ONLY Use this on 8MHz Pro Mini's
  RTC.begin();            // start the RTC
  clearClockTrigger();    //stops RTC from holding the interrupt low if system reset just occured
  RTC.turnOffAlarm(1);    // particular to this RTC library, so might need to rewrite
  
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);   // 16 second delay for time to compile & upload
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);   // Also prevents powerbounce problems.
  
  createFile(FileName);
  createFile(FileName2);
  
  delay(25);
  LowPower.powerDown(SLEEP_60MS, ADC_OFF, BOD_OFF);   // SD cards can draw power for up to 1sec after file close...
  DIDR0 = 0x0F;  //  disables the digital inputs on analog 0..3 (analog 4&5 being used by I2C!)
  for(byte i = 3;i<10;i++) {
    pinMode(i,INPUT_PULLUP); //set unused digital pins to input pullup to save power
  }
}

void loop() {
  getTime();
  readBattery();
  if(dailyToggle == true) {
    oncePerDay();
  }
  oncePerInterval();
  setNextAlarm();   // also executes the oncePerDay function
  
  //——– sleep and wait for next RTC alarm ————–
  attachInterrupt(RTC_INTERRUPT_PIN, rtcISR, LOW);      // Enable interrupt on pin2
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_ON);   // Enter power down state with ADC module disabled to save power:
  
  //processor starts HERE AFTER THE RTC ALARM WAKES IT UP
  detachInterrupt(RTC_INTERRUPT_PIN);   // Immediately disable the interrupt on waking
}

boolean isThereWater() {
  digitalWrite(WATER_POWER_PIN,HIGH);
  delay(10);
  bool temp = digitalRead(WATER_PIN);
  digitalWrite(WATER_POWER_PIN,LOW);
  return temp;
}

void oncePerDay() {
  bool temp = isThereWater();
  writeToCard(FileName2,readRTCtemp(),batteryVoltage,temp);
  dailyToggle = false;
}

void oncePerInterval() {
  if(isThereWater() == HIGH) {
    writeToCard(FileName,readRTCtemp(),batteryVoltage,HIGH);
  }
}

void writeToCard(char fileToWrite[12], float rtcTemp, float battVolt, byte waterOrNot) {
  file.open(fileToWrite, O_WRITE | O_APPEND); // open the file for write at end like the Native SD library
  delay(20);//LowPower.powerDown(SLEEP_30MS, ADC_OFF, BOD_OFF);
  file.print(CycleTimeStamp);
  file.print(",");    
  file.print(rtcTemp);
  file.print(",");    
  file.print(battVolt);
  file.print(",");
  file.print(waterOrNot);
  file.println(",");
  file.close();
}

void getTime() {
  //  This function reads the time and disables the RTC alarm
  DateTime now = RTC.now(); //this reads the time from the RTC
  //  loads the time into a string variable:
  sprintf(CycleTimeStamp, "%04d/%02d/%02d %02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute());
  // We set the clockInterrupt in the ISR, deal with that now:
  if (clockInterrupt) {
    if (RTC.checkIfAlarm(1)) {       //Is the RTC alarm still on?
      RTC.turnOffAlarm(1);              //then turn it off.
    }
    clockInterrupt = false;                //reset the interrupt flag to false
  }
}

void setNextAlarm() {
  byte Alarmhour;
  byte Alarmminute;
  
  DateTime now = RTC.now(); //this reads the time from the RTC
  Alarmhour = now.hour();
  Alarmminute = now.minute() + SAMPLE_INTERVAL_MIN;
  
  //============Set the next alarm time =============
  // check for roll-overs // NO DAY FUNCTION, so does it not have a day, is hour/minute enough?
  if (Alarmminute > 59) { //error catching the 60 rollover!
    Alarmminute = 0;
    Alarmhour = Alarmhour + 1;
    if (Alarmhour > 23) {
      Alarmhour = 0;
      dailyToggle = true; // ACtivates the once per day write on the next write.
    }
  }
  
  RTC.setAlarm1Simple(Alarmhour, Alarmminute);
  RTC.turnOnAlarm(1);
}

void readBattery() {
  int BatteryReading = 0;
  BatteryReading = analogRead(BATTERY_PIN); 
  // for stand-alone ProMini loggers, I monitor the main battery voltage (which is > Aref)
  // with a voltage divider: RawBattery - 10MΩ - A0 - 3.3MΩ - GND
  // with a 104 ceramic capacitor accross the 3.3MΩ resistor to enable the ADC to read the high impedance resistors
  batteryVoltage = float((BatteryReading/ 255.75)*3.3);
  if (int(batteryVoltage*1000) < CUTOFF_VOLTAGE) { 
    if (file.isOpen()) {
      file.close();
    }
    LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_ON);   //  Shut down the logger because of low voltage reading
  }
}

float readRTCtemp() { // read the RTC temp register and print that out
  float RTCtemp;
  byte tMSB = 0;
  byte tLSB = 0;
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0x11);                     //the register where the temp data is stored
  Wire.endTransmission();
  Wire.requestFrom(DS3231_I2C_ADDRESS, 2);   //ask for two bytes of data
  if (Wire.available()) {
    tMSB = Wire.read();            //2’s complement int portion
    tLSB = Wire.read();            //fraction portion
    RTCtemp = ((((short)tMSB << 8) | (short)tLSB) >> 6) / 4.0;  // Allows for readings below freezing: thanks to Coding Badly
    RTCtemp = (RTCtemp * 1.8) + 32.0; // To Convert Celcius to Fahrenheit
  }
  else RTCtemp = 0; //if rtc_TEMP_degC is equal to 0, then the RTC has an error.
  return RTCtemp;
}

void rtcISR() { // This is the Interrupt subroutine that only executes when the RTC alarm goes off
  clockInterrupt = true;
}

void clearClockTrigger() {
  byte bytebuffer1;
  Wire.beginTransmission(0x68);   //  Tell devices on the bus we are talking to the DS3231
  Wire.write(0x0F);               //  status register
  Wire.endTransmission();         //  Read the flag
  Wire.requestFrom(0x68,1);       //  Read one byte
  bytebuffer1=Wire.read();        //  Skip it.
  Wire.beginTransmission(0x68);   //  Tell devices on the bus we are talking to the DS3231 
  Wire.write(0x0F);               //  status register
  Wire.write(0b00000000);         //  Write 0s
  Wire.endTransmission();
  clockInterrupt=false;           //Finally clear the flag we use to indicate the trigger occurred
}

void createFile(char fileToWrite[12]) {
  if (!file.open(fileToWrite, O_CREAT | O_EXCL | O_WRITE)) { // note that restarts often generate empty log files!
    // O_CREAT = create the file if it does not exist,  O_EXCL = fail if the file exists, O_WRITE - open for write
    for (int i = 1; i < 512; i++) {
      delay(5);
      if(fileToWrite == FileName) {
        snprintf(fileToWrite, sizeof(FileName), "data%03d.csv", i);  //concatenates the next number into the filename
      }
      else if(fileToWrite == FileName2) {
        snprintf(fileToWrite, sizeof(FileName), "daly%03d.csv", i);  //concatenates the next number into the filename
      }
      if (file.open(fileToWrite, O_CREAT | O_EXCL | O_WRITE)) { break; } //if you can open a file with the new name, break out of the loop
    }
  }
  delay(25);
  file.println((__FlashStringHelper*)codebuild);    // writes the entire path + filename to the start of the data file
  file.println();file.println((__FlashStringHelper*)header); //write the header information in the new file
  file.close();
  delay(25);
}
