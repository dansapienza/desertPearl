/* A basic STARTER script from the Cave Pearl Project 
that sleeps the datalogger and wakes from DS3231 RTC alarms*/

#include <Wire.h>
#include <SPI.h>
#include <RTClib.h>     // library from https://github.com/MrAlvin/RTClib  Note: there are many other DS3231 libs availiable
#include <LowPower.h>   // https://github.com/rocketscream/Low-Power and https://github.com/rocketscream/Low-Power 
#include <SdFat.h>      // needs 512 byte ram buffer! see https://github.com/greiman/SdFat

// #define ECHO_TO_SERIAL       // define that enables debugging output to the serial monitor

SdFat sd; /*Create the objects to talk to the SD card*/
SdFile file;
const int chipSelect = 10;    //CS moved to pin 10 on the arduino

RTC_DS3231 RTC; // creates an RTC object in the code
// variables for reading the RTC time & handling the INT(0) interrupt it generates
#define SampleIntervalMinutes 1  // Whole numbers 1-30 only, must be a divisor of 60
// CHANGE SampleIntervalMinutes to the number of minutes you want between samples!
#define DS3231_I2C_ADDRESS 0x68
#define RTC_INTERRUPT_PIN 2
byte Alarmhour;
byte Alarmminute;
char CycleTimeStamp[ ] = "0000/00/00,00:00"; //16 ascii characters (without seconds)
volatile boolean clockInterrupt = false;  //this flag is set to true when the RTC interrupt handler is executed
//variables for reading the DS3231 RTC temperature register
float rtc_TEMP_degC;
byte tMSB = 0;
byte tLSB = 0;

char FileName[12] = "data000.csv"; 
const char codebuild[] PROGMEM = __FILE__;  // loads the compiled source code directory & filename into a varaible
const char header[] PROGMEM = "Timestamp, RTC temp(C),RAILvoltage,AnalogReading,Add more headers here"; //gets written to second line datalog.txt in setup

//track of the rail voltage using the internal 1.1v bandgap trick
uint16_t VccBGap = 9990; 
uint16_t CutoffVoltage = 3400; 
//if running of of 2x AA cells (with no regulator) the input cutoff voltage should be 2850 mV (or higher)
//if running a unit with the voltage regulator the input cutoff voltage should be 3400 mV

//example variables for analog pin reading
#define analogPin 0
int AnalogReading = 0;
#define BatteryPin 6
int BatteryReading = 0;
float batteryVoltage = 9999.9;
//Global variables
//******************
byte bytebuffer1 =0;

//======================================================================================================================
//  *  *   *   *   *   *   SETUP   *   *   *   *   *
//======================================================================================================================

