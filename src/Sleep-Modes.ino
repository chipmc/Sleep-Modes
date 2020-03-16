/*
* Project Sleep Modes
* Description: Tests the 3rd Generation Device Carrier Boron Sleep Modes with the RTC
* Author: Charles McClelland
* Date: Started 11-17-2019 
* 
* Implements the following Tests
* 1 - System Stop Mode Sleep wake on Time
* 2 - RTC Alarm Test
* 3 - System Stop Mode Sleep with Wake on Interrupt
* 4 - System Deep Sleep with RTC Wake
* 5 - Enable Pin Sleep Functionality
* 
* v0.10 - Initial Release
* v0.11 - Simplified and added watchdog monitoring
* v1.00 - All Sleep modes working 
*/

// Included Libraries
#include "3rdGenDevicePinoutdoc.h"                              // Documents pinout
#include "MCP79410RK.h"

// Prototypes and System Mode calls
FuelGauge batteryMonitor;                                       // Prototype for the fuel gauge (included in Particle core library)
MCP79410 rtc;                                                   // Rickkas MCP79410 libarary

#define testNumberAddr  0x00                                    // Where we store the current test number (1 byte)
#define testStartTimeAddr 0x01                                  // Where we record the test start time (since some tests cause a reboot) (4 bytes)
#define numberOfTestsPassedAddr 0x05                            // Where we store the current count of successful tests (1 byte)

// Pin Constants for Boron
const int blueLED  = D7;                                         // This LED is on the Electron itself
const int userSwitch = D4;                                       // User switch with a pull-up resistor
const int donePin = D5;                                          // Pin the Electron uses to "pet" the watchdog
const int wakeUpPin = D8;                                        // This is the Particle Electron WKP pin
const int DeepSleepPin = D6;                                     // Power Cycles the Particle Device and the Carrier Board only RTC Alarm can wake

// Program Variables
volatile bool watchdogInterrupt = false;                         // variable used to see if the watchdogInterrupt had fired                          
uint8_t testNumber;                                              // What test number are we on
MCP79410Time t;                                                  // Time object - future use
const int testDurationSeconds = 20;                              // Can make shorter or longer - affects all tests
const int numberOfTests = 5;                                     // Number of tests in the suite
int numberOfTestsPassed = 0;                                     // Our scorecard
volatile bool watchDogFlag = false;                              // Keeps track of the watchdog timer's "pets"

// setup() runs once, when the device is first turned on.
void setup() {
  //pinMode(userSwitch,INPUT);                                    // Button for user input
  pinMode(wakeUpPin,INPUT);                                       // This pin is active HIGH
  pinMode(blueLED, OUTPUT);                                       // declare the Blue LED Pin as an output
  pinMode(donePin,OUTPUT);                                        // Allows us to pet the watchdog
  pinMode(DeepSleepPin ,OUTPUT);                                  // For a hard reset active HIGH

  testNumber = EEPROM.read(testNumberAddr);                       // Load values from EEPROM and bounds check (1st run will have random values)
  if (testNumber < 0 || testNumber > 5) testNumber = 0;
  numberOfTestsPassed = EEPROM.read(numberOfTestsPassedAddr);
  if (numberOfTestsPassed < 0 || numberOfTestsPassed > numberOfTests) numberOfTestsPassed = 0;

  waitUntil(meterParticlePublish);
  if (testNumber) Particle.publish("Status", "Continuing after reset",PRIVATE);
  else Particle.publish("Status", "Beginning Test Run",PRIVATE);
  
  rtc.setup();                                                     // Start the RTC code
 
  attachInterrupt(wakeUpPin, watchdogISR, RISING);                 // Need to pet the watchdog when needed - may not be relevant to your application
}


