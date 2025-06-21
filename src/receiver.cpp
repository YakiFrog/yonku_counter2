#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "Adafruit_VL6180X.h"

// BLE settings
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// LED pin definitions (PWM compatible pins)
#define RED_LED_PIN D0    // Red LED (PWM compatible)
#define BLUE_LED_PIN D1   // Blue LED (PWM compatible)

// Device identification structure
struct DeviceCalibration {
  String macAddress;
  int deviceNumber;
  String deviceName;
  // Offset calibration values
  int offsetCalibration;   // Offset correction value (mm)
};

// Each device calibration settings (WiFi MAC address based)
DeviceCalibration devices[] = {
  {"cc:ba:97:15:4d:0c", 1, "Device1", 125},
  {"cc:ba:97:15:53:20", 2, "Device2", 55},  // WiFi MAC address
  {"cc:ba:97:15:4f:28", 3, "Device3", -5},
  {"cc:ba:97:15:37:34", 4, "Device4", 0} 
};

// BLE related variables
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Global variables
bool sensorAvailable = false;
DeviceCalibration currentDevice;
bool deviceIdentified = false;

// VL6180X sensor instance
Adafruit_VL6180X vl = Adafruit_VL6180X();

// Baseline distance and count related variables
int baselineDistance = 0;        // Baseline distance when nothing is passing through
bool baselineCalibrated = false;
int deviceCount = 0;             // Count for this device
unsigned long lastCountTime = 0; // Last time a count was made
const unsigned long COUNT_IGNORE_DURATION = 3000; // 3-second duplicate count prevention
const int DETECTION_THRESHOLD = 15; // Detection threshold (difference from baseline distance mm)

// LED control related variables
bool countUpLEDActive = false;   // Count-up blink control
unsigned long countUpLEDStartTime = 0; // Count-up LED start time
const unsigned long COUNT_UP_LED_DURATION = 3000; // 3-second blink duration

// BLE connection state management callback class
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("*** BLE client connected ***");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("*** BLE client disconnected ***");
    }
};

// Device identification function
void identifyDevice() {
  // Get WiFi MAC address
  String wifiMacAddress = WiFi.macAddress();
  wifiMacAddress.toLowerCase();
  
  Serial.print("WiFi MAC address: ");
  Serial.println(wifiMacAddress);
  
  // Also display Bluetooth MAC address (for reference)
  String btMacStr = "";
  uint8_t btMac[6];
  esp_read_mac(btMac, ESP_MAC_BT);
  for (int i = 0; i < 6; i++) {
    if (i > 0) btMacStr += ":";
    if (btMac[i] < 0x10) btMacStr += "0";
    btMacStr += String(btMac[i], HEX);
  }
  btMacStr.toLowerCase();
  Serial.print("Bluetooth MAC address: ");
  Serial.println(btMacStr);
  
  // Search device array for matching device (identify by WiFi MAC)
  for (int i = 0; i < 4; i++) {
    if (devices[i].macAddress == wifiMacAddress) {
      currentDevice = devices[i];
      deviceIdentified = true;
      Serial.print("Device identification completed: ");
      Serial.print(currentDevice.deviceName);
      Serial.print(" (number: ");
      Serial.print(currentDevice.deviceNumber);
      Serial.println(")");
      
      // Display device-specific offset value
      Serial.print("Offset value: ");
      Serial.print(currentDevice.offsetCalibration);
      Serial.println("mm");
      return;
    }
  }
  
  // Use default values for unregistered devices
  Serial.println("WARNING: Unregistered device. Using default settings.");
  currentDevice = {"unknown", 0, "Unknown Device", 0};
  deviceIdentified = false;
}

// LED intensity setting function
void setLEDIntensity(int redIntensity, int blueIntensity) {
  analogWrite(RED_LED_PIN, redIntensity);   // 0-255 range
  analogWrite(BLUE_LED_PIN, blueIntensity); // 0-255 range
}

