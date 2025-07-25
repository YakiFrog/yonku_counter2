#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// BLEの設定
#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  // Nordic UART Service UUID
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // TX Characteristic UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // RX Characteristic UUID

// LED設定
#define LED_PIN 21  // 内蔵LED

// UART設定（transmitterからのデータ受信）
#define UART_RX_PIN 44  // UART受信ピン（transmitterのTXと接続）
#define UART_TX_PIN 43  // UART送信ピン（使用しないが定義）
#define UART_BAUD_RATE 115200

// BLEサーバー関連の変数
BLEServer* pServer = nullptr;
BLECharacteristic* pTxCharacteristic = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// データ管理用変数
String currentData = "";        // 現在のデータ
unsigned long dataTimestamp = 0; // データのタイムスタンプ

// LED制御用変数
unsigned long ledStartTime = 0;
bool ledOn = false;
const int LED_DURATION = 100;  // LED点灯時間（ms）

// BLEサーバーコールバッククラス
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("Client connected");
        Serial.flush();
        
        // 接続時にLED点灯
        digitalWrite(LED_PIN, HIGH);
        ledOn = true;
        ledStartTime = millis();
    };

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Client disconnected");
        Serial.flush();
        
        // 切断時にLED点滅
        for(int i = 0; i < 3; i++) {
            digitalWrite(LED_PIN, HIGH);
            delay(100);
            digitalWrite(LED_PIN, LOW);
            delay(100);
        }
    }
};

// BLE受信コールバッククラス（必要に応じて拡張可能）
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string rxValue = pCharacteristic->getValue();
        
        if (rxValue.length() > 0) {
            Serial.print("Received from client: ");
            for (int i = 0; i < rxValue.length(); i++) {
                Serial.print(rxValue[i]);
            }
            Serial.println();
            Serial.flush();
        }
    }
};

void setup() {
    Serial.begin(115200);
    delay(2000); // 安定化のための待機時間
    Serial.println("Tanaka Gate Server Starting...");
    Serial.flush();
    
    // UART初期化（transmitterからのデータ受信用）
    Serial2.begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    
    // LED初期化
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    // 起動LED表示
    for(int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(200);
        digitalWrite(LED_PIN, LOW);
        delay(200);
    }
    
    // BLE初期化
    Serial.println("Initializing BLE...");
    Serial.flush();
    BLEDevice::init("TanakaGateServer");
    
    // BLE送信パワーを最大に設定
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
    
    // BLEサーバー作成
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    
    // BLEサービス作成
    BLEService *pService = pServer->createService(SERVICE_UUID);
    
    // TX Characteristic作成（サーバーからクライアントへのデータ送信用）
    pTxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_TX,
                        BLECharacteristic::PROPERTY_NOTIFY
                    );
    pTxCharacteristic->addDescriptor(new BLE2902());
    
    // RX Characteristic作成（クライアントからサーバーへのデータ受信用）
    BLECharacteristic* pRxCharacteristic = pService->createCharacteristic(
                            CHARACTERISTIC_UUID_RX,
                            BLECharacteristic::PROPERTY_WRITE
                        );
    pRxCharacteristic->setCallbacks(new MyCallbacks());
    
    // サービス開始
    pService->start();
    
    // アドバタイジング開始
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(false);
    pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
    BLEDevice::startAdvertising();
    
    Serial.println("BLE server started");
    Serial.println("Waiting for client connection...");
    Serial.println("Waiting for UART data from transmitter...");
    Serial.println("=========================================");
    Serial.flush();
}

void loop() {
    // LED制御（一定時間後に消灯）
    if (ledOn && (millis() - ledStartTime) > LED_DURATION) {
        digitalWrite(LED_PIN, LOW);
        ledOn = false;
    }
    
    // UARTからのデータ受信チェック
    if (Serial2.available()) {
        String receivedData = Serial2.readStringUntil('\n');
        receivedData.trim(); // 改行文字等を除去
        
        if (receivedData.length() > 0) {
            Serial.print("Received from transmitter: ");
            Serial.println(receivedData);
            Serial.flush();
            
            // タイムスタンプ付きでデータを更新
            currentData = String(millis()) + ":" + receivedData;
            dataTimestamp = millis();
            
            // BLEクライアントに送信
            if (deviceConnected && pTxCharacteristic) {
                // タイムスタンプ付きデータをBLE経由で送信（一意性を保証）
                pTxCharacteristic->setValue(currentData.c_str());
                pTxCharacteristic->notify();
                
                Serial.print("Sent to client: ");
                Serial.println(currentData);
                Serial.flush();
                
                // データ送信時にLED点灯
                digitalWrite(LED_PIN, HIGH);
                ledOn = true;
                ledStartTime = millis();
            } else {
                Serial.println("No client connected - data not sent");
                Serial.flush();
            }
        }
    }
    
    // 接続状態の変化をチェック
    if (!deviceConnected && oldDeviceConnected) {
        delay(500); // give the bluetooth stack the chance to get things ready
        pServer->startAdvertising(); // restart advertising
        Serial.println("Start advertising");
        Serial.flush();
        oldDeviceConnected = deviceConnected;
    }
    
    if (deviceConnected && !oldDeviceConnected) {
        // do stuff here on connecting
        oldDeviceConnected = deviceConnected;
    }
    
    delay(10); // 短い遅延でCPU使用率を下げる
}