void loop() {
  rtc.loop();                                                     // Need to run this in the main loop
  if (watchDogFlag) {
    waitUntil(meterParticlePublish);
    Particle.publish("Watchdog","Interrupt",PRIVATE);
    watchDogFlag = false;
  }

  switch (testNumber) {
    case 0:
      if (digitalRead(userSwitch) == LOW) {
        testNumber++;
      }
      break;

    case 1:                                                       // Test for simple System.sleep - Stop Mode
      if (systemSleepTest()) numberOfTestsPassed++;
      testNumber++;
      EEPROM.write(testNumberAddr,testNumber);
      EEPROM.write(numberOfTestsPassedAddr, numberOfTestsPassed);
      break;

    case 2:                                                       // Test for simple RTC alarm - no sleep
      if (rtcAlarmTest()) numberOfTestsPassed++;
      testNumber++;
      EEPROM.write(testNumberAddr,testNumber);
      EEPROM.write(numberOfTestsPassedAddr, numberOfTestsPassed);
      break;

    case 3:                                                       // Test to wake te device on a hardware interrupt
      if (systemSleepWakeOnInterruptTest()) numberOfTestsPassed++;
      testNumber++;
      EEPROM.write(testNumberAddr,testNumber);
      EEPROM.write(numberOfTestsPassedAddr, numberOfTestsPassed);
      break;

    case 4:                                                       // Deep Sleep test where the device is awoken by the RTC on D8
      if (systemDeepSleepRTCWakeTest()) numberOfTestsPassed++;
      testNumber++;
      EEPROM.write(testNumberAddr,testNumber);
      EEPROM.write(numberOfTestsPassedAddr, numberOfTestsPassed);
      break;

    case 5:                                                       // System is powered down with the Enable pin and awoken by the RTC
      if (powerOffSleepWithRTCWakeTest()) numberOfTestsPassed++;
      testNumber++;
      EEPROM.write(testNumberAddr,testNumber);
      EEPROM.write(numberOfTestsPassedAddr, numberOfTestsPassed);
      break;

    default: {                                                    // Publish the final tally and reset for another go in 30 secs
      char resultStr[64];
      if (testNumber == 6) {
        snprintf(resultStr,sizeof(resultStr),"Sleep Tests Complete - Passed %i out of %i", numberOfTestsPassed, numberOfTests);
        waitUntil(meterParticlePublish);
        Particle.publish("Final Result",resultStr,PRIVATE);
        delay(30000);                                             // Space things out a bit - easier to read console and not piss off the carrier
        testNumber = 0;
      }
      } break;
  }
}

bool systemSleepTest() {
  waitUntil(meterParticlePublish);
  Particle.publish("Test", "System Stop Mode Sleep",PRIVATE);
  elapsedTimeCorrect(true);                                                            // Start the timer
  System.sleep({},{},testDurationSeconds,SLEEP_NETWORK_STANDBY);                       // Particle Stop Mode Sleep 10 secs
  if (elapsedTimeCorrect(false)) return 1;                                             // Make sure we slep the right amount of time
  else return 0;
}

bool rtcAlarmTest() {                                                                 // RTC Alarm and Watchdog share access to Wake Pin via an OR gate
  waitUntil(meterParticlePublish);
  Particle.publish("Test", "RTC Alarm", PRIVATE);
  elapsedTimeCorrect(true);                                                           // Start the timer
  rtc.setAlarm(10);
  waitFor(alarmSounded,12000);                                                   
  if (rtc.getInterrupt() && digitalRead(wakeUpPin)) {                                 // We need both a HIGH on Wake and the RTC Interrupt to pass this test
    if (elapsedTimeCorrect(false)) return true;                                       // Passed
    else return false; 
  }
  waitUntil(meterParticlePublish);
  Particle.publish("Result","Failed - No Interrupt",PRIVATE);
  return false;
}

bool alarmSounded() {                                                                 // Simple function to support waitFor on the alarm interrupt
  if (digitalRead(wakeUpPin))return true;
  else return false;
}

