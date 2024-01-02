/*=========================================================================================
 * Example of an Arduino sketch to convert 433 MHz RF signal into bits.
 * Program to receive packets from a wireless sensor with Nexus_TH modulation protocol. 
 * Receiver on datapin D2 of NodeMcu. Modulation protocol: OOK_PULSE_PPM. 
 * Tested on NodeMcu ESP8266 board with cheap XY-MK-5V super regenerative receiver.
 * Written by : Arthur Kalverboer, 30-12-2023
 * https://github.com/akalverboer/
 *=========================================================================================
Protocol Nexus_TH
The sensor sends a packet of 36 bits 12 times.
The packets are PPM modulated (distance coding) with a pulse of ~500 us.
A bit is defined by a pulse (HIGH) followed by a gap (LOW).
- a 0 bit is followed by a short gap of ~1000 us.
- a 1 bit is followed by a long gap of ~2000 us.
- a sync bit is followed by a gap of ~4000 us.

Packet of 36 bits:
 7  6  5  4  3  2  1  0  0   1 0 11 10  9  8  7  6  5  4  3  2  1  0         7 6 5 4 3 2 1 0
|ID|ID|ID|ID|ID|ID|ID|ID|B|0|C|C| T| T| T| T| T| T| T| T| T| T| T| T|1|1|1|1|H|H|H|H|H|H|H|H|
 Device ID               |   |    Temperature                        Fixed   Humidity
                         Battery status: OK=1, Low=0
                             Channel: 00, 01 or 10

Example packet: 00100011 1000 000001001001 1111 00010101
Device ID: 00100011 = 35
Battery status: 1 = OK
Channel: 00
Temperature: 000001001001 = 7.3  (73/10) Celsius
Humidity: 00010101 = 21  Percentage

NB. As far as I understand.
The width of the last gap is undefined because there is not a desync pulse.
So I can not know if bit 36 (the last) is HIGH or LOW. Therefore I choose one: HIGH.
This gives at most a minor error in the humidity value.
===========================================================================================
*/

class NearestNumber {
  // ===============================================================================
  private:
    const uint8_t lenArr = 6;
    const long numLow = 150; 
    const long numHigh = 5500;
    long numArr[6] = {numLow, 500, 1000, 2000, 4000, numHigh};
    long nearestNum = 0;
    uint8_t nearestIdx = 0;
    void findNearest(long val) {
      long nNum = 0;
      uint8_t nIdx = 0;
      for (uint8_t i = 0; i < lenArr; i++) {
        if (abs(val-numArr[i]) < abs(val-nNum)) {
          nNum = numArr[i];
          nIdx = i;
        }
      }
      this->nearestNum = nNum;
      this->nearestIdx = nIdx;
      return;
    }
  public:
    uint8_t findNearestIdx(long val) { findNearest(val); return this->nearestIdx; }
    long findNearestNum(long val) { findNearest(val); return this->nearestNum; }
    long getNumLow() { return this->numLow; }
    long getNumHigh() { return this->numHigh; }
};  // NearestNumber ================================================================

struct INT_VARS {
  // Local variables for interrupt handler
  unsigned long time;
  unsigned long lastTime;
  long duration;
  long durCat;
  bool isRising;
  unsigned int bitCount;
  bool syncPassed;
};  // semicolon!!

#define ledPin      LED_BUILTIN  // On board LED 

const unsigned int BUFFER_SIZE = 50; // Large enough to fit packet of 36 bits
const byte BIN1 = 1;
const byte BIN0 = 0;
const int  DATAPIN = D2;  // NodeMCU pin D2 (GPIO3): receiver signal

// Globals
volatile byte packetBits[BUFFER_SIZE];  // Store bits of packets
volatile bool packetReceived = false;   // If true, packet ready for processing
volatile INT_VARS ix;       // Local vars of interrupt handler
NearestNumber nn;  