void setup() {
  
  
  pinMode(RTC_INTERRUPT_PIN,INPUT_PULLUP);//RTC alarms low, so need pullup on the D2 line 
  //Note using the internal pullup is not needed if you have hardware pullups on SQW line, and most RTC modules do.
  
  // Setting the SPI pins high helps some sd cards go into sleep mode 
  // the following pullup resistors only needs to be enabled for the ProMini builds - not the UNO loggers
  pinMode(chipSelect, OUTPUT); digitalWrite(chipSelect, HIGH); //ALWAYS pullup the ChipSelect pin with the SD library
  //and you may need to pullup MOSI/MISO, usually MOSIpin=11, and MISOpin=12 if you do not already have hardware pulls
  pinMode(11, OUTPUT);digitalWrite(11, HIGH); //pullup the MOSI pin on the SD card module
  pinMode(12, INPUT_PULLUP); //pullup the MISO pin on the SD card module
  //pinMode(13, OUTPUT);digitalWrite(13, LOW); //pull DOWN the 13scl pin on the SD card (IDLES LOW IN MODE0)
  // NOTE: In Mode (0), the SPI interface holds the CLK line low when the bus is inactive, so DO NOT put a pullup on it.
  // NOTE: when the SPI interface is active, digitalWrite() cannot effect MISO,MOSI,CS or CLK
  delay(1);
  
  // Serial.begin(9600);    // Open serial communications and wait for port to open:
  Wire.begin();          // start the i2c interface for the RTC
  TWBR = 2;//for 400 kHz bus @ 8MHz CPU speed ONLY // AT24c256 ok @ 400kHz http://www.atmel.com/Images/doc0670.pdf  
  RTC.begin();           // start the RTC

  // check RTC
  //****************
  clearClockTrigger(); //stops RTC from holding the interrupt low if system reset just occured
  RTC.turnOffAlarm(1);
  DateTime now = RTC.now();
  sprintf(CycleTimeStamp, "%04d/%02d/%02d %02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute());
  
  // 24 second delay here for time to compile & upload if you just connected the UART
  // this delay also prevents short power connection bounces from writing multiple file headers
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  

  #ifdef ECHO_TO_SERIAL
    Serial.print(F("Initializing SD card-"));
  #endif
  // print lines in the setup loop only happen once
  // see if the card is present and can be initialized
  if (!sd.begin(chipSelect,SPI_FULL_SPEED)) {
    #ifdef ECHO_TO_SERIAL
      Serial.println(F("Card failed, or not present"));
    #endif
    // don’t do anything more:
    return;
  }
  // Serial.println(F("card initialized."));
  delay(50); //sd.begin hits the power supply pretty hard
  
  // Find the next availiable file name
  //===================================
  // 2 GB or smaller cards should be formatted FAT16 - FAT16 has a limit of 512 entries in root
  // O_CREAT = create the file if it does not exist,  O_EXCL = fail if the file exists, O_WRITE - open for write
  if (!file.open(FileName, O_CREAT | O_EXCL | O_WRITE)) { // note that restarts often generate empty log files!
    for (int i = 1; i < 512; i++) {
      delay(5);
      snprintf(FileName, sizeof(FileName), "data%03d.csv", i);//concatenates the next number into the filename
      if (file.open(FileName, O_CREAT | O_EXCL | O_WRITE)) // O_CREAT = create file if not exist, O_EXCL = fail if file exists, O_WRITE - open for write
      {
        break; //if you can open a file with the new name, break out of the loop
      }
    }
  }
  delay(25);
  //write the header information in the new file
  file.println((__FlashStringHelper*)codebuild); // writes the entire path + filename to the start of the data file
  file.println();file.println((__FlashStringHelper*)header);
  file.close(); delay(25);
  LowPower.powerDown(SLEEP_60MS, ADC_OFF, BOD_OFF);// SD cards can draw power for up to 1sec after file close...
  

  #ifdef ECHO_TO_SERIAL
    Serial.print(F("Data Filename:")); Serial.println(FileName); Serial.println(); Serial.flush();
  #endif

  DIDR0 = 0x0F;  //  disables the digital inputs on analog 0..3 (analog 4&5 being used by I2C!)

  //set unused digital pins to input pullup to save power
  for(byte i = 3;i<10;i++) {
    pinMode(i,INPUT_PULLUP); //only if you do not have the onewire bus connected
  }
  #ifndef ECHO_TO_SERIAL
   pinMode(0,INPUT_PULLUP); //but not if we are on usb- then these pins are RX & TX 
   pinMode(1,INPUT_PULLUP);
  #endif
  
//====================================================================================================
}   //   terminator for setup
//=====================================================================================================

// ========================================================================================================
//      *  *  *  *  *  *  MAIN LOOP   *  *  *  *  *  *
//========================================================================================================

void loop() {
  //—–This part reads the time and disables the RTC alarm
 
  DateTime now = RTC.now(); //this reads the time from the RTC
  sprintf(CycleTimeStamp, "%04d/%02d/%02d %02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute());
  //loads the time into a string variable - don’t record seconds in the time stamp because the interrupt to time reading interval is <1s, so seconds are always ’00’
  
  // We set the clockInterrupt in the ISR, deal with that now:
  if (clockInterrupt) {
    if (RTC.checkIfAlarm(1)) {       //Is the RTC alarm still on?
      RTC.turnOffAlarm(1);              //then turn it off.
    }
    #ifdef ECHO_TO_SERIAL
       //print (optional) debugging message to the serial window if you wish
       Serial.print("RTC Alarm on INT-0 triggered at ");
       Serial.println(CycleTimeStamp);
    #endif
    clockInterrupt = false;                //reset the interrupt flag to false
  }
  
  // read the RTC temp register and print that out
  // Note: the DS3231 temp registers (11h-12h) are only updated every 64seconds
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0x11);                     //the register where the temp data is stored
  Wire.endTransmission();
  Wire.requestFrom(DS3231_I2C_ADDRESS, 2);   //ask for two bytes of data
  if (Wire.available()) {
    tMSB = Wire.read();            //2’s complement int portion
    tLSB = Wire.read();             //fraction portion
    rtc_TEMP_degC = ((((short)tMSB << 8) | (short)tLSB) >> 6) / 4.0;  // Allows for readings below freezing: thanks to Coding Badly
    rtc_TEMP_degC = (rtc_TEMP_degC * 1.8) + 32.0; // To Convert Celcius to Fahrenheit
  }
  else {
    rtc_TEMP_degC = 0;
    //if rtc_TEMP_degC contains zero, then you know you had a problem reading the data from the RTC!
  }
  #ifdef ECHO_TO_SERIAL
    Serial.print(F(". TEMPERATURE from RTC is: "));
    Serial.print(rtc_TEMP_degC);
    Serial.println(F(" Fahrenheit"));
  #endif

  // You could read in other variables here …like the analog pins, I2C sensors, etc
  AnalogReading = analogRead(analogPin); 

  //========== Now write the data to the SD card ===========
  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  
  file.open(FileName, O_WRITE | O_APPEND); // open the file for write at end like the Native SD library
    delay(20);//LowPower.powerDown(SLEEP_30MS, ADC_OFF, BOD_OFF);
    file.print(CycleTimeStamp);
    file.print(",");    
    file.print(rtc_TEMP_degC);
    file.print(",");    
    file.print(VccBGap);
    file.print(",");
    file.print(AnalogReading);
    file.println(",");
    file.close();
    //the SD card is the biggest load on the system, and so it is a good test of the battery under load

  //------- if you are running from a raw battery---------------------
  /* bVccBGap = getRailVoltage(); //If you are running from raw battery power (with no regulator) vcc = the battery voltage
  if (VccBGap < CutoffVoltage) { 
      if (file.isOpen()) {
        file.close();
      }
      LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_ON); //shut down the logger because of low voltage reading
  }
  //------------ OR -------------------
  //------- if you are running with the standard regulator ---------------------
  */
  BatteryReading = analogRead(BatteryPin); 
  // for stand-alone ProMini loggers, I monitor the main battery voltage (which is > Aref)
  // with a voltage divider: RawBattery - 10MΩ - A0 - 3.3MΩ - GND
  // with a 104 ceramic capacitor accross the 3.3MΩ resistor to enable the ADC to read the high impedance resistors
  batteryVoltage = float((BatteryReading/ 255.75)*3.3);
  if (int(batteryVoltage*1000) < CutoffVoltage) { 
    if (file.isOpen()) {
      file.close();
    }
    #ifdef ECHO_TO_SERIAL
      Serial.print("DEAD BATTERY!!!!")
    #endif
    LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_ON); //shut down the logger because of low voltage reading
  }
  
  // print to the serial port too:
  #ifdef ECHO_TO_SERIAL
      Serial.print(CycleTimeStamp);
      Serial.print(",");    
      Serial.print(rtc_TEMP_degC);
      Serial.print(",");    
      Serial.print(VccBGap);
      Serial.print(",");
      Serial.println(AnalogReading);
  #endif


  //============Set the next alarm time =============
  Alarmhour = now.hour();
  Alarmminute = now.minute() + SampleIntervalMinutes;

  // check for roll-overs // NO DAY FUNCTION, so does it not have a day, is hour/minute enough?
  if (Alarmminute > 59) { //error catching the 60 rollover!
    Alarmminute = 0;
    Alarmhour = Alarmhour + 1;
    if (Alarmhour > 23) {
      Alarmhour = 0;
      // put ONCE-PER-DAY code here -it will execute on the 24 hour rollover
    }
  }
  // then set the alarm
  RTC.setAlarm1Simple(Alarmhour, Alarmminute);
  RTC.turnOnAlarm(1);
  if (RTC.checkAlarmEnabled(1)) {
    //you would comment out most of this message printing
    //if your logger was actually being deployed in the field
    #ifdef ECHO_TO_SERIAL
      Serial.print(F("RTC Alarm Enabled!"));
      Serial.print(F(" Going to sleep for : "));
      Serial.print(SampleIntervalMinutes);
      Serial.println(F(" minutes"));
      Serial.println();
      Serial.flush();//adds a carriage return & waits for buffer to empty
    #endif
    }

  //——– sleep and wait for next RTC alarm ————–
  // Enable interrupt on pin2 & attach it to rtcISR function:
  attachInterrupt(0, rtcISR, LOW);

  
  // Enter power down state with ADC module disabled to save power:
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_ON);
  
  //processor starts HERE AFTER THE RTC ALARM WAKES IT UP
  
  detachInterrupt(0); // immediately disable the interrupt on waking
}
//================ END of the MAIN LOOP ===============

