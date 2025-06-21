#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// BLEの設定
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// LED設定
#define LED_PIN 21

// 単一デバイス接続用変数
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
bool deviceConnected = false;
bool doConnect = false;
BLEAddress* pServerAddress = nullptr;

// BLEクライアント接続状態管理用コールバッククラス
class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
        Serial.println("*** Client connected successfully ***");
        deviceConnected = true;
    }

    void onDisconnect(BLEClient* pclient) {
        Serial.println("*** Client disconnected ***");
        deviceConnected = false;
    }
};

// 通知コールバック
static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    
    String receivedData = "";
    for (int i = 0; i < length; i++) {
        receivedData += (char)pData[i];
    }
    
    Serial.print("Received: ");
    Serial.println(receivedData);
    
    // LED点灯
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
}

// BLEサーバーへの接続（元のコードに基づく単純版）
bool connectToServer() {
    Serial.print("接続先: ");
    Serial.println(pServerAddress->toString().c_str());
    Serial.flush();
    
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());
    
    // サーバーに接続
    if (!pClient->connect(*pServerAddress)) {
        Serial.println("ERROR: Failed to connect to server");
        Serial.flush();
        return false;
    }
    Serial.println("✓ Connected to server");
    Serial.flush();

    // サービスの取得
    BLERemoteService* pRemoteService = pClient->getService(BLEUUID(SERVICE_UUID));
    if (pRemoteService == nullptr) {
        Serial.println("ERROR: Service not found");
        Serial.flush();
        pClient->disconnect();
        return false;
    }
    Serial.println("✓ Service found");
    Serial.flush();

    // キャラクタリスティックの取得
    pRemoteCharacteristic = pRemoteService->getCharacteristic(BLEUUID(CHARACTERISTIC_UUID));
    if (pRemoteCharacteristic == nullptr) {
        Serial.println("ERROR: Characteristic not found");
        Serial.flush();
        pClient->disconnect();
        return false;
    }
    Serial.println("✓ Characteristic found");
    Serial.flush();

    // 通知の登録
    if(pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
        Serial.println("✓ Notifications enabled");
        Serial.flush();
    } else {
        Serial.println("WARNING: Device does not support notifications");
        Serial.flush();
    }
    
    deviceConnected = true;
    Serial.println("=== CONNECTION SUCCESSFUL ===");
    Serial.flush();
    return true;
}

// BLEスキャンコールバッククラス（元のコードに基づく）
class SingleDeviceCallback: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        // デバイス名の取得（空の場合の処理）
        String deviceName = advertisedDevice.getName().c_str();
        if (deviceName.length() == 0) {
            deviceName = "Unknown";
        }
        
        Serial.printf("Found: %s (%s) RSSI: %d\n", 
                     deviceName.c_str(),
                     advertisedDevice.getAddress().toString().c_str(),
                     advertisedDevice.getRSSI());
        Serial.flush();
        
        // YonkuCounterデバイスを見つけたら接続準備
        if (advertisedDevice.haveName() && 
            deviceName.startsWith("YonkuCounter_")) {
            
            Serial.println(">>> Target YonkuCounter device found! <<<");
            Serial.flush();
            
            BLEDevice::getScan()->stop();
            pServerAddress = new BLEAddress(advertisedDevice.getAddress());
            doConnect = true;
        }
    }
};

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("=== Single Device Connection Test ===");
    Serial.println("This test connects to only ONE YonkuCounter device");
    
    // LED初期化
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    // 起動表示
    for(int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(200);
        digitalWrite(LED_PIN, LOW);
        delay(200);
    }
    
    // BLE初期化
    Serial.println("Initializing BLE...");
    BLEDevice::init("SingleTestDevice");
    
    // 送信パワー設定
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    delay(1000);
    Serial.println("BLE initialized");
    
    // スキャン設定
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new SingleDeviceCallback());
    pBLEScan->setInterval(1349);
    pBLEScan->setWindow(449);
    pBLEScan->setActiveScan(true);
    
    Serial.println("Starting initial scan for YonkuCounter devices...");
    Serial.flush();
    pBLEScan->start(5, false); // 5秒間スキャン（無制限ではなく）
}

void loop() {
    // 接続が必要な場合
    if (doConnect) {
        if (connectToServer()) {
            Serial.println("*** Connection successful! ***");
            Serial.flush();
        } else {
            Serial.println("*** Connection failed! ***");
            Serial.flush();
            deviceConnected = false;
        }
        doConnect = false;
    }

    // 接続状態監視
    if (deviceConnected && pClient && !pClient->isConnected()) {
        Serial.println("Connection lost! Restarting scan...");
        Serial.flush();
        deviceConnected = false;
        if (pClient) {
            delete pClient;
            pClient = nullptr;
        }
        pRemoteCharacteristic = nullptr;
        
        delay(2000);
        Serial.println("Starting scan again...");
        Serial.flush();
        BLEDevice::getScan()->start(5, false); // 5秒間スキャン
    }

    // 未接続状態での再スキャン制御
    static unsigned long lastScanTime = 0;
    const long scanInterval = 5000;  // スキャン間隔（5秒）
    
    if (!deviceConnected && !doConnect && (millis() - lastScanTime >= scanInterval)) {
        lastScanTime = millis();
        Serial.println("Scanning for devices...");
        Serial.flush();
        BLEDevice::getScan()->start(5, false);  // 5秒間スキャン
    }
    
    // ステータス表示
    static unsigned long lastStatus = 0;
    if (millis() - lastStatus > 10000) {
        Serial.print("Status: ");
        Serial.println(deviceConnected ? "Connected" : "Scanning...");
        Serial.flush();
        lastStatus = millis();
    }
    
    delay(100);
}
