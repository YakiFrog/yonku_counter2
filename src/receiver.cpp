#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "Adafruit_VL6180X.h"

// BLE設定
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// LEDピン定義（PWM対応ピン）
#define RED_LED_PIN D0    // 赤色LED（PWM対応）
#define BLUE_LED_PIN D1   // 青色LED（PWM対応）

// デバイス識別構造体
struct DeviceCalibration {
  String macAddress;
  int deviceNumber;
  String deviceName;
  // オフセット校正値
  int offsetCalibration;   // オフセット補正値（mm）
};

// 各デバイスの校正設定（WiFi MACアドレスベース）
DeviceCalibration devices[] = {
  {"cc:ba:97:15:4d:0c", 1, "Device1", 125},
  {"cc:ba:97:15:53:20", 2, "Device2", 55},  // WiFi MACアドレス
  {"cc:ba:97:15:4f:28", 3, "Device3", -5},
  {"cc:ba:97:15:37:34", 4, "Device4", 0} 
};

// BLE関連変数
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// グローバル変数
bool sensorAvailable = false;
DeviceCalibration currentDevice;
bool deviceIdentified = false;

// VL6180Xセンサーインスタンス
Adafruit_VL6180X vl = Adafruit_VL6180X();

// ベースライン距離とカウント関連変数
int baselineDistance = 0;        // 何も通過していないときのベースライン距離
bool baselineCalibrated = false;
int deviceCount = 0;             // このデバイスのカウント数
unsigned long lastCountTime = 0; // 最後にカウントした時刻
const unsigned long COUNT_IGNORE_DURATION = 3000; // 3秒間の重複カウント防止
const int DETECTION_THRESHOLD = 16; // 検出閾値（ベースライン距離からの差mm）

// LED制御関連変数
bool countUpLEDActive = false;   // カウントアップ点滅制御
unsigned long countUpLEDStartTime = 0; // カウントアップLED開始時刻
const unsigned long COUNT_UP_LED_DURATION = 3000; // 3秒間の点滅時間

// BLE接続状態管理コールバッククラス
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

// デバイス識別機能
void identifyDevice() {
  // WiFi MACアドレスを取得
  String wifiMacAddress = WiFi.macAddress();
  wifiMacAddress.toLowerCase();
  
  Serial.print("WiFi MAC address: ");
  Serial.println(wifiMacAddress);
  
  // Bluetooth MACアドレスも表示（参考用）
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
  
  // デバイス配列から一致するデバイスを検索（WiFi MACで識別）
  for (int i = 0; i < 4; i++) {
    if (devices[i].macAddress == wifiMacAddress) {
      currentDevice = devices[i];
      deviceIdentified = true;
      Serial.print("Device identification completed: ");
      Serial.print(currentDevice.deviceName);
      Serial.print(" (number: ");
      Serial.print(currentDevice.deviceNumber);
      Serial.println(")");
      
      // デバイス固有のオフセット値を表示
      Serial.print("Offset value: ");
      Serial.print(currentDevice.offsetCalibration);
      Serial.println("mm");
      return;
    }
  }
  
  // 未登録デバイスの場合のデフォルト値を使用
  Serial.println("WARNING: Unregistered device. Using default settings.");
  currentDevice = {"unknown", 0, "Unknown Device", 0};
  deviceIdentified = false;
}

// LED強度設定機能
void setLEDIntensity(int redIntensity, int blueIntensity) {
  analogWrite(RED_LED_PIN, redIntensity);   // 0-255の範囲
  analogWrite(BLUE_LED_PIN, blueIntensity); // 0-255の範囲
}

// BLE初期化
void initBLE() {
  String deviceName = "YonkuCounter_" + String(currentDevice.deviceNumber);
  
  BLEDevice::init(deviceName.c_str());
  
  // BLE送信出力を最大に設定（接続安定性向上）
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9); // 出力を最大値（+9dBm）に設定
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);     // アドバタイジング出力を最大に設定
  
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
  pAdvertising->setMinPreferred(0x0);  // iOS接続性向上
  BLEDevice::startAdvertising();
  
  Serial.println("BLE initialization complete - " + deviceName);
  Serial.println("Advertising started - waiting for connection...");
  Serial.print("Service UUID: ");
  Serial.println(SERVICE_UUID);
  Serial.print("Characteristic UUID: ");
  Serial.println(CHARACTERISTIC_UUID);
}

// ベースライン距離校正機能
void calibrateBaseline() {
  if (!sensorAvailable) {
    Serial.println("Sensor not available. Skipping baseline distance setup.");
    return;
  }
  
  Serial.println("=== Baseline Distance Calibration Start ===");
  Serial.println("Measuring baseline distance with nothing in the lane...");
  
  // 複数回測定して平均を取る
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
  
  if (validMeasurements >= measurements / 2) { // 半分以上の測定が成功した場合
    baselineDistance = totalRange / validMeasurements;
    baselineCalibrated = true;
    
    Serial.print("Baseline distance setup complete: ");
    Serial.print(baselineDistance);
    Serial.println("mm");
    Serial.print("Detection threshold: ");
    Serial.print(baselineDistance - DETECTION_THRESHOLD); // 閾値分下回った際に検出
    Serial.println("mm or less");
  } else {
    Serial.println("Failed to set baseline distance. Using fixed value.");
    baselineDistance = 200; // デフォルト値
    baselineCalibrated = false;
  }
  
  Serial.println("=== Baseline Distance Calibration End ===");
}

