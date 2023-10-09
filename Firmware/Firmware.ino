/*
  Copyright 2023 HadesVR
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,INCLUDING BUT NOT LIMITED TO
  THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <Wire.h>
#include <SPI.h>
#include <EEPROM.h>
#include "RF24.h"
#include "FastIMU.h"
#include "Madgwick.h"

//==========================================================================================================
//************************************ USER CONFIGURABLE STUFF HERE*****************************************
//==========================================================================================================

//#define SERIAL_DEBUG
#define IMU_ADDRESS     0x68                // You can find it out by using the IMUIdentifier example
MPU9250 IMU;                                // IMU type

//#define RIGHT_CONTROLLER                    // Leave the desired controller role uncommented to set.
#define LEFT_CONTROLLER

#define BatLevelMax         990
#define JoyXMin             0             //These values are for PS4 analog sticks
#define JoyXMax             1023          //different sticks might have different maximums and minimums
#define JoyYMin             0             //along with different deadzones, if your controller drifts
#define JoyYMax             1023          //you'll need to increase the deadzones
#define JoyXDeadZoneMin     487
#define JoyXDeadZoneMax     537
#define JoyYDeadZoneMin     487
#define JoyYDeadZoneMax     537

//==========================================================================================================
//************************************* Pin definitions ****************************************************
//==========================================================================================================
#define NRFCE               A0
#define VbatPin             A1
//Menu/Sys                  A2
#define JoyYPin             A3
//SDA                       A4
//SCL                       A5
#define TriggerPin          A6
#define JoyXPin             A7

#define GripPin             0
//TXO                       1
#define JoyBtnPin           2
#define pinkyFingerPin      3
#define ringFingerPin       4
#define middleFingerPin     5  
//Menu/Sys                  6  
#define Led1Pin             7
#define ModePin             8
#define Led2Pin             9

#ifdef RIGHT_CONTROLLER
#define MenuPin             A2
#define SysPin              6  
#else
#define MenuPin             6
#define SysPin              A2 
#endif
//==========================================================================================================
calData calib =
{ false,                   //data valid?
  {0, 0, 0},              //Accel bias
  {0, 0, 0},              //Gyro bias
  {0, 0, 0},              //Mag bias
  {1, 1, 1},              //Mag Scale
};

#define HTC_SysClick        0x0001
#define HTC_MenuClick       0x0002
#define HTC_ThumbstickClick 0x0004
#define HTC_GripClick       0x0008
#define HTC_ThumbstickTouch 0x0010
//==========================================================================================================
//************************************* Data packet stuff *************************************************
//==========================================================================================================
struct ctrlData {
  int16_t qW;
  int16_t qX;
  int16_t qY;
  int16_t qZ;
  int16_t accX;
  int16_t accY;
  int16_t accZ;
  uint16_t BTN;
  uint8_t  trigg;
  int8_t  axisX;
  int8_t  axisY;
  int8_t  trackY;
  uint8_t  vBAT;
  uint8_t  fingerThumb;
  uint8_t  fingerIndex;
  uint8_t  fingerMiddle;
  uint8_t  fingerRing;
  uint8_t  fingerPinky;
  uint8_t  gripForce;
  uint16_t Data;
};
ctrlData data;
//==========================================================================================================
//**************************************** Analog inputs ***************************************************
//==========================================================================================================
int tracky;
int trackoutput;
int axisX;
int axisY;
bool joyTouch = false;
//==========================================================================================================
//**************************************** RF Data stuff ***************************************************
//==========================================================================================================
RF24 radio(NRFCE, 10);

#ifdef RIGHT_CONTROLLER
uint64_t Pipe = 0xF0F0F0F0E1LL; //right
#warning Compiling Right controller role...

#elif defined(LEFT_CONTROLLER)
uint64_t Pipe = 0xF0F0F0F0D2LL; //left
#warning Compiling Left controller role...

#else
#error No Valid controller role selected!
#endif
//==========================================================================================================
//**************************************** IMU variables ***************************************************
//==========================================================================================================
AccelData IMUAccel;
GyroData IMUGyro;
MagData IMUMag;
//==========================================================================================================
//************************************** Filter variables **************************************************
//==========================================================================================================
Madgwick filter;
static const float MadgwickBeta = 0.16f;
float rot = 0.f;
//==========================================================================================================

bool joyClickInvert = true;
bool middleBTNPressed = false;
int batteryPercent = 100;
void setup() {
  Wire.begin();
  Wire.setClock(400000); //400khz clock
  pinMode(SysPin, INPUT_PULLUP);
  pinMode(MenuPin, INPUT_PULLUP);
  pinMode(GripPin, INPUT_PULLUP);
  pinMode(JoyBtnPin, INPUT_PULLUP);
  pinMode(TriggerPin, INPUT_PULLUP);
  pinMode(middleFingerPin, INPUT_PULLUP);
  pinMode(ringFingerPin, INPUT_PULLUP);
  pinMode(pinkyFingerPin, INPUT_PULLUP);
  pinMode(ModePin, INPUT_PULLUP);
  pinMode(Led1Pin, OUTPUT);
  pinMode(Led2Pin, OUTPUT);
  digitalWrite(Led1Pin, HIGH);
  digitalWrite(Led2Pin, HIGH);

#ifdef SERIAL_DEBUG
  Serial.begin(38400);
  debugPrintln("[INFO]\tSerial debug active.");
  #ifdef RIGHT_CONTROLLER
  debugPrintln("[INFO]\tController role: RIGHT");
  #else
  debugPrintln("[INFO]\tController role: LEFT");
  #endif
#endif

  radio.begin();
  radio.setPayloadSize(32);
  radio.setPALevel(RF24_PA_MAX);
  radio.setDataRate(RF24_2MBPS);
  radio.openWritingPipe(Pipe);
  radio.startListening();
  radio.setAutoAck(false);

  int err = IMU.init(calib, IMU_ADDRESS);
  if (err != 0)
  {
    debugPrint("[ERROR]\t Couldn't initialize IMU of type: ");
    debugPrint(IMU.IMUType());
    debugPrint(", Got error: ");
    debugPrintln(err);
    while (true) {
      blinkLEDS(1000, 1, false);
    }
  }
  else{
    debugPrint("[INFO]\t");
    debugPrint(IMU.IMUName());
    debugPrintln(" Initialized.");
  }
  if (!radio.isChipConnected())
  {
    debugPrintln("[ERROR]\tCould not initialize NRF24L01");
    while (true) {
      blinkLEDS(200, 1, false);
    }
  }
  else
  {
    debugPrintln("[INFO]\tNRF24L01 Initialized.");
  }

  EEPROM.get(210, calib);

  bool calDone = !calib.valid;                             //check if calibration values are on flash
  while (calDone)
  {
    debugPrintln("[INFO]\tCalibration not done!");
    if (!digitalRead(ModePin))
    {
      calDone = false;
    }
    blinkLEDS(200,1,true);
  }
  if (!digitalRead(ModePin)) {
    if (!digitalRead(MenuPin)) {
      blinkLEDS(500, 1, false);
      debugPrintln("[INFO]\tAccelerometer and gyroscope calibration mode.");
      debugPrintln("[INFO]\tKeep IMU completely still on flat and level surface.");
      delay(8000);
      IMU.calibrateAccelGyro(&calib);
      debugPrintln("[INFO]\tAccel & Gyro calibration complete!");

      if (!IMU.hasMagnetometer()) {
        calib.valid = true;
        blinkLEDS(500, 1, false);
      }
    }
    else {
      if (IMU.hasMagnetometer()) {
        blinkLEDS(500, 2, false);
        debugPrintln("[INFO]\tMagnetic calibration mode.");
        debugPrintln("[INFO]\tMove IMU in figure 8 until done.");
        delay(3000);
        IMU.calibrateMag(&calib);
        calib.valid = true;
        debugPrintln("[INFO]\tMagnetic calibration complete!");
        delay(1000);
        blinkLEDS(500, 1, false);
      }
    }
    printCalibration();
    debugPrintln("[INFO]\tWriting values to EEPROM!");
    EEPROM.put(210, calib);
    delay(3000);
  }

  //initialize controller data.
  data.qW = 1;
  data.qX = 0;
  data.qY = 0;
  data.qZ = 0;
  data.BTN = 0;
  data.trigg = 0;
  data.axisX = 0;
  data.axisY = 0;
  data.trackY = 0;
  data.vBAT = 0;
  data.fingerThumb = 0;
  data.fingerIndex = 0;
  data.fingerMiddle = 0;
  data.fingerRing = 0;
  data.fingerPinky = 0;
  data.Data = 0xFF;

  filter.begin(MadgwickBeta);

  err = IMU.init(calib, IMU_ADDRESS);
  if (err != 0)
  {
    debugPrint("[ERROR]\tIMU ERROR: ");
    debugPrintln(err);
    while (true);
  }

  digitalWrite(Led1Pin, LOW);//turn on LEDS
  digitalWrite(Led2Pin, LOW);
}

void loop() 
{
  IMU.update();
  IMU.getAccel(&IMUAccel);
  IMU.getGyro(&IMUGyro);

  if (IMU.hasMagnetometer()) {
    IMU.getMag(&IMUMag);
    filter.update(IMUGyro.gyroX, IMUGyro.gyroY, IMUGyro.gyroZ, IMUAccel.accelX, IMUAccel.accelY, IMUAccel.accelZ, IMUMag.magX, IMUMag.magY, IMUMag.magZ);
  }
  else {
    filter.updateIMU(IMUGyro.gyroX, IMUGyro.gyroY, IMUGyro.gyroZ, IMUAccel.accelX, IMUAccel.accelY, IMUAccel.accelZ);
  }

  rot += (abs(IMUGyro.gyroX) + abs(IMUGyro.gyroY) + abs(IMUGyro.gyroZ));
  if (rot > 64000.f) rot = 64000.f;
  rot *= 0.97f;
  filter.changeBeta(rot * (1.5 - 0.1) / 64000 + 0.1);

  if (!digitalRead(ModePin)) {
    if (!middleBTNPressed) {
      middleBTNPressed = true;
      if (joyClickInvert) {
        joyClickInvert = false;
      } else {
        joyClickInvert = true;
      }
    }
  }
  else {
    middleBTNPressed = false;
  }

  int btn = 0;
  axisX = analogRead(JoyXPin);
  axisY = analogRead(JoyYPin);

  if (axisX > JoyXDeadZoneMax || axisX < JoyXDeadZoneMin) {
    data.axisX = -map(axisX, JoyXMin, JoyXMax, -127, 127);
    if (joyClickInvert && digitalRead(JoyBtnPin)) {
      btn |= HTC_ThumbstickClick;
    }
    btn |= HTC_ThumbstickTouch;
  } else {
    data.axisX = 0;
  }

  if (axisY > JoyYDeadZoneMax || axisY < JoyYDeadZoneMin) {
    data.axisY = -map(axisY, JoyYMin, JoyYMax, -127, 127);
    if (joyClickInvert && digitalRead(JoyBtnPin)) {
      btn |= HTC_ThumbstickClick;
    }
    btn |= HTC_ThumbstickTouch;
  } else {
    data.axisY = 0;
  }

  if (!joyClickInvert) {
    if (!digitalRead(JoyBtnPin)) {
      btn |= HTC_ThumbstickClick;
    }
  }

  data.trigg = (map(analogRead(TriggerPin), 1024, 0, 0, 255));

  if (!digitalRead(SysPin)) {
    btn |= HTC_SysClick;
  }
  if (!digitalRead(MenuPin)) {
    btn |= HTC_MenuClick;
  }
  if (!digitalRead(JoyBtnPin)) {
    btn |= HTC_ThumbstickClick;
  }
  if (!digitalRead(GripPin)) {
    btn |= HTC_GripClick;
  }

  if (!digitalRead(middleFingerPin)) {
    data.fingerMiddle = 255;
  } else {
    data.fingerRing = 0;
  }
  if (!digitalRead(ringFingerPin)) {
    data.fingerRing = 255;
  } else {
    data.fingerRing = 0;
  }
  if (!digitalRead(pinkyFingerPin)) {
    data.fingerPinky = 255;
  } else {
    data.fingerPinky = 0;
  }
  
  data.BTN = btn;
  data.trackY = (trackoutput * 127);
  data.vBAT = getBattPercent();
  data.qW = (int16_t)(filter.getQuatW() * 32767.f);
  data.qX = (int16_t)(filter.getQuatY() * 32767.f);
  data.qY = (int16_t)(filter.getQuatZ() * 32767.f);
  data.qZ = (int16_t)(filter.getQuatX() * 32767.f);
  data.accX = (short)(IMUAccel.accelX * 2048);
  data.accY = (short)(IMUAccel.accelY * 2048);
  data.accZ = (short)(IMUAccel.accelZ * 2048);
  radio.stopListening();
  radio.write(&data, sizeof(ctrlData));
  radio.startListening();
}
void printCalibration()
{
  debugPrintln("[INFO]\tAccel biases X/Y/Z: ");
  debugPrint("[INFO]\t");
  debugPrint(calib.accelBias[0]);
  debugPrint(", ");
  debugPrint(calib.accelBias[1]);
  debugPrint(", ");
  debugPrintln(calib.accelBias[2]);
  debugPrintln("[INFO]\tGyro biases X/Y/Z: ");
  debugPrint("[INFO]\t");
  debugPrint(calib.gyroBias[0]);
  debugPrint(", ");
  debugPrint(calib.gyroBias[1]);
  debugPrint(", ");
  debugPrintln(calib.gyroBias[2]);
  if (IMU.hasMagnetometer()) {
    debugPrintln("[INFO]\tMag biases X/Y/Z: ");
    debugPrint("[INFO]\t");
    debugPrint(calib.magBias[0]);
    debugPrint(", ");
    debugPrint(calib.magBias[1]);
    debugPrint(", ");
    debugPrintln(calib.magBias[2]);
    debugPrintln("[INFO]\tMag Scale X/Y/Z: ");
    debugPrint("[INFO]\t");
    debugPrint(calib.magScale[0]);
    debugPrint(", ");
    debugPrint(calib.magScale[1]);
    debugPrint(", ");
    debugPrintln(calib.magScale[2]);
  }
  delay(5000);
}

void blinkLEDS(int speed, int times, bool pattern){
  if(pattern){
    for (int i = 0; i <= times; i++) {
      digitalWrite(Led1Pin, LOW);
      digitalWrite(Led2Pin, LOW);
      delay(speed);
      digitalWrite(Led1Pin, HIGH);
      digitalWrite(Led2Pin, HIGH);
      delay(speed);
    }
    delay(speed*4);
    return;
    }
  for (int i = 0; i <= times; i++) {
      digitalWrite(Led1Pin, LOW);
      digitalWrite(Led2Pin, LOW);
      delay(speed);
      digitalWrite(Led1Pin, HIGH);
      digitalWrite(Led2Pin, HIGH);
      delay(speed);
  }
}

byte getBattPercent(){
  int counts = analogRead(VbatPin);
  int mv = map(counts, 860, 1023, 3660, 4320);
  int localPercent;
  if (mv > 4200){
    localPercent = 100;
  }
  else if(mv > 3870){
    localPercent = map(mv, 3870, 4200, 70, 100);
  }
  else if(mv > 3680)
  {
    localPercent = map(mv, 3680, 3870, 20, 70);
  }
  else{
    localPercent = 0;
  }
  batteryPercent = (batteryPercent * 9 + localPercent) / 10;
  return map(batteryPercent, 0, 100, 0, 255);
}

void debugPrint(String arg) {
#ifdef SERIAL_DEBUG
  Serial.print(arg);
#endif
}
void debugPrint(int arg) {
#ifdef SERIAL_DEBUG
  Serial.print(arg);
#endif
}
void debugPrintln(String arg) {
#ifdef SERIAL_DEBUG
  Serial.println(arg);
#endif
}
void debugPrintln(int arg) {
#ifdef SERIAL_DEBUG
  Serial.println(arg);
#endif
}
