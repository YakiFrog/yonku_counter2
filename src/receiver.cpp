#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "Adafruit_VL6180X.h"

// BLEの設定
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// LEDピンの定義（PWM対応ピン）
#define RED_LED_PIN D0    // 赤色LED（PWM対応）
#define BLUE_LED_PIN D1   // 青色LED（PWM対応）

// デバイス識別用の構造体
struct DeviceCalibration {
  String macAddress;
  int deviceNumber;
  String deviceName;
  // オフセットキャリブレーション値
  int offsetCalibration;   // オフセット補正値（mm）
};

// 各デバイスのキャリブレーション設定（オフセット値を個別に設定可能）
DeviceCalibration devices[] = {
  {"cc:ba:97:15:4d:0c", 1, "デバイス1", 125},
  {"cc:ba:97:15:53:20", 2, "デバイス2", 55},
  {"cc:ba:97:15:4f:28", 3, "デバイス3", -5},
  {"cc:ba:97:15:37:35", 4, "デバイス4", 0}  // 修正: 35に変更
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

// VL6180Xセンサーのインスタンス
Adafruit_VL6180X vl = Adafruit_VL6180X();

// 基準距離とカウント関連
int baselineDistance = 0;        // 何も通っていない状態の基準距離
bool baselineCalibrated = false;
int deviceCount = 0;             // このデバイスのカウント
unsigned long lastCountTime = 0; // 最後にカウントした時刻
const unsigned long COUNT_IGNORE_DURATION = 3000; // 3秒間重複カウント防止
const int DETECTION_THRESHOLD = 15; // 検出閾値（基準距離からの差分mm）

// LED制御関連
bool countUpLEDActive = false;   // カウントアップ時の点滅制御
unsigned long countUpLEDStartTime = 0; // カウントアップ時LED開始時刻
const unsigned long COUNT_UP_LED_DURATION = 3000; // 3秒間点滅

// BLE接続状態管理用コールバッククラス
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("*** BLEクライアントが接続されました ***");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("*** BLEクライアントが切断されました ***");
    }
};

// デバイス識別機能
void identifyDevice() {
  // MACアドレスを取得
  String macAddress = WiFi.macAddress();
  macAddress.toLowerCase();
  
  Serial.print("デバイスMACアドレス: ");
  Serial.println(macAddress);
  
  // デバイス配列から該当するデバイスを検索
  for (int i = 0; i < 4; i++) {
    if (devices[i].macAddress == macAddress) {
      currentDevice = devices[i];
      deviceIdentified = true;
      Serial.print("デバイス識別完了: ");
      Serial.print(currentDevice.deviceName);
      Serial.print(" (番号: ");
      Serial.print(currentDevice.deviceNumber);
      Serial.println(")");
      
      // デバイス固有のオフセット値を表示
      Serial.print("オフセット値: ");
      Serial.print(currentDevice.offsetCalibration);
      Serial.println("mm");
      return;
    }
  }
  
  // 未登録のデバイスの場合はデフォルト値を使用
  Serial.println("警告: 未登録のデバイスです。デフォルト設定を使用します。");
  currentDevice = {"unknown", 0, "未登録デバイス", 0};
  deviceIdentified = false;
}

// LEDの強度を設定する関数
void setLEDIntensity(int redIntensity, int blueIntensity) {
  analogWrite(RED_LED_PIN, redIntensity);   // 0-255の範囲
  analogWrite(BLUE_LED_PIN, blueIntensity); // 0-255の範囲
}

// BLE初期化
void initBLE() {
  String deviceName = "YonkuCounter_" + String(currentDevice.deviceNumber);
  
  BLEDevice::init(deviceName.c_str());
  
  // BLE送信パワーを最大に設定（接続安定性向上）
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9); // 出力パワーを最大(+9dBm)に設定
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);     // アドバタイジングの出力も最大に
  
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
  pAdvertising->setMinPreferred(0x0);  // iOS接続性向上（旧プロジェクト設定）
  BLEDevice::startAdvertising();
  
  Serial.println("BLE初期化完了 - " + deviceName);
  Serial.println("アドバタイジング開始 - 接続を待機中...");
  Serial.print("サービスUUID: ");
  Serial.println(SERVICE_UUID);
  Serial.print("キャラクタリスティックUUID: ");
  Serial.println(CHARACTERISTIC_UUID);
}