void initHandler() {
  // Called at setup.  Locals of handler
  ix.time = 0;
  ix.lastTime = 0;   // Time of last interrupt (microsec)
  ix.duration = 0;
  ix.durCat = 0;
  ix.isRising = false;   // Datapin has received rising value
  ix.bitCount = 0;       // Current number of bits in packet
  ix.syncPassed = false;   // True if sync pulse is detected
  for (uint8_t j = 0; j < BUFFER_SIZE; j=j+1) {
    packetBits[j] = 0;
  }
  return;
} // initHandler()

String getPacketBits() {
  // Loop over packetBits to return string of packet bits
  String bitString; 
  for (uint8_t j=0; j <= 35; j=j+1) {
    if (j == 8)  {bitString += "_";}
    if (j == 12)  {bitString += "_";}
    if (j == 24)  {bitString += "_";}
    if (j == 28)  {bitString += "_";}
    bitString += String(packetBits[j]);
  }
  return bitString;
} // getPacketBits()

float getTemperature() {
  // Get temperature from received packetBits and return it.
  // Tested with pos and neg temperatures
  byte tempBits[12];
  Serial.print("Temperature bits:  ");
  for (uint8_t j=0; j <= 11; j=j+1) {
    tempBits[j] = packetBits[j+12];
    Serial.print(String(tempBits[j]) + " ");
  }
  Serial.println();

  // Convert tempBits to decimal int value tempInt
  bool negative;
  if (tempBits[0] == BIN0) {negative = false;} else {negative = true;}
  int tempInt = 0;
  for (uint8_t j=0; j<12; j++) { 
    tempInt = (tempInt << 1) + tempBits[j];
  }
  if (negative) { tempInt = -1 * (4096 - tempInt); }  // 2^12 = 4096

  float tempFloat = tempInt * 0.1; 
  return tempFloat;
} // getTemperature()

String getBatteryStatus() {
  // Get battery status from received packetBits and return it.
  byte batteryStatus = packetBits[8];
  String status = (batteryStatus == 1) ? "OK" : "LOW";
  return status;
} // getBatteryStatus()

String getChannel() {
  // Get channel from received packetBits and return it.
  String channel = String(packetBits[11]) + String(packetBits[12]);
  return channel;
} // getChannel()

int getDeviceID() {
  // Get deviceID from received packetBits and return it.
  byte deviceBits[8];
  for (uint8_t j = 0; j <= 7; j=j+1) {
    deviceBits[j] = packetBits[j];
  }

  // Convert deviceBits to decimal int value deviceInt
  int deviceInt = 0;
  for (uint8_t j = 0; j <= 7; j++) { 
    deviceInt = (deviceInt << 1) + deviceBits[j];
  }
  return deviceInt;
} // getDeviceID()

int getHumidity() {
  // Get Humidity from received packetBits and return it.
  byte humidityBits[8];
  for (uint8_t j = 0; j <= 7; j=j+1) {
    humidityBits[j] = packetBits[j+28];
  }

  // Convert humidityBits to decimal int value humidity
  int humidity = 0;
  for (uint8_t j = 0; j <= 7; j++) { 
    humidity = (humidity << 1) + humidityBits[j];
  }
  return humidity;
} // getHumidity()

