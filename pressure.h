#ifndef pressure_h
#define pressure_h

#include <Wire.h>
#include "MS5837.h"
#include <Adafruit_MPL115A2.h>

Adafruit_MPL115A2 mpl115a2;
MS5837 sensor;
float offSetPrs;

void start_pressure_sensors() {
  Wire.begin();

  // Initialize pressure sensor
  // Returns true if initialization was successful
  // We can't continue with the rest of the program unless we can initialize the sensor
  while (!sensor.init()) {
    delay(250);
  }
  mpl115a2.begin();
  
  sensor.setModel(MS5837::MS5837_30BA);
  sensor.setFluidDensity(997); // kg/m^3 (freshwater, 1029 for seawater)
  float pressure1 = 0;
  float pressure2 = 0;
  for(byte i = 1;i<6;i++) {
    sensor.read();
    pressure1 = pressure1 + sensor.pressure();
    pressure2 = pressure2 + mpl115a2.getPressure();  
    delay(15);
  }
  pressure1 = pressure1/5;
  pressure2 = pressure2/5;
  offSetPrs = pressure2*10 - pressure1;
}

float get_pressure_1() {
  float pressure1 = 0;
  for(byte i = 1;i<6;i++) {
    sensor.read();
    pressure1 = pressure1 + sensor.pressure();
    delay(15);
  }
  pressure1 = pressure1/5;
  return pressure1;
}

float get_pressure_2() {
  float pressure2 = 0;
  for(byte i = 1;i<6;i++) {
    pressure2 = pressure2 + mpl115a2.getPressure();  
    delay(15);
  }
  pressure2 = pressure2/5;
  return pressure2*10;
}

#endif