// 基準距離キャリブレーション機能
void calibrateBaseline() {
  if (!sensorAvailable) {
    Serial.println("センサーが利用できません。基準距離設定をスキップします。");
    return;
  }
  
  Serial.println("=== 基準距離キャリブレーション開始 ===");
  Serial.println("レーンに何も置かない状態で基準距離を測定します...");
  
  // 複数回測定して平均値を取得
  int measurements = 20;
  int totalRange = 0;
  int validMeasurements = 0;
  
  Serial.println("測定開始...");
  for (int i = 0; i < measurements; i++) {
    uint8_t range = vl.readRange();
    uint8_t status = vl.readRangeStatus();
    
    if (status == VL6180X_ERROR_NONE) {
      totalRange += range;
      validMeasurements++;
      Serial.print("測定 ");
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(range);
      Serial.println("mm");
    } else {
      Serial.print("測定 ");
      Serial.print(i + 1);
      Serial.println(": エラー");
    }
    delay(100);
  }
  
  if (validMeasurements >= measurements / 2) { // 半分以上の測定が成功した場合
    baselineDistance = totalRange / validMeasurements;
    baselineCalibrated = true;
    
    Serial.print("基準距離設定完了: ");
    Serial.print(baselineDistance);
    Serial.println("mm");
    Serial.print("検出閾値: ");
    Serial.print(baselineDistance - DETECTION_THRESHOLD); // 基準より閾値分小さい値で検出
    Serial.println("mm以下");
  } else {
    Serial.println("基準距離の設定に失敗しました。固定値を使用します。");
    baselineDistance = 200; // デフォルト値
    baselineCalibrated = false;
  }
  
  Serial.println("=== 基準距離キャリブレーション終了 ===");
}

void setup() {
  Serial.begin(115200);
  delay(1000); // シリアル通信の安定化待ち
  Serial.flush(); // バッファをクリア
  Serial.println("Yonku Counter Receiver (個別センサー) プログラム開始");
  
  // WiFiを初期化してMACアドレスを取得（接続はしない）
  WiFi.mode(WIFI_STA);
  delay(100);
  
  // デバイス識別を実行
  identifyDevice();
  
  // BLE初期化
  initBLE();
  
  // I2C通信の初期化（明示的に設定）
  Wire.begin();
  Wire.setClock(100000); // I2Cクロックを100kHzに設定（安定性向上）
  
  // LEDピンを出力モードに設定（センサー初期化前に設定）
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);
  
  // 初期状態では両方のLEDを消灯
  setLEDIntensity(0, 0);
  
  // 起動状態を示すLED点滅
  for(int i = 0; i < 3; i++) {
    setLEDIntensity(0, 100);
    delay(200);
    setLEDIntensity(0, 0);
    delay(200);
  }
  
  // VL6180Xセンサーの初期化（タイムアウト付き）
  Serial.println("VL6180Xセンサー初期化開始...");
  
  int retryCount = 0;
  const int maxRetries = 10;
  bool sensorInitialized = false;
  
  while (retryCount < maxRetries && !sensorInitialized) {
    if (vl.begin()) {
      sensorInitialized = true;
      sensorAvailable = true; // センサー初期化成功
      Serial.println("VL6180Xセンサー初期化完了");
    } else {
      retryCount++;
      Serial.print("VL6180X初期化失敗 (試行 ");
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
        Serial.println("2秒後に再試行...");
        delay(2000);
      }
    }
  }
  
  if (!sensorInitialized) {
    Serial.println("VL6180Xセンサーの初期化に失敗しました。");
    Serial.println("センサーなしモードで動作します。");
    // センサーなしでも動作継続（無限ループを回避）
  } else {
    // デバイス固有のオフセットを適用
    if (currentDevice.offsetCalibration != 0) {
      vl.setOffset(currentDevice.offsetCalibration);
      Serial.print("オフセット適用: ");
      Serial.print(currentDevice.offsetCalibration);
      Serial.println("mm");
    }
    
    // 単発測定モードで安定性向上
    Serial.println("単発測定モード開始");
    Serial.println("基準距離キャリブレーションを実行します...");
    
    // 基準距離キャリブレーション実行
    delay(2000); // センサー安定化待ち
    calibrateBaseline();
    
    Serial.println("キャリブレーションを再実行する場合は 'c' を送信してください");
  }
  
  // 初期化完了を示すLED点滅
  for(int i = 0; i < 2; i++) {
    setLEDIntensity(0, 255);
    delay(300);
    setLEDIntensity(0, 0);
    delay(300);
  }
  setLEDIntensity(0, 100); // 青色通常点灯で開始
  
  Serial.println("レシーバーセットアップ完了");
}

