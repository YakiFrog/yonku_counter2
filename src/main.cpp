#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_VL6180X.h"

// LEDピンの定義（PWM対応ピン）
#define RED_LED_PIN D0    // 赤色LED（PWM対応）
#define BLUE_LED_PIN D1   // 青色LED（PWM対応）

// グローバル変数でセンサー状態を追跡
bool sensorAvailable = false;

// VL6180Xセンサーのインスタンス
Adafruit_VL6180X vl = Adafruit_VL6180X();

// LEDの強度を設定する関数
void setLEDIntensity(int redIntensity, int blueIntensity) {
  analogWrite(RED_LED_PIN, redIntensity);   // 0-255の範囲
  analogWrite(BLUE_LED_PIN, blueIntensity); // 0-255の範囲
}

void setup() {
  Serial.begin(115200);
  delay(1000); // シリアル通信の安定化待ち
  Serial.flush(); // バッファをクリア
  Serial.println("LED制御 + VL6180X ToFセンサー プログラム開始");
  
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
    // 単発測定モードで安定性向上
    Serial.println("単発測定モード開始");
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
      Serial.print("距離: ");
      Serial.print(range);
      Serial.println(" mm");
      lastPrintTime = millis();
    }
    
    // 距離に応じてLEDの強度を変更（顕著な色の差）
    if (range < 25) {
      // 25mm未満: 赤色LED最大強度＋点滅効果（緊急危険）
      static unsigned long lastFlashTime = 0;
      static bool flashState = false;
      if (millis() - lastFlashTime > 100) { // 100msごとに点滅
        flashState = !flashState;
        setLEDIntensity(flashState ? 255 : 200, 0);
        lastFlashTime = millis();
      }
    } else if (range < 40) {
      // 25-40mm: 赤色LED最大強度（非常に危険）
      setLEDIntensity(255, 0);
    } else if (range < 60) {
      // 40-60mm: 赤色LED中強度（警告）
      setLEDIntensity(180, 0);
    } else if (range < 90) {
      // 60-90mm: オレンジ色（赤＋青の混合）で注意
      setLEDIntensity(200, 30);
    } else if (range < 130) {
      // 90-130mm: 青色LED中強度（検出）
      setLEDIntensity(0, 200);
    } else if (range < 180) {
      // 130-180mm: 青色LED弱（遠距離検出）
      setLEDIntensity(0, 100);
    } else if (range < 250) {
      // 180-250mm: 青色LED非常に弱（微検出）
      setLEDIntensity(0, 30);
    } else {
      // 250mm以上: 両方のLEDを消灯（安全距離）
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
    // センサーなしモード：LEDテストパターン表示
    static unsigned long lastPatternTime = 0;
    static int patternStep = 0;
    
    if (millis() - lastPatternTime > 1000) { // 1秒ごとにパターン変更
      switch (patternStep % 4) {
        case 0: setLEDIntensity(50, 0); break;   // 赤弱
        case 1: setLEDIntensity(0, 50); break;   // 青弱  
        case 2: setLEDIntensity(25, 25); break; // 両方弱
        case 3: setLEDIntensity(0, 0); break;   // 消灯
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
  
  // 30Hzで動作（約33.3ms）
  delay(33); // 33ms
}