void ICACHE_RAM_ATTR handler() {
  // Keyword ICACHE_RAM_ATTR necessary for ESP8266 boards.
  // Called by interrupt if DATAPIN is going from LOW to HIGH or HIGH to LOW
  // The packetBits array stores the bits defined by the duration of LOW states (gaps).

  // Ignore interrupt if we have not processed the previous received packet
  if (packetReceived == true) {
    return;
  }

  // Calculate timing since last change
  ix.time = micros();
  ix.duration = ix.time - ix.lastTime;
  ix.lastTime = ix.time; 
  ix.durCat = nn.findNearestNum(ix.duration);

  // Interrupt triggered by LOW-HIGH (RISING) or HIGH-LOW (FALLING)
  // RISING-trigger: gap finished. FALLING-trigger: pulse finished
  ix.isRising = (digitalRead(DATAPIN) == HIGH) ? true : false;

  if ((ix.isRising == true) && (ix.durCat == 4000)) {
    // Sync gap detected. Wait for next packet interrupts.
    ix.syncPassed = true; 
    ix.bitCount = 0;
    }
  else if ((ix.isRising == false) && (ix.durCat == 500)) {
    // Pulse detected. Wait for next interrupt. Do nothing.
    }
  else if ( (ix.syncPassed == true) && (ix.isRising == true) && ( (ix.durCat == 1000)  || (ix.durCat == 2000) ) ) {
    // Bit 0 or bit 1 gap detected. Store new bit in array buffer.
    ix.bitCount ++;
    if (ix.durCat == 1000) { packetBits[ix.bitCount-1] = BIN0; };
    if (ix.durCat == 2000) { packetBits[ix.bitCount-1] = BIN1; };
 
    if (ix.bitCount == 35) {
      // The width of the last gap is undefined because there is not a desync pulse.
      // So we can not know if bit 36 (the last) is HIGH or LOW. Therefore we choose one: HIGH.
      ix.bitCount ++;
      packetBits[ix.bitCount-1] = BIN1;
      ix.syncPassed = false; 
      packetReceived = true;       // Packet ready. Packet can be processed by loop()
    }}
  else if ( (ix.durCat == nn.getNumLow()) || (ix.durCat == nn.getNumHigh()) ) {
    // Undefined pulse or gap width. Skip packet reading and wait for new packet.
    ix.syncPassed = false;
    ix.bitCount = 0;
    }
  else {
    // Undefined condition. E.g: small gap of 500 us
    // Skip packet reading and wait for new packet.
    ix.syncPassed = false; 
    ix.bitCount = 0;
  }
  return;
} // handler()

void setup() {
  Serial.begin(115200);
  delay(6000);
  Serial.println();
  Serial.println("============================================");
  Serial.println("Start receiver");
  pinMode(DATAPIN, INPUT);    // Set Receiver input mode
  pinMode(ledPin,OUTPUT);     // Set LED pin to output mode
  initHandler();
  packetReceived = false;
  Serial.println("Attach interrupt, waiting for receiver interrupts ...");
  digitalWrite(ledPin, LOW); // LED ON
  attachInterrupt(digitalPinToInterrupt(DATAPIN), handler, CHANGE);
  return;
} // setup()

void loop() {
  if (packetReceived == true) {
    // Packet from 433 MHz signal as bitstring available.
    // Disable interrupt to avoid new data corrupting the buffer
    detachInterrupt(digitalPinToInterrupt(DATAPIN));
    digitalWrite(ledPin, HIGH); // LED OFF
    Serial.println("Interrupt detached, processing data started");
    Serial.println("============================================");
    Serial.println();

    String packetBits = getPacketBits();
    Serial.print("Received packet bits: ");
    Serial.println(packetBits);  // Example: 00100011_1000_000001001001_1111_00010101

    int deviceID = getDeviceID();
    Serial.println("Device id: " + String(deviceID));

    String bs = getBatteryStatus();
    Serial.println("Battery status: " + bs);

    String ch = getChannel();
    Serial.println("Channel: " + ch);

    float temp = getTemperature();
    Serial.print("Temperature is: ");
    Serial.print(temp, 1);
    Serial.println(" C");

    int humidity = getHumidity();
    Serial.println("Humidity: " + String(humidity) + " %");

    // Delay loop for minimal 1 sec to avoid repeating packets
    Serial.println();
    Serial.println("Processing data completed, wait 4 sec to continue ...");
    delay(4000);
    packetReceived = false;
    initHandler();
    Serial.println("Attach interrupt again, waiting for receiver interrupts ...");
    digitalWrite(ledPin, LOW); // LED ON
    attachInterrupt(digitalPinToInterrupt(DATAPIN), handler, CHANGE);    // re-enable interrupt
  } // if received
  return;
} // loop()
// ********************************************************************************