void loop() {
  // BLE接続状態の管理
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // BLEスタックに準備時間を与える
    pServer->startAdvertising(); // 再度アドバタイジングを開始
    Serial.println("アドバタイジング再開");
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  // キャリブレーションコマンドチェック
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
    // VL6180Xセンサーから距離を読み取り（単発測定モード使用）
    uint8_t range = vl.readRange();
    uint8_t status = vl.readRangeStatus();
  
    // 測定エラーをチェック
    if (status == VL6180X_ERROR_NONE) {
      // 距離データの出力頻度を制限（USB負荷軽減）
      static unsigned long lastPrintTime = 0;
      if (millis() - lastPrintTime > 1000) { // 1秒ごとに距離を出力
        Serial.print("[");
        Serial.print(currentDevice.deviceName);
        Serial.print("] 距離: ");
        Serial.print(range);
        Serial.print("mm, カウント: ");
        Serial.println(deviceCount);
        lastPrintTime = millis();
      }
      
      // ミニ四駆通過判定（基準距離より閾値分以上小さい場合）
      if (baselineCalibrated && range < (baselineDistance - DETECTION_THRESHOLD)) {
        // 重複カウント防止チェック
        unsigned long currentTime = millis();
        static bool canCountUpMessageShown = false; // カウントアップ可能メッセージの表示フラグ
        
        // 待機時間経過の判定
        bool canCountUp = (currentTime - lastCountTime > COUNT_IGNORE_DURATION);
        
        // カウントアップ可能タイミングのメッセージ（待機時間終了時に一度だけ表示）
        if (canCountUp && !canCountUpMessageShown) {
          Serial.println("=== カウントアップ可能タイミング到達 ===");
          Serial.print("前回カウントから ");
          Serial.print(currentTime - lastCountTime);
          Serial.println("ms経過");
          canCountUpMessageShown = true;
        }
        
        if (canCountUp) {
          deviceCount++;
          
          Serial.print("*** ミニ四駆通過検出！ ***");
          Serial.print(" 距離: ");
          Serial.print(range);
          Serial.print("mm (基準: ");
          Serial.print(baselineDistance);
          Serial.print("mm) カウント: ");
          Serial.println(deviceCount);
          
          // カウントアップ時のLED制御開始
          countUpLEDActive = true;
          countUpLEDStartTime = currentTime;
          setLEDIntensity(0, 0); // 青色を一瞬消す
          
          // カウントアップ可能メッセージフラグをリセット（次回のカウントアップ用）
          canCountUpMessageShown = false;
        } else {
          // カウントアップできない場合のログ
          Serial.print("[カウント待機中] 距離: ");
          Serial.print(range);
          Serial.print("mm, 待機残り時間: ");
          Serial.print(COUNT_IGNORE_DURATION - (currentTime - lastCountTime));
          Serial.println("ms");
        }
        
        // 基準点より小さい値が続く限り、lastCountTimeを更新して待機時間を延長
        lastCountTime = currentTime;
        
        // LED制御：オブジェクト検出時は赤色点灯（カウントアップ中でない場合）
        if (!countUpLEDActive) {
          setLEDIntensity(255, 0); // 赤色点灯（検出中）
        }
      } else {
        // 通過していない場合は通常の青色点灯
        if (!countUpLEDActive) {
          setLEDIntensity(0, 100); // 青色通常点灯
        }
      }
    } else {
      // エラー時は赤色LEDを点滅
      static unsigned long lastErrorFlashTime = 0;
      static bool errorFlashState = false;
      if (millis() - lastErrorFlashTime > 250) { // 250msごとに点滅
        errorFlashState = !errorFlashState;
        setLEDIntensity(errorFlashState ? 255 : 0, 0);
        lastErrorFlashTime = millis();
      }
    }
    
    // カウントアップ時のLED点滅制御
    if (countUpLEDActive) {
      unsigned long currentTime = millis();
      unsigned long elapsedTime = currentTime - countUpLEDStartTime;
      
      if (elapsedTime < COUNT_UP_LED_DURATION) {
        // 3秒間点滅（250msごとに点滅）
        static unsigned long lastBlinkTime = 0;
        static bool blinkState = false;
        
        if (currentTime - lastBlinkTime > 250) {
          blinkState = !blinkState;
          setLEDIntensity(0, blinkState ? 255 : 0); // 青色点滅
          lastBlinkTime = currentTime;
        }
      } else {
        // 3秒経過後、カウントアップLED制御終了
        countUpLEDActive = false;
        setLEDIntensity(0, 100); // 青色通常点灯に戻る
      }
    }
    
    // BLEで常時カウントデータを送信（取りこぼし防止）
    static unsigned long lastBLESendTime = 0;
    if (deviceConnected && pCharacteristic && millis() - lastBLESendTime > 100) { // 100msごとに送信
      String countData = String(currentDevice.deviceNumber) + ":" + String(deviceCount);
      pCharacteristic->setValue(countData.c_str());
      pCharacteristic->notify();
      
      // 通信時の青色点灯（カウントアップ中でない場合のみ）
      if (!countUpLEDActive) {
        static unsigned long commLEDStartTime = 0;
        static bool commLEDActive = false;
        
        if (!commLEDActive) {
          setLEDIntensity(0, 255); // 青色点灯
          commLEDStartTime = millis();
          commLEDActive = true;
        } else if (millis() - commLEDStartTime > 50) { // 50ms点灯
          setLEDIntensity(0, 100); // 青色通常点灯に戻る
          commLEDActive = false;
        }
      }
      
      lastBLESendTime = millis();
    }
  } else {
    // センサーなしモード：青色で待機状態を表示
    static unsigned long lastPatternTime = 0;
    static bool patternState = false;
    
    if (millis() - lastPatternTime > 1000) { // 1秒ごとにパターン変更
      patternState = !patternState;
      setLEDIntensity(0, patternState ? 200 : 50); // 青色で明滅
      lastPatternTime = millis();
    }
  }
  
  // シリアルバッファを定期的にフラッシュ（USB安定性向上）
  static unsigned long lastFlushTime = 0;
  if (millis() - lastFlushTime > 1000) { // 1秒ごと
    Serial.flush();
    lastFlushTime = millis();
  }
  
  delay(20);
}
