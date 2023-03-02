#include <Wire.h>
#include "RTClib.h"
RTC_DS3231 rtc;

// settings
const double maxV = 3.3;  // max voltage output by pins
const double desiredMaxV = 3; // max measureable voltage of analog voltmeter
const int maxPWM = 1023;
const double desiredMaxPWM = maxPWM * desiredMaxV / maxV;  // PWM value that gives the desired max voltage
// time
const int secAmount = 60;
const int minAmount = 60;
const int hAmount = 24;
const double secIncrPWM = desiredMaxPWM / secAmount;
const double minIncrPWM = desiredMaxPWM / minAmount;
const double hIncrPWM = desiredMaxPWM / hAmount;
// temp & RH
const int minTemp = -40;
const int maxTemp = 80;
const int minRH = 0;
const int maxRH = 100;
const int tempAmount = maxTemp - minTemp;
const int RHAmount = maxRH - minRH;
const double tempIncrPWM = desiredMaxPWM / tempAmount;
const double RHIncrPWM = desiredMaxPWM / RHAmount;
// other
const int ADDRESS = 0x40;

// data
int secTime = 0;
int minTime = 0;
int hTime = 0;
int temp;
int rh;

// states
int showTemp = 0; // temp & RH instead of time
int rtcPresent;
int settingTime = 0;  // 0: not setting time, 1: h, 2: min
int prevPush = 0;
int animation = 0;

// events
int eventPush;
int eventRelease;
int eventHold;

// loop coordination
unsigned long prevMillis = 0;
unsigned long releaseMillis = 0;
unsigned long loopMillis = 0;
unsigned long pushStartMillis = 0;
unsigned long animationStartMillis = 0;
int pressDelayMillis = 100;
int pushHoldMillis = 1000;

// values to reflect voltmeter inaccuracy (PWM value is multiplied by value in corresponding quarter)
float secMod[4] = {1.018, 1.03, 1.035, 1.018};  // ideally all 1
float minMod[4] = {1.01, 1.034, 1.045, 1.045};  // ideally all 1
float hMod[4] = {1.025, 1.035, 1.036, 1.034};  // ideally all 1

// pins
int buttonPin = D3;
int secPin = D5;
int minPin = D6;
int hPin = D7;

void sensor_init(const int addr) {
  Wire.begin();  delay(100);
  Wire.beginTransmission(addr);
  Wire.endTransmission();
}

double read_temperature(const int addr) {
  double temperature;
  int low_byte, high_byte, raw_data;
  /* Send command of initiating temperature measurement */
  Wire.beginTransmission(addr);
  Wire.write(0xE3);
  Wire.endTransmission();
  /* Read data of temperature */
  Wire.requestFrom(addr, 2);
  if (Wire.available() <= 2) {
    high_byte = Wire.read();
    low_byte = Wire.read();
    high_byte = high_byte << 8;
    raw_data = high_byte + low_byte;
  }
  temperature = (175.72 * raw_data) / 65536;
  temperature = temperature - 46.85;
  return temperature;
}

double read_humidity(const int addr) {
  double humidity, raw_data_1, raw_data_2;
  int low_byte, high_byte, container;
  /* Send command of initiating relative humidity measurement */
  Wire.beginTransmission(addr);
  Wire.write(0xE5);
  Wire.endTransmission();
  /* Read data of relative humidity */
  Wire.requestFrom(addr, 2);
  if(Wire.available() <= 2) {
    high_byte = Wire.read();
    container = high_byte / 100;
    high_byte = high_byte % 100;
    low_byte = Wire.read();
    raw_data_1 = container * 25600;
    raw_data_2 = high_byte * 256 + low_byte;
  }
  raw_data_1 = (125 * raw_data_1) / 65536;
  raw_data_2 = (125 * raw_data_2) / 65536;
  humidity = raw_data_1 + raw_data_2;
  humidity = humidity - 6;
  return humidity;
}

void updateTime() {
  if (++secTime >= secAmount) {
    secTime = 0;
    if (++minTime >= minAmount) {
      minTime = 0;
      if (++hTime >= hAmount) {
        hTime = 0;
      }
      if (rtcPresent) {
        updateTimeRTC;
      }
    }
  }
}

void updateTimeRTC() {
  DateTime now = rtc.now();
  secTime = now.second();
  minTime = now.minute();
  hTime = now.hour();
}