// BLE initialization
void initBLE() {
  String deviceName = "YonkuCounter_" + String(currentDevice.deviceNumber);
  
  BLEDevice::init(deviceName.c_str());
  
  // Set BLE transmission power to maximum (improve connection stability)
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9); // Set output power to maximum (+9dBm)
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);     // Set advertising output to maximum
  
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x0);  // Improve iOS connectivity
  BLEDevice::startAdvertising();
  
  Serial.println("BLE initialization complete - " + deviceName);
  Serial.println("Advertising started - waiting for connection...");
  Serial.print("Service UUID: ");
  Serial.println(SERVICE_UUID);
  Serial.print("Characteristic UUID: ");
  Serial.println(CHARACTERISTIC_UUID);
}

// Baseline distance calibration function
void calibrateBaseline() {
  if (!sensorAvailable) {
    Serial.println("Sensor not available. Skipping baseline distance setup.");
    return;
  }
  
  Serial.println("=== Baseline Distance Calibration Start ===");
  Serial.println("Measuring baseline distance with nothing in the lane...");
  
  // Take multiple measurements and get average
  int measurements = 20;
  int totalRange = 0;
  int validMeasurements = 0;
  
  Serial.println("Starting measurements...");
  for (int i = 0; i < measurements; i++) {
    uint8_t range = vl.readRange();
    uint8_t status = vl.readRangeStatus();
    
    if (status == VL6180X_ERROR_NONE) {
      totalRange += range;
      validMeasurements++;
      Serial.print("Measurement ");
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(range);
      Serial.println("mm");
    } else {
      Serial.print("Measurement ");
      Serial.print(i + 1);
      Serial.println(": Error");
    }
    delay(100);
  }
  
  if (validMeasurements >= measurements / 2) { // If more than half of measurements succeeded
    baselineDistance = totalRange / validMeasurements;
    baselineCalibrated = true;
    
    Serial.print("Baseline distance setup complete: ");
    Serial.print(baselineDistance);
    Serial.println("mm");
    Serial.print("Detection threshold: ");
    Serial.print(baselineDistance - DETECTION_THRESHOLD); // Detect when value is threshold below baseline
    Serial.println("mm or less");
  } else {
    Serial.println("Failed to set baseline distance. Using fixed value.");
    baselineDistance = 200; // Default value
    baselineCalibrated = false;
  }
  
  Serial.println("=== Baseline Distance Calibration End ===");
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Wait for serial communication stabilization
  Serial.flush(); // Clear buffer
  Serial.println("Yonku Counter Receiver (Individual Sensor) Program Starting");
  
  // Initialize WiFi to get MAC address (without connecting)
  WiFi.mode(WIFI_STA);
  delay(100);
  
  // Execute device identification
  identifyDevice();
  
  // BLE initialization
  initBLE();
  
  // Initialize I2C communication (explicit setting)
  Wire.begin();
  Wire.setClock(100000); // Set I2C clock to 100kHz (improve stability)
  
  // Set LED pins to output mode (before sensor initialization)
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);
  
  // Turn off both LEDs initially
  setLEDIntensity(0, 0);
  
  // LED blinking to indicate startup
  for(int i = 0; i < 3; i++) {
    setLEDIntensity(0, 100);
    delay(200);
    setLEDIntensity(0, 0);
    delay(200);
  }
  
  // VL6180X sensor initialization (with timeout)
  Serial.println("Starting VL6180X sensor initialization...");
  
  int retryCount = 0;
  const int maxRetries = 10;
  bool sensorInitialized = false;
  
  while (retryCount < maxRetries && !sensorInitialized) {
    if (vl.begin()) {
      sensorInitialized = true;
      sensorAvailable = true; // Sensor initialization successful
      Serial.println("VL6180X sensor initialization complete");
    } else {
      retryCount++;
      Serial.print("VL6180X initialization failed (attempt ");
      Serial.print(retryCount);
      Serial.print("/");
      Serial.print(maxRetries);
      Serial.println(")");
      
      // LED blinking to indicate error state
      setLEDIntensity(255, 0);
      delay(500);
      setLEDIntensity(0, 0);
      delay(500);
      
      if (retryCount < maxRetries) {
        Serial.println("Retrying in 2 seconds...");
        delay(2000);
      }
    }
  }
  
  if (!sensorInitialized) {
    Serial.println("VL6180X sensor initialization failed.");
    Serial.println("Operating in sensor-less mode.");
    // Continue operation without sensor (avoid infinite loop)
  } else {
    // Apply device-specific offset
    if (currentDevice.offsetCalibration != 0) {
      vl.setOffset(currentDevice.offsetCalibration);
      Serial.print("Offset applied: ");
      Serial.print(currentDevice.offsetCalibration);
      Serial.println("mm");
    }
    
    // Start single-shot measurement mode for improved stability
    Serial.println("Single-shot measurement mode started");
    Serial.println("Executing baseline distance calibration...");
    
    // Execute baseline distance calibration
    delay(2000); // Wait for sensor stabilization
    calibrateBaseline();
    
    Serial.println("To re-run calibration, send 'c'");
  }
  
  // LED blinking to indicate initialization complete
  for(int i = 0; i < 2; i++) {
    setLEDIntensity(0, 255);
    delay(300);
    setLEDIntensity(0, 0);
    delay(300);
  }
  setLEDIntensity(0, 100); // Start with normal blue lighting
  
  Serial.println("Receiver setup complete");
}

