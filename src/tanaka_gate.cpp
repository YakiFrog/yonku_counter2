#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// BLEの設定
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// UART設定
#define UART_RX_PIN 44  // UART受信ピン
#define UART_TX_PIN 43  // UART送信ピン
#define UART_BAUD_RATE 115200

// LED設定
#define LED_PIN 21  // 内蔵LED

// グローバル変数
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
unsigned long previousMillis = 0;
const long interval = 2000;  // メッセージ送信間隔 (ms)
int messageCount = 0;

// UART受信用バッファ
const int BUFFER_SIZE = 128;
char uartBuffer[BUFFER_SIZE];
int bufferIndex = 0;

// LED制御用変数
unsigned long ledStartTime = 0;
bool ledOn = false;
const int LED_DURATION = 100;  // LED点灯時間（ms）

// BLEからの受信を処理するコールバッククラス
class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    
    if (value.length() == 1) {  // 1文字のみ許可
      char c = value[0];
      Serial.println(c);
      
      // UARTにBLEから受信したデータを送信
      Serial2.println(c);
    }
  }
};

// BLEの接続/切断を処理するコールバッククラス
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    // Serial.println("デバイスが接続されました");
  }
  
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    // Serial.println("デバイスが切断されました");
  }
};

void setup() {
  // シリアル通信の初期化（デバッグ用）
  // Serial.begin(115200);
  // Serial.println("BLEサーバーを開始します...");
  
  // LED初期化
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // UART通信の初期化（外部デバイスとの通信用）
  Serial2.begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  
  // BLEデバイスの初期化
  BLEDevice::init("TanakaGate");  // デバイス名を設定
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9); // 出力パワーを最大(+9dBm)に設定
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);     // アドバタイジングの出力も最大に

  // BLEサーバーの作成
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  // BLEサービスの作成
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  // BLEキャラクタリスティックの作成
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  
  // BLE2902ディスクリプタを追加（通知機能を有効化）
  pCharacteristic->addDescriptor(new BLE2902());
  
  // BLEキャラクタリスティックのコールバックを設定
  pCharacteristic->setCallbacks(new MyCallbacks());
  
  // サービスを開始
  pService->start();
  
  // アドバタイジングを開始
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // iOSとの接続性を向上
  BLEDevice::startAdvertising();
  Serial.println("BLEサーバーが起動しました。接続を待機しています...");
}

void loop() {
  unsigned long currentMillis = millis();
  
  // UART経由でのデータ受信処理
//   while (Serial2.available()) {
//     char c = Serial2.read();
    
//     // 改行またはキャリッジリターンを受信した場合、または最大バッファサイズに達した場合
//     if (c == '\n' || c == '\r' || bufferIndex >= BUFFER_SIZE - 1) {
//       if (bufferIndex > 0) {
//         uartBuffer[bufferIndex] = '\0'; // 文字列の終端
//         Serial.print("UARTから受信: ");
//         Serial.println(uartBuffer);
        
//         // BLE経由でデータを送信（接続中の場合のみ）
//         if (deviceConnected) {
//           // LED点灯開始
//           digitalWrite(LED_PIN, HIGH);
//           ledOn = true;
//           ledStartTime = currentMillis;
          
//           pCharacteristic->setValue(uartBuffer);
//           pCharacteristic->notify();
//           Serial.println("BLEへ送信しました");
//           // もう一度
//           delay(100); // 100ms待機
//           pCharacteristic->setValue(uartBuffer);
//           pCharacteristic->notify();
//         }
        
//         // バッファをリセット
//         bufferIndex = 0;
//       }
//     } else {
//       // バッファに文字を追加
//       uartBuffer[bufferIndex++] = c;
//     }
//   }

  // 接続状態が変化した場合の処理
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // Bluetoothスタックの準備時間
    pServer->startAdvertising(); // 再度アドバタイジングを開始
    // Serial.println("アドバタイジングを再開します");
    oldDeviceConnected = deviceConnected;
  }
  
  // 新しい接続があった場合の処理
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
    // Serial.println("新しい接続を検出しました");
  }
  
  // LED制御の更新
  if (ledOn && (currentMillis - ledStartTime >= LED_DURATION)) {
    digitalWrite(LED_PIN, LOW);
    ledOn = false;
  }
}