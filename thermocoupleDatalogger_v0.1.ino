// 10-Channel Thermocouple Datalogger
// Daniel Müller
// Tamera, 30.07.2019

// This code runs on an Arduino Mega and incorporates ten breakout boards from Adafruit 
// (https://www.adafruit.com/product/1727) for the MAX31850K chip. It also features an 
// LCD screen with an LCD backpack from Adafruit (https://www.adafruit.com/product/292),
// an Adafruit DS1307 RTC Breakout Board (https://www.adafruit.com/product/3296), an SD
// module from DFRobot.com, an Adafruit MCP9808 Breakout Board for reference temperature
// (https://www.adafruit.com/product/1782) and a push button.

// The datalog interval can be set between 2 seconds up to an hour and datalogging can
// be switched on and off.

#include <Wire.h>
#include <Adafruit_LiquidCrystal.h>
#include <OneButton.h>
#include "Adafruit_MCP9808.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "RTClib.h"
#include <SPI.h>
#include <SD.h>

Adafruit_LiquidCrystal lcd(0);  // Connect LCD via i2c

// Setup the thermocouples
#define ONE_WIRE_BUS 2  // Data wire is plugged into port 2 on the Arduino
OneWire oneWire(ONE_WIRE_BUS);  // Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
DallasTemperature sensors(&oneWire);  // Pass our oneWire reference to Dallas Temperature. 
DeviceAddress channel1, channel2, channel3, channel4, channel5, channel6, channel7, channel8, channel9, channel10;

OneButton button(3, true);  // Setup a OneButton on Pin 3

RTC_DS1307 rtc;

Adafruit_MCP9808 tempsensor = Adafruit_MCP9808();  // Create the MCP9808 temperature sensor object

const int SPIchipSelectPin = 53;
const int  resolution = 9;  // Thermocouple Resolution in bits
const long loggingIntervals[10] = {2000, 5000, 10000, 30000, 60000, 300000, 600000, 1800000, 3600000};
const int lcdUpdateInterval = 1000;  // Update interval for the LCD screen

bool sdCardPresent = 1;
bool setupActive = 1;
int loggingIntervalIndex = 0;
int loggingIntervalIndexOld = 0;
long loggingInterval = 0;
bool loggingOn = 0;
long loggingTimer = 0;
long loggingStartTime = 0;
long lcdUpdateTimer = 0;
int activeChannel = 0;
float activeChannelValue;
float tempC;

void setup() {
  Serial.begin(9600);
  while (!Serial) {}
  lcd.begin(16, 2);  // set up the LCD's number of rows and columns
  if (!rtc.begin()) {  // start up the RTC or print error on LCD
    lcd.setCursor(0, 0);
    lcd.print("RTC Error");
    while (1);  // stop here
  }

  // see if the SD card is present and can be initialized
  Serial.print("Initializing SD card...");
    
  if (!SD.begin(SPIchipSelectPin)) {
    Serial.println(" SD card error!");
    sdCardPresent = 0;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SD card error!");
    lcd.setCursor(0, 1);
    lcd.print("Wait to proceed.");
    delay(5000);
  }
  else Serial.print(" Done!");

  // This will set the rtc to the computer time of sketch upload
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));  // This line sets the RTC to the sketch-uploading computer's time

  if (!tempsensor.begin(0x18)) {
    Serial.println("T_ref Error");
    while (1);
  }
  tempsensor.setResolution(1);  // Set the reference temperature sensor to 0.25° resolution
  
  thermocoupleSetup();  // do the thermocouple setup, find addresses & indices etc.
  
  button.attachClick(click);  // link the doubleclick function to be called on a click event
  button.attachPress(press);  // link the press function to be called on a press event
  button.setPressTicks(2000);  // sets the duration for press event to 2 s

  if (sdCardPresent) setLoggingInterval();
  writeHeader();
  
  setupActive = 0;  // mark exiting setup() for click() & press() functions
}
  
void loop() {
  button.tick();  // To check button for activity, we need to tick here
  checkTimer();  // This function checks whether the measurement interval has passed and then calls logMeasurements()
}

void checkTimer() {
  // logMeasurements if interval exceeded and logging is switched on
  if ((millis() - loggingTimer) >= loggingInterval && loggingOn) {
    loggingTimer = millis();
    logMeasurements();
  }
  // update the lcd if lcdUpdateInterval is exceeded (or it is still zero in the first call)
  else if ((millis() - lcdUpdateTimer) >= lcdUpdateInterval || lcdUpdateTimer == 0) {
    lcdUpdateTimer = millis();
    updateActiveChannelValue();
  }
}