void writeTime(int which) {
  /* 0: write all
   * 1: write seconds
   * 2: write minutes
   * 3: write hours
   */
  Serial.println(secTime);
  Serial.println(minTime);
  Serial.println(hTime);
  Serial.println();
  if (which == 0 | which == 3)
    analogWrite(secPin, (int)(secTime * secIncrPWM * secMod[(int)(secTime/(secAmount/4+0.01))]));  // write seconds
  if (which == 0 | which == 2)
    analogWrite(minPin, (int)(minTime * minIncrPWM * minMod[(int)(minTime/(minAmount/4+0.01))]));  // write minutes
  if (which == 0 | which == 1)
    analogWrite(hPin, (int)(hTime * hIncrPWM * hMod[(int)(hTime/(hAmount/4+0.01))]));  // write hours
}

void writeNull() {
  analogWrite(hPin, 0);
  analogWrite(minPin, 0);
  analogWrite(secPin, 0);
}

void writeTemp() {
  temp = (int)read_temperature(ADDRESS);
  rh = (int)read_humidity(ADDRESS);  
  if (temp < minTemp) {
    temp = minTemp; }
  else if (temp > maxTemp) {
    temp = maxTemp; }
  if (rh < minRH) {
    rh = minRH; }
  else if (rh > maxTemp) {
    rh = maxRH; }
  Serial.println(temp);
  Serial.println(rh);
  Serial.println();
  analogWrite(secPin, (int)((rh - minRH) * RHIncrPWM * secMod[(int)((rh - minRH)/(RHAmount/4+0.1))])); // write humidity
  analogWrite(minPin, (int)((temp - minTemp) * tempIncrPWM * minMod[(int)((temp - minTemp)/(tempAmount/4+0.1))])); // write temperature
  analogWrite(hPin, 0);
}

void clearEvents() {
  eventPush = 0;
  eventRelease = 0;
  eventHold = 0;
}

void saveTimeRTC() {
  rtc.adjust(DateTime(1, 1, 1, hTime, minTime, secTime));
}

void readPushbutton() {
  /* set eventPush to 1 if pressed
   * set eventRelease to 1 if released
   * switch showTemp on press
   */
  if (!prevPush && !digitalRead(buttonPin) && millis() - releaseMillis >= pressDelayMillis) {
    pushStartMillis = millis();
    loopMillis = 0;
    prevPush = 1;
    eventPush = 1;
    if (!settingTime) {
      showTemp = !showTemp;
      if (!showTemp && rtcPresent) {
        updateTimeRTC();
      }
    }
  }
  else if (prevPush && digitalRead(buttonPin)) {
    prevPush = 0;
    eventRelease = 1;
    releaseMillis = millis();
    if (millis() - pushStartMillis >= pushHoldMillis) {
      eventHold = 1;
    }
  }
}

void settingTimeStart() {
  if (++settingTime >= 3)
    settingTime = 0;
  if (settingTime) {
    animation = 1;
    animationStartMillis = millis();
    writeNull();
  }
}

void animate() {
  if (millis() - animationStartMillis >= 1000) {
    animation = 0;
    writeNull();
    writeTime(settingTime);
  }
  else if (millis() - animationStartMillis >= 500) {
    switch (settingTime) {
      case 1:
        analogWrite(hPin, desiredMaxPWM * hMod[3]);
        break;
      case 2:
        analogWrite(minPin, desiredMaxPWM * minMod[3]);
        break;
      case 3:
        analogWrite(secPin, desiredMaxPWM * secMod[3]);
        break;
    }
  }
}

void setTime() {
  switch (settingTime) {
    case 1:
      if (++hTime >= hAmount)
        hTime = 0;
      break;
    case 2:
      if (++minTime >= minAmount)
        minTime = 0;
      break;
    case 3:
      if (++secTime >= secAmount)
        secTime = 0;
      break;
  }
  writeTime(settingTime);
}

void setup() {
  analogWriteRange(maxPWM);
  Serial.begin(9600);
  prevMillis = millis();
  while(!Serial && millis() - prevMillis < 7000) delay(10); // wait for serial connection for up to 7 seconds
  sensor_init(ADDRESS); // initialize the temperature and humidity sensor
  rtcPresent = rtc.begin();
  if (rtcPresent) {
    if (rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // if RTC lost it's time, reset it
    }
    updateTimeRTC();
  }
  // pins
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(secPin, OUTPUT);  // also RH
  pinMode(minPin, OUTPUT);  // also temp
  pinMode(hPin, OUTPUT);
}

void loop() {
  clearEvents();
  readPushbutton();
  if (!settingTime && millis() - loopMillis >= 1000) {
    loopMillis = millis();
    if (!showTemp) {
      updateTime();
      writeTime(0);
    } else {
      writeTemp();
    }
  }
  if (eventHold) {
    settingTimeStart();
    if (!settingTime && rtcPresent) {
      secTime = 0;
      saveTimeRTC();
      showTemp = 0;
    }
  }
  else if (settingTime && animation)
    animate();
  else if (eventRelease && settingTime)
    setTime();
}