void setup() {
  Serial.begin(115200);
  delay(1000); // シリアル通信の安定化待機
  Serial.flush(); // バッファクリア
  Serial.println("Yonku Counter Receiver (Individual Sensor) Program Starting");
  
  // WiFiを初期化してMACアドレスを取得（接続しない）
  WiFi.mode(WIFI_STA);
  delay(100);
  
  // デバイス識別実行
  identifyDevice();
  
  // BLE初期化
  initBLE();
  
  // I2C通信を初期化（明示的設定）
  Wire.begin();
  Wire.setClock(100000); // I2Cクロックを100kHzに設定（安定性向上）
  
  // LEDピンを出力モードに設定（センサー初期化前に実行）
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);
  
  // 最初は両LEDを消灯
  setLEDIntensity(0, 0);
  
  // 起動を示すLED点滅
  for(int i = 0; i < 3; i++) {
    setLEDIntensity(0, 100);
    delay(200);
    setLEDIntensity(0, 0);
    delay(200);
  }
  
  // VL6180Xセンサー初期化（タイムアウト付き）
  Serial.println("Starting VL6180X sensor initialization...");
  
  int retryCount = 0;
  const int maxRetries = 10;
  bool sensorInitialized = false;
  
  while (retryCount < maxRetries && !sensorInitialized) {
    if (vl.begin()) {
      sensorInitialized = true;
      sensorAvailable = true; // センサー初期化成功
      Serial.println("VL6180X sensor initialization complete");
    } else {
      retryCount++;
      Serial.print("VL6180X initialization failed (attempt ");
      Serial.print(retryCount);
      Serial.print("/");
      Serial.print(maxRetries);
      Serial.println(")");
      
      // エラー状態を示すLED点滅
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
    // センサーなしでも動作継続（無限ループ回避）
  } else {
    // デバイス固有のオフセットを適用
    if (currentDevice.offsetCalibration != 0) {
      vl.setOffset(currentDevice.offsetCalibration);
      Serial.print("Offset applied: ");
      Serial.print(currentDevice.offsetCalibration);
      Serial.println("mm");
    }
    
    // 安定性向上のためシングルショット測定モードを開始
    Serial.println("Single-shot measurement mode started");
    Serial.println("Executing baseline distance calibration...");
    
    // ベースライン距離校正を実行
    delay(2000); // センサーの安定化待機
    calibrateBaseline();
    
    Serial.println("To re-run calibration, send 'c'");
  }
  
  // 初期化完了を示すLED点滅
  for(int i = 0; i < 2; i++) {
    setLEDIntensity(0, 255);
    delay(300);
    setLEDIntensity(0, 0);
    delay(300);
  }
  setLEDIntensity(0, 100); // 通常の青色点灯で開始
  
  Serial.println("Receiver setup complete");
}