void logMeasurements() {
  // This function gets called each time the current time
  // exceeds multiples of the logging interval.
  // It takes readings from all  sensors (RTC, T_ref, thermocouples) & writes them to SD card & serial output

  button.tick();  // tick button for better responsiveness

  // Open the file on the SD card to write in it
  File dataFile = SD.open("datalog.txt", FILE_WRITE);
  if (dataFile) { // Checks if the file is open
    
    // take an RTC reading and print it
    DateTime now = rtc.now();
    Serial.print(now.year(), DEC);
    dataFile.print(now.year(), DEC);
    Serial.print('/');
    dataFile.print('/');
    Serial.print(now.month(), DEC);
    dataFile.print(now.month(), DEC);
    Serial.print('/');
    dataFile.print('/');
    Serial.print(now.day(), DEC);
    dataFile.print(now.day(), DEC);
    Serial.print("\t");
    dataFile.print("\t");
    Serial.print(now.hour(), DEC);
    dataFile.print(now.hour(), DEC);
    Serial.print(':');
    dataFile.print(':');
    Serial.print(now.minute(), DEC);
    dataFile.print(now.minute(), DEC);
    Serial.print(':');
    dataFile.print(':');
    Serial.print(now.second(), DEC);
    dataFile.print(now.second(), DEC);
    Serial.print("\t");
    dataFile.print("\t");
  
    // take a reference temperature reading and print it
    tempsensor.wake();
    Serial.print(tempsensor.readTempC());
    dataFile.print(tempsensor.readTempC());
    tempsensor.shutdown_wake(1);
    Serial.print("\t");
    dataFile.print("\t");
  
    // take thermocouple readings of all channels and print them
    sensors.requestTemperatures();
    for (int i = 0; i <= 9; i++) {
      button.tick();  // tick button for better responsiveness
      // Instead of the following, here the data logging will be happening...
      tempC = sensors.getTempCByIndex(i);
      if (tempC < 0) {
        Serial.print("NaN");
        dataFile.print("NaN");
      }
      else {
        Serial.print(tempC);
        dataFile.print(tempC);
      }
      Serial.print("\t");
      dataFile.print("\t");
    }
    Serial.println();
    dataFile.println();
    dataFile.close(); // Closes the file again
  }
  else {  // In case the file wasn't detected to be open, print an error message
    Serial.println("error opening datalog.txt");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("error opening");
    lcd.setCursor(0, 1);
    lcd.print("datalog.txt");
  }
}

void updateActiveChannelValue() { 
  // This function gets measurements for the active channel only
  if (activeChannel == 0) {
    tempsensor.wake();
    activeChannelValue = tempsensor.readTempC();
    tempsensor.shutdown_wake(1);
  }
  else {
    sensors.requestTemperatures();
    tempC = sensors.getTempCByIndex(activeChannel-1);
    if (tempC < 0) activeChannelValue = NAN;
    else activeChannelValue = tempC;
  }
  updateLCD();
}

void updateLCD() {  // Gets called when the LCD display needs to be updated
  lcd.clear();
  lcd.setCursor(0, 0);
  if (loggingOn) {
    lcd.print("log... ");
    // The following code will show the elapsed logging time on the LCD screen.
    // First condition for elapsed time smaller than one minute
    if (millis()/1000-loggingStartTime < 60) {
      lcd.print(millis()/1000-loggingStartTime);
      lcd.print(" s");
    }
    // Next condition for elapsed time smaller than one hour
    else if (millis()/1000-loggingStartTime < 3600) {
      lcd.print((millis()/1000-loggingStartTime)/60);
      lcd.print("m ");
      lcd.print(millis()/1000-loggingStartTime);
      lcd.print("s");
    }
    // Next condition for elapsed time smaller than a day
    else if (millis()/1000-loggingStartTime < 86400) {
      lcd.print((millis()/1000-loggingStartTime)/3600);
      lcd.print("h ");
      lcd.print((millis()/1000-loggingStartTime)/60);
      lcd.print("m");
    }
    // Last condition for elapsed time bigger than one day
    else {
      lcd.print((millis()/1000-loggingStartTime)/86400);
      lcd.print("d ");
      lcd.print((millis()/1000-loggingStartTime)/3600);
      lcd.print("h");
    }
  }
  else if (sdCardPresent == 0) lcd.print("no SD card.");
  else lcd.print("logging off");
  lcd.setCursor(0, 1);
  lcd.print("T_");
  if (activeChannel == 0) lcd.print("ref");
  else lcd.print(activeChannel);
  lcd.print(": ");
  lcd.print(activeChannelValue);
}

void click() {  // Gets called, when a button click is detected
  // In setup() mode, a click toggles loggingIntervals
  if (setupActive) {
    if (loggingIntervalIndex < 8) {
      loggingIntervalIndex++;
    }
    else {
      loggingIntervalIndex = 0;
    }
  }
  // In loop() mode, a click toggles channels & then updates the LCD screen
  else {
    if (activeChannel < 10) {
      activeChannel++;
    }
    else {
      activeChannel = 0;  
    }
    updateLCD();
  }
}

void press() {  // This function gets called, when a button press is detected
  // In setup() mode a press sets the logging interval and exits setLoggingInterval()
  if (setupActive) {
    loggingInterval = loggingIntervals[loggingIntervalIndex];
  }
  // In loop() mode it switches logging on/off if an SD card is present
  // and then updates the LCD screen
  if (!setupActive && sdCardPresent == 1) {
    if (loggingOn) {
      loggingOn = 0;
    }
    else {
      loggingOn = 1;
      loggingStartTime = millis()/1000;
    }
    updateLCD();
  }
}

