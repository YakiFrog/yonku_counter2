#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include "Adafruit_VL6180X.h"

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

// グローバル変数
bool sensorAvailable = false;
DeviceCalibration currentDevice;
bool deviceIdentified = false;

// VL6180Xセンサーのインスタンス
Adafruit_VL6180X vl = Adafruit_VL6180X();

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

// オフセットキャリブレーション機能
void calibrateOffset() {
  if (!sensorAvailable) {
    Serial.println("センサーが利用できません。");
    return;
  }
  
  Serial.println("=== オフセットキャリブレーション開始 ===");
  Serial.println("センサーから10mmの位置に基準物体を設置してください。");
  Serial.println("準備ができたら任意のキーを押してください...");
  
  // シリアル入力待ち
  while (!Serial.available()) {
    delay(100);
  }
  Serial.read(); // バッファをクリア
  
  // 複数回測定して平均値を取得
  int measurements = 10;
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
    delay(200);
  }
  
  if (validMeasurements > 0) {
    int averageRange = totalRange / validMeasurements;
    int offset = averageRange - 10; // 10mmが基準
    
    Serial.print("平均測定値: ");
    Serial.print(averageRange);
    Serial.println("mm");
    Serial.print("計算されたオフセット: ");
    Serial.print(offset);
    Serial.println("mm");
    
    // オフセットを設定
    vl.setOffset(offset);
    
    // 検証測定
    delay(500);
    uint8_t verifyRange = vl.readRange();
    Serial.print("検証測定: ");
    Serial.print(verifyRange);
    Serial.println("mm");
    
    Serial.println("オフセットキャリブレーション完了");
    Serial.print("この値をコードに保存してください: ");
    Serial.println(offset);
  } else {
    Serial.println("有効な測定値が取得できませんでした。");
  }
  
  Serial.println("=== キャリブレーション終了 ===");
}

void setup() {
  Serial.begin(115200);
  delay(1000); // シリアル通信の安定化待ち
  Serial.flush(); // バッファをクリア
  Serial.println("LED制御 + VL6180X ToFセンサー プログラム開始");
  
  // WiFiを初期化してMACアドレスを取得（接続はしない）
  WiFi.mode(WIFI_STA);
  delay(100);
  
  // デバイス識別を実行
  identifyDevice();
  
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
    Serial.println("キャリブレーションを実行する場合は 'c' を送信してください");
  }
  
  // 初期化完了を示すLED点滅
  for(int i = 0; i < 2; i++) {
    setLEDIntensity(0, 255);
    delay(300);
    setLEDIntensity(255, 0);
    delay(300);
  }
  setLEDIntensity(0, 0);
  
  Serial.println("セットアップ完了");
}