void loop() {
  // BLE connection state management
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // Give BLE stack time to prepare
    pServer->startAdvertising(); // Start advertising again
    Serial.println("Advertising restarted");
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  // Check for calibration command
  if (Serial.available() > 0) {
    char command = Serial.read();
    if (command == 'c' || command == 'C') {
      calibrateBaseline();
      // Clear buffer
      while (Serial.available()) {
        Serial.read();
      }
    }
  }
  
  // Execute sensor reading only when sensor is available
  if (sensorAvailable) {
    // Read distance from VL6180X sensor (using single-shot measurement mode)
    uint8_t range = vl.readRange();
    uint8_t status = vl.readRangeStatus();
  
    // Check for measurement errors
    if (status == VL6180X_ERROR_NONE) {
      // Limit distance data output frequency (reduce USB load)
      static unsigned long lastPrintTime = 0;
      if (millis() - lastPrintTime > 1000) { // Output distance every second
        Serial.print("[");
        Serial.print(currentDevice.deviceName);
        Serial.print("] Distance: ");
        Serial.print(range);
        Serial.print("mm, Count: ");
        Serial.println(deviceCount);
        lastPrintTime = millis();
      }
      
      // Mini 4WD passage detection (when smaller than baseline distance by threshold or more)
      if (baselineCalibrated && range < (baselineDistance - DETECTION_THRESHOLD)) {
        // Duplicate count prevention check
        unsigned long currentTime = millis();
        static bool canCountUpMessageShown = false; // Flag for count-up possible message display
        
        // Check if waiting time has elapsed
        bool canCountUp = (currentTime - lastCountTime > COUNT_IGNORE_DURATION);
        
        // Message for count-up possible timing (display once when waiting time ends)
        if (canCountUp && !canCountUpMessageShown) {
          Serial.println("=== Count-up timing reached ===");
          Serial.print("Elapsed since last count: ");
          Serial.print(currentTime - lastCountTime);
          Serial.println("ms");
          canCountUpMessageShown = true;
        }
        
        if (canCountUp) {
          deviceCount++;
          
          Serial.print("*** Mini 4WD passage detected! ***");
          Serial.print(" Distance: ");
          Serial.print(range);
          Serial.print("mm (baseline: ");
          Serial.print(baselineDistance);
          Serial.print("mm) Count: ");
          Serial.println(deviceCount);
          
          // Start count-up LED control
          countUpLEDActive = true;
          countUpLEDStartTime = currentTime;
          setLEDIntensity(0, 0); // Turn off blue momentarily
          
          // Reset count-up possible message flag (for next count-up)
          canCountUpMessageShown = false;
        } else {
          // Log when count-up is not possible
          Serial.print("[Count waiting] Distance: ");
          Serial.print(range);
          Serial.print("mm, remaining wait time: ");
          Serial.print(COUNT_IGNORE_DURATION - (currentTime - lastCountTime));
          Serial.println("ms");
        }
        
        // Update lastCountTime to extend waiting time as long as values below baseline continue
        lastCountTime = currentTime;
        
        // LED control: red light when object detected (if not during count-up)
        if (!countUpLEDActive) {
          setLEDIntensity(255, 0); // Red light (detecting)
        }
      } else {
        // Normal blue light when not passing through
        if (!countUpLEDActive) {
          setLEDIntensity(0, 100); // Normal blue light
        }
      }
    } else {
      // Flash red LED during errors
      static unsigned long lastErrorFlashTime = 0;
      static bool errorFlashState = false;
      if (millis() - lastErrorFlashTime > 250) { // Flash every 250ms
        errorFlashState = !errorFlashState;
        setLEDIntensity(errorFlashState ? 255 : 0, 0);
        lastErrorFlashTime = millis();
      }
    }
    
    // Count-up LED blinking control
    if (countUpLEDActive) {
      unsigned long currentTime = millis();
      unsigned long elapsedTime = currentTime - countUpLEDStartTime;
      
      if (elapsedTime < COUNT_UP_LED_DURATION) {
        // Blink for 3 seconds (every 250ms)
        static unsigned long lastBlinkTime = 0;
        static bool blinkState = false;
        
        if (currentTime - lastBlinkTime > 250) {
          blinkState = !blinkState;
          setLEDIntensity(0, blinkState ? 255 : 0); // Blue blinking
          lastBlinkTime = currentTime;
        }
      } else {
        // End count-up LED control after 3 seconds
        countUpLEDActive = false;
        setLEDIntensity(0, 100); // Return to normal blue light
      }
    }
    
    // Always send count data via BLE (prevent data loss)
    static unsigned long lastBLESendTime = 0;
    if (deviceConnected && pCharacteristic && millis() - lastBLESendTime > 100) { // Send every 100ms
      String countData = String(currentDevice.deviceNumber) + ":" + String(deviceCount);
      pCharacteristic->setValue(countData.c_str());
      pCharacteristic->notify();
      
      // Blue light during communication (only when not during count-up)
      if (!countUpLEDActive) {
        static unsigned long commLEDStartTime = 0;
        static bool commLEDActive = false;
        
        if (!commLEDActive) {
          setLEDIntensity(0, 255); // Blue light
          commLEDStartTime = millis();
          commLEDActive = true;
        } else if (millis() - commLEDStartTime > 50) { // Light for 50ms
          setLEDIntensity(0, 100); // Return to normal blue light
          commLEDActive = false;
        }
      }
      
      lastBLESendTime = millis();
    }
  } else {
    // Sensor-less mode: Display standby state with blue light
    static unsigned long lastPatternTime = 0;
    static bool patternState = false;
    
    if (millis() - lastPatternTime > 1000) { // Change pattern every second
      patternState = !patternState;
      setLEDIntensity(0, patternState ? 200 : 50); // Blue blinking
      lastPatternTime = millis();
    }
  }
  
  // Periodically flush serial buffer (improve USB stability)
  static unsigned long lastFlushTime = 0;
  if (millis() - lastFlushTime > 1000) { // Every second
    Serial.flush();
    lastFlushTime = millis();
  }
  
  delay(20);
}