void setLoggingInterval() {  // This function gets called once in setup to set the logging interval
  lcd.clear();
  while (!loggingInterval) {
    button.tick();  // To have the button functioning in her, we need to tick it
    lcd.setCursor(0, 0);
    lcd.print("log interval:");
    // Differentiate between seconds and minutes
    if (loggingIntervals[loggingIntervalIndex]/1000 <= 60) {
      lcd.setCursor(0, 1);
      lcd.print(loggingIntervals[loggingIntervalIndex]/1000);
      lcd.print(" s");
    }
    else {
      lcd.setCursor(0, 1);
      lcd.print(loggingIntervals[loggingIntervalIndex]/(60000));
      lcd.print(" min.");
    }
    // Only update, if the logging interval has changed
    if (loggingIntervalIndexOld != loggingIntervalIndex) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("log interval:");
      lcd.setCursor(0, 1);
      lcd.print(loggingIntervals[loggingIntervalIndex]/1000);
    }
    loggingIntervalIndexOld = loggingIntervalIndex;
  }
}

void thermocoupleSetup() {
  sensors.begin();  // Start up the library
  // find addresses for the thermocouples
  if (!sensors.getAddress(channel5, 0)) Serial.println("Unable to find address for channel 5");
  if (!sensors.getAddress(channel2, 1)) Serial.println("Unable to find address for channel 2");
  if (!sensors.getAddress(channel4, 2)) Serial.println("Unable to find address for channel 4");
  if (!sensors.getAddress(channel7, 3)) Serial.println("Unable to find address for channel 7");
  if (!sensors.getAddress(channel1, 4)) Serial.println("Unable to find address for channel 1");
  if (!sensors.getAddress(channel8, 5)) Serial.println("Unable to find address for channel 8");
  if (!sensors.getAddress(channel6, 6)) Serial.println("Unable to find address for channel 6");
  if (!sensors.getAddress(channel9, 7)) Serial.println("Unable to find address for channel 9");
  if (!sensors.getAddress(channel3, 8)) Serial.println("Unable to find address for channel 3");
  if (!sensors.getAddress(channel10, 9)) Serial.println("Unable to find address for channel 10");
  sensors.setResolution(channel1, resolution);
  sensors.setResolution(channel2, resolution);
  sensors.setResolution(channel3, resolution);
  sensors.setResolution(channel4, resolution);
  sensors.setResolution(channel5, resolution);
  sensors.setResolution(channel6, resolution);
  sensors.setResolution(channel7, resolution);
  sensors.setResolution(channel8, resolution);
  sensors.setResolution(channel9, resolution);
  sensors.setResolution(channel10, resolution);
} 

void writeHeader() {

  // Print header to serial output
  Serial.println("10-channel thermocouple datalogger v0.1");
  Serial.println();
  Serial.print("date");
  Serial.print("\t");
  Serial.print("time");
  Serial.print("\t");
  Serial.print("t_ref");
  Serial.print("\t");
  Serial.print("t_1");
  Serial.print("\t");
  Serial.print("t_2");
  Serial.print("\t");
  Serial.print("t_3");
  Serial.print("\t");
  Serial.print("t_4");
  Serial.print("\t");
  Serial.print("t_5");
  Serial.print("\t");
  Serial.print("t_6");
  Serial.print("\t");
  Serial.print("t_7");
  Serial.print("\t");
  Serial.print("t_8");
  Serial.print("\t");
  Serial.print("t_9");
  Serial.print("\t");
  Serial.print("t_10");
  Serial.print("\n");

  // If SD card is present, open the file on the SD card to attach the header to it
  if (sdCardPresent) {
    File dataFile = SD.open("datalog.txt", FILE_WRITE);
    if (dataFile) { // Checks if the file is open
      dataFile.println("10-channel thermocouple datalogger v0.1");
      dataFile.println();
      dataFile.print("date");
      dataFile.print("\t");
      dataFile.print("time");
      dataFile.print("\t");
      dataFile.print("t_ref");
      dataFile.print("\t");
      dataFile.print("t_1");
      dataFile.print("\t");
      dataFile.print("t_2");
      dataFile.print("\t");
      dataFile.print("t_3");
      dataFile.print("\t");
      dataFile.print("t_4");
      dataFile.print("\t");
      dataFile.print("t_5");
      dataFile.print("\t");
      dataFile.print("t_6");
      dataFile.print("\t");
      dataFile.print("t_7");
      dataFile.print("\t");
      dataFile.print("t_8");
      dataFile.print("\t");
      dataFile.print("t_9");
      dataFile.print("\t");
      dataFile.print("t_10");
      dataFile.print("\n");
      dataFile.close(); // Closes the file again
    }
    else {  // In case the file wasn't detected to be open, print an error message
      Serial.println("error opening datalog.txt");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("error opening");
      lcd.setCursor(0, 1);
      lcd.print("datalog.txt");
    }
  }
}