void loop() {
  // キャリブレーションコマンドチェック
  if (Serial.available() > 0) {
    char command = Serial.read();
    if (command == 'c' || command == 'C') {
      calibrateOffset();
      // バッファをクリア
      while (Serial.available()) {
        Serial.read();
      }
    }
  }
  
  // 生存確認用のハートビート（デバッグ用）- 一時的に無効化
  // static unsigned long lastHeartbeat = 0;
  // if (millis() - lastHeartbeat > 5000) { // 5秒ごとに変更（負荷軽減）
  //   Serial.println("ハートビート - ループ実行中");
  //   lastHeartbeat = millis();
  // }
  
  // センサーが利用可能な場合のみセンサー読み取りを実行
  if (sensorAvailable) {
    // VL6180Xセンサーから距離を読み取り（単発測定モード使用）
    uint8_t range = vl.readRange();
    uint8_t status = vl.readRangeStatus();
  
  // 測定エラーをチェック
  if (status == VL6180X_ERROR_NONE) {
    // 距離データの出力頻度を制限（USB負荷軽減）
    static unsigned long lastPrintTime = 0;
    if (millis() - lastPrintTime > 500) { // 500msごと（2Hz）に距離を出力
      Serial.print("[");
      Serial.print(currentDevice.deviceName);
      Serial.print("] 距離: ");
      Serial.print(range);
      Serial.println(" mm");
      lastPrintTime = millis();
    }
    
    // 距離判定（固定閾値を使用）
    if (range < 25) {
      // 緊急距離未満: 赤色LED最大強度＋点滅効果（緊急危険）
      static unsigned long lastFlashTime = 0;
      static bool flashState = false;
      if (millis() - lastFlashTime > 100) { // 100msごとに点滅
        flashState = !flashState;
        setLEDIntensity(flashState ? 255 : 200, 0);
        lastFlashTime = millis();
      }
    } else if (range < 40) {
      // 危険距離: 赤色LED最大強度（非常に危険）
      setLEDIntensity(255, 0);
    } else if (range < 60) {
      // 警告距離: 赤色LED中強度（警告）
      setLEDIntensity(180, 0);
    } else if (range < 90) {
      // 注意距離: オレンジ色（赤＋青の混合）で注意
      setLEDIntensity(200, 30);
    } else if (range < 130) {
      // 検出距離: 青色LED中強度（検出）
      setLEDIntensity(0, 200);
    } else if (range < 180) {
      // 遠距離: 青色LED弱（遠距離検出）
      setLEDIntensity(0, 100);
    } else if (range < 250) {
      // 微検出距離: 青色LED非常に弱（微検出）
      setLEDIntensity(0, 30);
    } else {
      // 安全距離以上: 両方のLEDを消灯（安全距離）
      setLEDIntensity(0, 0);
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
  } else {
    // センサーなしモード：デバイス番号に応じたLEDテストパターン表示
    static unsigned long lastPatternTime = 0;
    static int patternStep = 0;
    
    if (millis() - lastPatternTime > 1000) { // 1秒ごとにパターン変更
      // デバイス番号に応じて異なるパターンを表示
      switch (currentDevice.deviceNumber) {
        case 1: // デバイス1: 赤色パターン
          switch (patternStep % 4) {
            case 0: setLEDIntensity(100, 0); break;  // 赤中
            case 1: setLEDIntensity(50, 0); break;   // 赤弱
            case 2: setLEDIntensity(200, 0); break;  // 赤強
            case 3: setLEDIntensity(0, 0); break;    // 消灯
          }
          break;
        case 2: // デバイス2: 青色パターン
          switch (patternStep % 4) {
            case 0: setLEDIntensity(0, 100); break;  // 青中
            case 1: setLEDIntensity(0, 50); break;   // 青弱
            case 2: setLEDIntensity(0, 200); break;  // 青強
            case 3: setLEDIntensity(0, 0); break;    // 消灯
          }
          break;
        case 3: // デバイス3: 交互パターン
          switch (patternStep % 4) {
            case 0: setLEDIntensity(100, 0); break;  // 赤
            case 1: setLEDIntensity(0, 100); break;  // 青
            case 2: setLEDIntensity(100, 0); break;  // 赤
            case 3: setLEDIntensity(0, 0); break;    // 消灯
          }
          break;
        case 4: // デバイス4: 混合パターン
          switch (patternStep % 4) {
            case 0: setLEDIntensity(100, 100); break; // 紫
            case 1: setLEDIntensity(50, 50); break;   // 紫弱
            case 2: setLEDIntensity(150, 30); break;  // オレンジ
            case 3: setLEDIntensity(0, 0); break;     // 消灯
          }
          break;
        default: // 未登録デバイス: デフォルトパターン
          switch (patternStep % 4) {
            case 0: setLEDIntensity(50, 0); break;   // 赤弱
            case 1: setLEDIntensity(0, 50); break;   // 青弱  
            case 2: setLEDIntensity(25, 25); break; // 両方弱
            case 3: setLEDIntensity(0, 0); break;   // 消灯
          }
          break;
      }
      patternStep++;
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