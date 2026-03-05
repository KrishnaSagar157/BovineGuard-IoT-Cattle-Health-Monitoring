#include <Wire.h>
#include <MPU6500_WE.h>
#include <MAX30105.h>
#include <heartRate.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <HardwareSerial.h>

// ---------------- SENSOR PINS ----------------
#define I2C1_SDA 8
#define I2C1_SCL 9
#define I2C2_SDA 4
#define I2C2_SCL 5
#define TEMP_PIN 14

// ---------------- GSM PINS (UPDATED) ----------------
// ESP32 GPIO 18 (RX) <--- connects to ---> SIM Module TX
// ESP32 GPIO 17 (TX) <--- connects to ---> SIM Module RX
#define GSM_RX 18   
#define GSM_TX 17   

HardwareSerial SerialGSM(2);

// ---------------- SENSOR OBJECTS -----------------
MPU6500_WE myMPU(0x68);
MAX30105 maxSensor;
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

// ---------------- CONFIGURATION -----------------
String PHONE = "PHONE_NUMBER";  // Destination Number

// ====================================================================
// WAIT-FOR FUNCTION
// Helper to read GSM responses with a timeout
// ====================================================================
bool waitFor(String target, uint32_t timeout = 6000) {
  uint32_t start = millis();
  String resp = "";

  while (millis() - start < timeout) {
    while (SerialGSM.available()) {
      char c = SerialGSM.read();
      resp += c;
      Serial.print(c); // Print GSM response to Serial Monitor for debugging

      if (resp.indexOf(target) != -1) return true;
    }
    delay(10); // Short delay to allow buffer fill
  }
  return false;
}

// ====================================================================
// GSM SETUP FUNCTION (ROBUST VERSION)
// ====================================================================
void setupGSM() {
  Serial.println("\n--- Initializing GSM Module ---");

  // 1. Handshake Loop: Wait until modem replies "OK" to "AT"
  bool modemReady = false;
  int retryCount = 0;
  
  while (!modemReady) {
    SerialGSM.println("AT");
    if (waitFor("OK", 1000)) {
      modemReady = true;
      Serial.println("\n[SUCCESS] Modem Connected!");
    } else {
      Serial.print(".");
      retryCount++;
      if (retryCount > 10) {
         Serial.println("\n[WARNING] Modem not responding. Check Power & Wiring!");
         retryCount = 0;
      }
      delay(1000); 
    }
  }

  // 2. Configure Settings
  Serial.println("Configuring Modem...");
  
  SerialGSM.println("ATE0");      // Turn off Echo (cleans up output)
  waitFor("OK");

  SerialGSM.println("AT+CMGF=1"); // Set SMS to TEXT Mode (Crucial!)
  waitFor("OK");

  SerialGSM.println("AT+CSCS=\"GSM\""); // Set Character Set
  waitFor("OK");
  
  // 3. Network Check
  Serial.println("Checking Network Registration...");
  SerialGSM.println("AT+CREG?");
  // Use a longer timeout here as network search can take time
  waitFor("OK", 5000); 

  Serial.println("--- GSM Setup Complete ---\n");
}

// ====================================================================
// SEND SMS FUNCTION
// ====================================================================
bool sendSMS(String number, String text) {
  Serial.println("--- Sending SMS ---");

  // 1. Send Command
  SerialGSM.print("AT+CMGS=\"");
  SerialGSM.print(number);
  SerialGSM.println("\"");

  // 2. Wait for '>' Prompt
  if (!waitFor(">", 5000)) {
    Serial.println("\n[ERROR] No '>' prompt received. Mode incorrect or busy.");
    return false;
  }

  // 3. Send Text + CTRL+Z (ASCII 26)
  SerialGSM.print(text);
  SerialGSM.write(26); 

  // 4. Wait for Confirmation
  if (!waitFor("OK", 15000)) {
    Serial.println("\n[ERROR] SMS Sending Timed Out/Failed.");
    return false;
  }

  Serial.println("\n[SUCCESS] SMS Sent Successfully!");
  return true;
}

// ====================================================================
// SENSOR INIT FUNCTIONS
// ====================================================================
void initMPU() {
  Wire.begin(I2C1_SDA, I2C1_SCL);
  if (!myMPU.init()) {
    Serial.println("MPU6500 ERROR: Check wiring (SDA=8, SCL=9)");
    while (1);
  }
  myMPU.setGyrRange(MPU6500_GYRO_RANGE_250);
  myMPU.setAccRange(MPU6500_ACC_RANGE_2G);
  Serial.println("MPU6500 Ready");
}

void initMAX30102() {
  // Use Wire1 for second I2C bus
  Wire1.begin(I2C2_SDA, I2C2_SCL);
  if (!maxSensor.begin(Wire1, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 ERROR: Check wiring (SDA=4, SCL=5)");
    while (1);
  }
  maxSensor.setup();
  maxSensor.setPulseAmplitudeRed(0x0A);
  maxSensor.setPulseAmplitudeGreen(0);
  Serial.println("MAX30102 Ready");
}

// ====================================================================
// MAIN SETUP
// ====================================================================
void setup() {
  // USB Serial for Debugging
  Serial.begin(115200);
  
  // Initialize Sensors
  initMPU();
  initMAX30102();
  sensors.begin(); // Temp Sensor

  // Initialize GSM Serial on new pins 17 & 18
  SerialGSM.begin(115200, SERIAL_8N1, GSM_RX, GSM_TX);  
  delay(2000); // Give modem a moment to power up

  // Run the robust GSM setup
  setupGSM();
}

// ====================================================================
// MAIN LOOP
// ====================================================================
void loop() {
  // 1. Read Sensors
  xyzFloat acc = myMPU.getAccRawValues();
  xyzFloat gyr = myMPU.getGyrRawValues();
  long irValue = maxSensor.getIR();

  sensors.requestTemperatures();
  float temperatureC = sensors.getTempCByIndex(0);

  // 2. Build SMS Message
  String msg = "ACC(" + String(acc.x,1) + "," + String(acc.y,1) + "," + String(acc.z,1) + ") ";
  msg += "GYR(" + String(gyr.x,1) + "," + String(gyr.y,1) + "," + String(gyr.z,1) + ") ";
  msg += "IR=" + String(irValue) + " ";
  msg += "TEMP=" + String(temperatureC,1);

  Serial.println("Data Prepared:");
  Serial.println(msg);

  // 3. Send SMS
  bool ok = sendSMS(PHONE, msg);

  // 4. Wait before next reading
  Serial.println("Waiting 15 seconds...");
  delay(60000);
}