// This is the Interrupt subroutine that only executes when the RTC alarm goes off
void rtcISR() {
    clockInterrupt = true;
  }

void clearClockTrigger() {
  Wire.beginTransmission(0x68);   //Tell devices on the bus we are talking to the DS3231
  Wire.write(0x0F);               //Tell the device which address we want to read or write
  Wire.endTransmission();         //Before you can write to and clear the alarm flag you have to read the flag first!
  Wire.requestFrom(0x68,1);       //Read one byte
  bytebuffer1=Wire.read();        //In this example we are not interest in actually using the bye
  Wire.beginTransmission(0x68);   //Tell devices on the bus we are talking to the DS3231 
  Wire.write(0x0F);               //status register
  Wire.write(0b00000000);         //Write the byte.  The last 0 bit resets Alarm 1 //is it ok to just set these all to zeros?
  Wire.endTransmission();
  clockInterrupt=false;           //Finally clear the flag we use to indicate the trigger occurred
}
//=================================================
//----------Voltage monitoring functions ----------
//=================================================

int getRailVoltage() {  // from http://forum.arduino.cc/index.php/topic,38119.0.html
  int result; 
  const long InternalReferenceVoltage = 1100L;

  for (int i = 0; i < 5; i++) { // have to loop 4 times before it yeilds consistent results - cap on aref needs to settle

    #if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
      // For mega boards
      // const long InternalReferenceVoltage = 1100L;  // Adjust this value to your boards specific internal BG voltage x1000
      // REFS1 REFS0          --> 0 1, AVcc internal ref.
      // MUX4 MUX3 MUX2 MUX1 MUX0  --> 11110 1.1V (VBG)
      ADMUX = (0 << REFS1) | (1 << REFS0) | (0 << ADLAR) | (1 << MUX4) | (1 << MUX3) | (1 << MUX2) | (1 << MUX1) | (0 << MUX0);
    #else
      // For 168/328 boards
      // const long InternalReferenceVoltage = 1100L;
      // Adust this value to your boards specific internal BG voltage x1000
      // REFS1 REFS0          --> 0 1, AVcc internal ref.
      // MUX3 MUX2 MUX1 MUX0  --> 1110 1.1V (VBG)
      ADMUX = (0 << REFS1) | (1 << REFS0) | (0 << ADLAR) | (1 << MUX3) | (1 << MUX2) | (1 << MUX1) | (0 << MUX0);
    #endif
      delay(1);
      // Start a conversion
      ADCSRA |= _BV( ADSC );
      // Wait for it to complete
      while ( ( (ADCSRA & (1 << ADSC)) != 0 ) );
      // Scale the value
      result = (((InternalReferenceVoltage * 1023L) / ADC) + 5L); //scale Rail voltage in mV
      // note that you can tune the accuracy of this function by changing InternalReferenceVoltage to match your board
      // just tweak the constant till the reported rail voltage matches what you read with a DVM!
  }     // end of for (int i=0; i < 5; i++) loop

  ADMUX = bit (REFS0) | (0 & 0x07); analogRead(A0); // re-set AVcc to rail and select input port A0 + engage new Aref

//  if (result < LowestVcc) {LowestVcc = result;} //as the batteries loose capacity, the delta btween these two numbers increases
//  if (result > HighestVcc) {HighestVcc = result;}
  
  return result;
//===================================
}  // terminator for getRailVoltage()
//===================================