void loop() {
  // BLE接続状態管理
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // BLEスタックに準備時間を与える
    pServer->startAdvertising(); // アドバタイジング再開
    Serial.println("Advertising restarted");
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  // 校正コマンドをチェック
  if (Serial.available() > 0) {
    char command = Serial.read();
    if (command == 'c' || command == 'C') {
      calibrateBaseline();
      // バッファをクリア
      while (Serial.available()) {
        Serial.read();
      }
    }
  }
  
  // センサーが利用可能な場合のみセンサー読み取りを実行
  if (sensorAvailable) {
    // VL6180Xセンサーから距離を読み取り（シングルショット測定モード使用）
    uint8_t range = vl.readRange();
    uint8_t status = vl.readRangeStatus();
  
    // 測定エラーをチェック
    if (status == VL6180X_ERROR_NONE) {
      // 距離データの出力頻度を制限（USB負荷軽減）
      static unsigned long lastPrintTime = 0;
      if (millis() - lastPrintTime > 1000) { // 1秒間隔で距離を出力
        Serial.print("[");
        Serial.print(currentDevice.deviceName);
        Serial.print("] Distance: ");
        Serial.print(range);
        Serial.print("mm, Count: ");
        Serial.println(deviceCount);
        lastPrintTime = millis();
      }
      
      // ミニ四駆通過検知（ベースライン距離より閾値以上小さい場合）
      if (baselineCalibrated && range < (baselineDistance - DETECTION_THRESHOLD)) {
        // 重複カウント防止チェック
        unsigned long currentTime = millis();
        static bool canCountUpMessageShown = false; // カウントアップ可能メッセージの表示フラグ
        
        // 待機時間が経過しているかチェック
        bool canCountUp = (currentTime - lastCountTime > COUNT_IGNORE_DURATION);
        
        // カウントアップ可能タイミングでのメッセージ（待機時間終了時に一度表示）
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
          
          // カウントアップLED制御開始
          countUpLEDActive = true;
          countUpLEDStartTime = currentTime;
          setLEDIntensity(0, 0); // 一時的に青色を消灯
          
          // カウントアップ可能メッセージフラグをリセット（次回カウントアップ用）
          canCountUpMessageShown = false;

          // --- ここでゲート番号に応じた文字列をBLE送信 ---
          if (deviceConnected && pCharacteristic) {
            char gateChar = 'a'; // デフォルト: 1
            if (currentDevice.deviceNumber == 1) gateChar = 'a';
            else if (currentDevice.deviceNumber == 2) gateChar = 's';
            else if (currentDevice.deviceNumber == 3) gateChar = 'd';
            else if (currentDevice.deviceNumber == 4) gateChar = 'f';
            String gateStr = String(gateChar);
            pCharacteristic->setValue(gateStr.c_str());
            pCharacteristic->notify();
            Serial.print("BLE sent gate char: ");
            Serial.println(gateStr);
          }
        } else {
          // カウントアップできない場合のログ
          Serial.print("[Count waiting] Distance: ");
          Serial.print(range);
          Serial.print("mm, remaining wait time: ");
          Serial.print(COUNT_IGNORE_DURATION - (currentTime - lastCountTime));
          Serial.println("ms");
        }
        
        // ベースライン以下の値が続く限り待機時間を延長するためlastCountTimeを更新
        lastCountTime = currentTime;
        
        // LED制御：物体検知時は赤色点灯（カウントアップ中でない場合）
        if (!countUpLEDActive) {
          setLEDIntensity(255, 0); // 赤色点灯（検知中）
        }
      } else {
        // 通過していない場合は通常の青色点灯
        if (!countUpLEDActive) {
          setLEDIntensity(0, 100); // 通常の青色点灯
        }
      }
    } else {
      // エラー時は赤色LED点滅
      static unsigned long lastErrorFlashTime = 0;
      static bool errorFlashState = false;
      if (millis() - lastErrorFlashTime > 250) { // 250ms間隔で点滅
        errorFlashState = !errorFlashState;
        setLEDIntensity(errorFlashState ? 255 : 0, 0);
        lastErrorFlashTime = millis();
      }
    }
    
    // カウントアップLED点滅制御
    if (countUpLEDActive) {
      unsigned long currentTime = millis();
      unsigned long elapsedTime = currentTime - countUpLEDStartTime;
      
      if (elapsedTime < COUNT_UP_LED_DURATION) { // もし，elapsedTimeが3秒未満なら
        // 3秒間点滅（250ms間隔）
        static unsigned long lastBlinkTime = 0;
        static bool blinkState = false;
        
        if (currentTime - lastBlinkTime > 250) {
          blinkState = !blinkState;
          setLEDIntensity(0, blinkState ? 255 : 0); // 青色点滅
          lastBlinkTime = currentTime;
        }
      } else {
        // 3秒経過後にカウントアップLED制御終了
        countUpLEDActive = false;
        setLEDIntensity(0, 100); // 通常の青色点灯に戻す
      }
    }
    
    // 常にBLEでカウントデータを送信（データ消失防止）
    static unsigned long lastBLESendTime = 0;
    // ↓カウント送信を不要ならコメントアウト
    if (deviceConnected && pCharacteristic && millis() - lastBLESendTime > 25) { // 1/25ms(40Hz)間隔で今のカウントを送信
      String countData = String(currentDevice.deviceNumber) + ":" + String(deviceCount);
      pCharacteristic->setValue(countData.c_str());
      pCharacteristic->notify();

      // 通信中の青色点滅（カウントアップ中でない場合のみ）
      if (!countUpLEDActive) {
        static unsigned long commLEDStartTime = 0;
        static bool commLEDActive = false;
        
        if (!commLEDActive) {
          setLEDIntensity(0, 255); // 青色点滅
          commLEDStartTime = millis();
          commLEDActive = true;
        } else if (millis() - commLEDStartTime > 50) { // 50ms間点滅
          setLEDIntensity(0, 100); // 通常の青色点滅に戻す
          commLEDActive = false;
        }
      }
      
      lastBLESendTime = millis();
    }
  } else {
    // センサーレスモード：青色点灯で待機状態を表示
    static unsigned long lastPatternTime = 0;
    static bool patternState = false;
    
    if (millis() - lastPatternTime > 1000) { // 1秒間隔でパターン変更
      patternState = !patternState;
      setLEDIntensity(0, patternState ? 200 : 50); // 青色点滅
      lastPatternTime = millis();
    }
  }
  
  // 定期的にシリアルバッファをフラッシュ（USB安定性向上）
  static unsigned long lastFlushTime = 0;
  if (millis() - lastFlushTime > 1000) { // 1秒間隔
    Serial.flush();
    lastFlushTime = millis();
  }
  
  delay(20); // 1/20ms（50Hz）で測定
}