bool systemSleepWakeOnInterruptTest() {                                               // Stop-mode sleep with an interrupt wake                                  
  unsigned long startTime;
  waitUntil(meterParticlePublish);
  Particle.publish("Test", "System Stop Mode with Wake on Interrupt", PRIVATE);
  waitUntil(meterParticlePublish);
  Particle.publish("Test", "Press User Button to Wake", PRIVATE);
  startTime = rtc.getRTCTime();
  System.sleep(userSwitch,CHANGE,testDurationSeconds,SLEEP_NETWORK_STANDBY);                  // Note, if you ware using a debounced switch FALLING will not work!
  if (rtc.getRTCTime() - startTime <= testDurationSeconds) {
    waitUntil(meterParticlePublish);
    Particle.publish("Results","Passed",PRIVATE);
    return true;
  }
  else  {
    waitUntil(meterParticlePublish);
    Particle.publish("Results","Failed",PRIVATE);
    return false;
  }
}

bool systemDeepSleepRTCWakeTest() {                                                 // Here the Boron is put into Deep sleep and the RTC wakes it up
  unsigned long startTime;
  EEPROM.get(testStartTimeAddr,startTime);
  if (startTime == 0L) {                                                            // Lets us know if we are in process or not  
    waitUntil(meterParticlePublish);
    Particle.publish("Test", "System Deep Mode Sleep with RTC Wake",PRIVATE);
    rtc.setAlarm(testDurationSeconds);                                              // Alarm on D8 to wake
    elapsedTimeCorrect(true);
    System.sleep(SLEEP_MODE_DEEP);                                                  // System Deep Sleep Mode - wakes only by D8
    return 1;                                                                       // For the compiler's sake - should never reach here.
  }
  else {
    // Device reboots here
    if (elapsedTimeCorrect(false)) return 1;
    else return 0;
  }
}

bool powerOffSleepWithRTCWakeTest() {                                               // Not working - should use the Enable pin to get deepest sleep
  unsigned long startTime;
  EEPROM.get(testStartTimeAddr,startTime);
  if (startTime == 0L) {                                                            // Lets us know if we are in process or not  
    waitUntil(meterParticlePublish);
    Particle.publish("Test", "Power Off with EN pin using RTC",PRIVATE);
    delay(5000);
    rtc.setAlarm(testDurationSeconds,false);                                              // Alarm on EN in 10 seconds
    elapsedTimeCorrect(true);
    digitalWrite(DeepSleepPin,HIGH);                                                // This command should cut power to the device using the Enable pin
    return 1;                                                                       // For the compiler's sake - should never reach here.
  }
  else {
    // Device reboots here
    if (elapsedTimeCorrect(false)) return 1;
    else return 0;
  }
}


// Utility Functions Area

void watchdogISR()                                                                  // Watchdog functionality not a big focus here
{
  digitalWrite(donePin, HIGH);                                                      // Pet the watchdog
  digitalWrite(donePin, LOW);
  watchDogFlag = true;
}

bool meterParticlePublish(void) {                                                  // Enforces Particle's limit on 1 publish a second
  static unsigned long lastPublish=0;                                              // Initialize and store value here
  if(millis() - lastPublish >= 1000) {                                             // Particle rate limits at 1 publish per second
    lastPublish = millis();
    return 1;
  }
  else return 0;
}

bool elapsedTimeCorrect(bool start) {                                             // Uses the RTC to calculate the elapsed time since we are testing Deep sleep in some cases
  time_t startTime;
  char resultStr[32];
  int adjustment;
  (testNumber == 4) ? (adjustment = 2 + millis()/1000) : adjustment = 2;
  if (start) {
    startTime = rtc.getRTCTime();
    EEPROM.put(testStartTimeAddr,startTime);
    return 0;
  }
  else {
    EEPROM.get(testStartTimeAddr,startTime);
    EEPROM.put(testStartTimeAddr,0L);
    int elapsedTime = (rtc.getRTCTime() - startTime);
    if (testDurationSeconds >= elapsedTime - adjustment && testDurationSeconds <= elapsedTime) {
      snprintf(resultStr,sizeof(resultStr),"Passed elapsed time %i", elapsedTime);
      waitUntil(meterParticlePublish);
      Particle.publish("Result",resultStr,PRIVATE);
      return 1;
    }
    else {
      snprintf(resultStr,sizeof(resultStr),"Failed elapsed time %i", elapsedTime);
      waitUntil(meterParticlePublish);
      Particle.publish("Result",resultStr,PRIVATE);
      return 0;
    } 
  }

}
