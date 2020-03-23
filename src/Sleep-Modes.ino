/*
* Project Sleep Modes
* Description: Tests the 3rd Generation Device Carrier Boron Sleep Modes with the RTC
* Author: Charles McClelland
* Date: Started 11-17-2019 
* 
* Implements the following Two Tests
* System Stop Mode Sleep wake on Time or Interrupt
* Enable Pin Sleep Functionality
* 
* v0.10 - Initial Release
* v0.11 - Simplified and added watchdog monitoring
* v1.00 - All Sleep modes working 
* v1.01 - Moved to longer numbers for longer sleep periods
* v1.02 - Reduced the numbers of tests to just the ones we will actually use - put in a test for dutation less than 15 minutes.
* v1.03 - Added Particle function for duration - took out verbiage about pass and fail
*/

char currentPointRelease[5] ="1.03";

// Included Libraries
#include "3rdGenDevicePinoutdoc.h"                              // Documents pinout
#include "MCP79410RK.h"

// Prototypes and System Mode calls
FuelGauge batteryMonitor;                                       // Prototype for the fuel gauge (included in Particle core library)
MCP79410 rtc;                                                   // Rickkas MCP79410 libarary

#define testNumberAddr  0x00                                    // Where we store the current test number (1 byte)
#define testStartTimeAddr 0x05                                  // Where we record the test start time (since some tests cause a reboot) (4 bytes)
#define testDurationSecondsAddr 0x0A                            // How long is the test inseconds (4 bytes)

// Pin Constants for Boron
const int blueLED  = D7;                                         // This LED is on the Electron itself
const int userSwitch = D4;                                       // User switch with a pull-up resistor
const int donePin = D5;                                          // Pin the Electron uses to "pet" the watchdog
const int wakeUpPin = D8;                                        // This is the Particle Electron WKP pin

// Program Variables                      
uint8_t testNumber;                                              // What test number are we on
MCP79410Time t;                                                  // Time object - future use
unsigned long testDurationSeconds = 60;                   // Can make shorter or longer - affects all tests
const int numberOfTests = 2;                                     // Number of tests in the suite
volatile bool watchDogFlag = false;                              // Keeps track of the watchdog timer's "pets"
time_t startTime;


// setup() runs once, when the device is first turned on.
void setup() {
  pinMode(userSwitch,INPUT);                                    // Button for user input
  pinMode(wakeUpPin,INPUT);                                       // This pin is active HIGH
  pinMode(blueLED, OUTPUT);                                       // declare the Blue LED Pin as an output
  pinMode(donePin,OUTPUT);                                        // Allows us to pet the watchdog

  Particle.variable("Release",currentPointRelease);

  Particle.function("Duration-Sec",setDuration);

  testNumber = EEPROM.read(testNumberAddr);                       // Load values from EEPROM and bounds check (1st run will have random values)
  if (testNumber < 0 || testNumber > 2) testNumber = 0;
  testDurationSeconds = EEPROM.get(testDurationSecondsAddr,testDurationSeconds);
  
  waitUntil(meterParticlePublish);
  Particle.publish("Test Duration", (String)testDurationSeconds,PRIVATE);
  
  waitUntil(meterParticlePublish);
  if (testNumber) Particle.publish("Status", "Continuing after reset",PRIVATE);
  else Particle.publish("Status", "Beginning Test Run",PRIVATE);
  
  rtc.setup();                                                     // Start the RTC code
  rtc.clearAlarm();                                                // In case it is set from the last call
 
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
    case 0:                                                       // Test for simple System.sleep - Stop Mode
      systemSleepTest();
      EEPROM.write(testNumberAddr,testNumber);
      break;

     case 1:                                                       // System is powered down with the Enable pin and awoken by the RTC
      powerOffSleepWithRTCWakeTest();
      EEPROM.write(testNumberAddr,testNumber);
      break;

    default: {                                                    // Publish the final tally and reset for another go in 30 secs
      char resultStr[64];
      if (testNumber == 2) {
        snprintf(resultStr,sizeof(resultStr),"Tests Complete - 60 sec delay");
        waitUntil(meterParticlePublish);
        Particle.publish("Complete",resultStr,PRIVATE);
        delay(60000);                                             // Space things out a bit - easier to read console and not piss off the carrier
        testNumber = 0;
      }
      } break;
  }
}

bool systemSleepTest() {
  waitUntil(meterParticlePublish);
  Particle.publish("Test 1", "System Stop Mode Sleep",PRIVATE);
  delay(10000);                                                   // Enough time to publish the functions before going back to sleep
  elapsedTimeCorrect(true);                                                            // Start the timer
  if (testDurationSeconds < 15 * 60) {
    System.sleep(userSwitch,CHANGE,testDurationSeconds,SLEEP_NETWORK_STANDBY);        // Particle Stop Mode Sleep less than 15 minutes
  }
  else System.sleep(userSwitch,CHANGE,testDurationSeconds);                           // Particle Stop Mode Sleep more than 15 minutes
  testNumber++;
  if (elapsedTimeCorrect(false)) return 1;                                             // Make sure we slep the right amount of time
  else return 0;
}

bool powerOffSleepWithRTCWakeTest() {                                               // Not working - should use the Enable pin to get deepest sleep
  unsigned long startTime;
  EEPROM.get(testStartTimeAddr,startTime);
  if (testNumber == 1) {                                                            // Lets us know if we are in process or not  
    waitUntil(meterParticlePublish);
    Particle.publish("Test 2", "Power Off with EN pin",PRIVATE);
    testNumber++;
    EEPROM.write(testNumberAddr,testNumber);
    delay(10000);                                                   // Enough time to publish the functions before going back to sleep
    elapsedTimeCorrect(true);
    rtc.setAlarm(testDurationSeconds);                                              // This means we will send the Alarm Pin high with a LOW alarm
    return 1;                                                                       // For the compiler's sake - should never reach here.
  }
  else {
    // Device reboots here
    testNumber = 0;
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
    unsigned long elapsedTime = (rtc.getRTCTime() - startTime);
    snprintf(resultStr,sizeof(resultStr),"Passed elapsed time %lu", elapsedTime);
    waitUntil(meterParticlePublish);
    Particle.publish("Result",resultStr,PRIVATE);
    return 1;
  }
}

int setDuration(String command)
{
  char * pEND;
  char data[256];
  int tempTime = strtol(command,&pEND,10);                       // Looks for the first integer and interprets it
  if ((tempTime < 0) || (tempTime > 3600)) return 0;             // Make sure it falls in a valid range or send a "fail" result
  testDurationSeconds = (unsigned long)tempTime;
  EEPROM.put(testDurationSecondsAddr,testDurationSeconds);    // Write to EEPROM
  snprintf(data, sizeof(data), "Test duration set to %lu", testDurationSeconds);
  waitUntil(meterParticlePublish);
  Particle.publish("Test Duration",data,PRIVATE);
  delay(1000);
  return 1;
}
