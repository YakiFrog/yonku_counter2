#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// BLEの設定
#define SERVICE_UUID_TRANSMITTER        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID_TRANSMITTER "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define SERVICE_UUID_TANAKA_GATE        "e1e2e3e4-1111-2222-3333-444455556666"
#define CHARACTERISTIC_UUID_TANAKA_GATE "e1e2e3e4-aaaa-bbbb-cccc-ddddeeeeffff"

// LED設定
#define LED_PIN 21

// 単一デバイス接続用変数
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
bool deviceConnected = false;
bool doConnect = false;
BLEAddress* pServerAddress = nullptr;

// 2台分のBLEクライアント・キャラクタリスティック・アドレス・接続状態
BLEClient* pClientTransmitter = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristicTransmitter = nullptr;
BLEAddress* pServerAddressTransmitter = nullptr;
bool deviceConnectedTransmitter = false;
bool doConnectTransmitter = false;

BLEClient* pClientTanakaGate = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristicTanakaGate = nullptr;
BLEAddress* pServerAddressTanakaGate = nullptr;
bool deviceConnectedTanakaGate = false;
bool doConnectTanakaGate = false;

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

// BLEサーバーへの接続（transmitter用）
bool connectToTransmitter() {
    Serial.print("接続先 (Transmitter): ");
    Serial.println(pServerAddressTransmitter->toString().c_str());
    Serial.flush();
    
    pClientTransmitter = BLEDevice::createClient();
    pClientTransmitter->setClientCallbacks(new MyClientCallback());
    
    // サーバーに接続
    if (!pClientTransmitter->connect(*pServerAddressTransmitter)) {
        Serial.println("ERROR: Failed to connect to transmitter server");
        Serial.flush();
        return false;
    }
    Serial.println("✓ Connected to transmitter server");
    Serial.flush();

    // サービスの取得
    BLERemoteService* pRemoteService = pClientTransmitter->getService(BLEUUID(SERVICE_UUID_TRANSMITTER));
    if (pRemoteService == nullptr) {
        Serial.println("ERROR: Transmitter service not found");
        Serial.flush();
        pClientTransmitter->disconnect();
        return false;
    }
    Serial.println("✓ Transmitter service found");
    Serial.flush();

    // キャラクタリスティックの取得
    pRemoteCharacteristicTransmitter = pRemoteService->getCharacteristic(BLEUUID(CHARACTERISTIC_UUID_TRANSMITTER));
    if (pRemoteCharacteristicTransmitter == nullptr) {
        Serial.println("ERROR: Transmitter characteristic not found");
        Serial.flush();
        pClientTransmitter->disconnect();
        return false;
    }
    Serial.println("✓ Transmitter characteristic found");
    Serial.flush();

    // 通知の登録
    if(pRemoteCharacteristicTransmitter->canNotify()) {
        pRemoteCharacteristicTransmitter->registerForNotify(notifyCallback);
        Serial.println("✓ Transmitter notifications enabled");
        Serial.flush();
    } else {
        Serial.println("WARNING: Transmitter device does not support notifications");
        Serial.flush();
    }
    
    deviceConnectedTransmitter = true;
    Serial.println("=== TRANSMITTER CONNECTION SUCCESSFUL ===");
    Serial.flush();
    return true;
}

// BLEサーバーへの接続（tanaka_gate用）
bool connectToTanakaGate() {
    Serial.print("接続先 (Tanaka Gate): ");
    Serial.println(pServerAddressTanakaGate->toString().c_str());
    Serial.flush();
    
    pClientTanakaGate = BLEDevice::createClient();
    pClientTanakaGate->setClientCallbacks(new MyClientCallback());
    
    // サーバーに接続
    if (!pClientTanakaGate->connect(*pServerAddressTanakaGate)) {
        Serial.println("ERROR: Failed to connect to Tanaka Gate server");
        Serial.flush();
        return false;
    }
    Serial.println("✓ Connected to Tanaka Gate server");
    Serial.flush();

    // サービスの取得
    BLERemoteService* pRemoteService = pClientTanakaGate->getService(BLEUUID(SERVICE_UUID_TANAKA_GATE));
    if (pRemoteService == nullptr) {
        Serial.println("ERROR: Tanaka Gate service not found");
        Serial.flush();
        pClientTanakaGate->disconnect();
        return false;
    }
    Serial.println("✓ Tanaka Gate service found");
    Serial.flush();

    // キャラクタリスティックの取得
    pRemoteCharacteristicTanakaGate = pRemoteService->getCharacteristic(BLEUUID(CHARACTERISTIC_UUID_TANAKA_GATE));
    if (pRemoteCharacteristicTanakaGate == nullptr) {
        Serial.println("ERROR: Tanaka Gate characteristic not found");
        Serial.flush();
        pClientTanakaGate->disconnect();
        return false;
    }
    Serial.println("✓ Tanaka Gate characteristic found");
    Serial.flush();

    // 通知の登録
    if(pRemoteCharacteristicTanakaGate->canNotify()) {
        pRemoteCharacteristicTanakaGate->registerForNotify(notifyCallback);
        Serial.println("✓ Tanaka Gate notifications enabled");
        Serial.flush();
    } else {
        Serial.println("WARNING: Tanaka Gate device does not support notifications");
        Serial.flush();
    }
    
    deviceConnectedTanakaGate = true;
    Serial.println("=== TANAKA GATE CONNECTION SUCCESSFUL ===");
    Serial.flush();
    return true;
}

// BLEスキャンコールバッククラス（2台対応）
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
        
        // transmitterデバイスを見つけたら接続準備
        if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID_TRANSMITTER))) {
            if (!deviceConnectedTransmitter && !doConnectTransmitter) {
                Serial.println(">>> Target Transmitter device found! <<<");
                Serial.flush();
                
                BLEDevice::getScan()->stop();
                pServerAddressTransmitter = new BLEAddress(advertisedDevice.getAddress());
                doConnectTransmitter = true;
            }
        }
        
        // tanaka_gateデバイスを見つけたら接続準備
        if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID_TANAKA_GATE))) {
            if (!deviceConnectedTanakaGate && !doConnectTanakaGate) {
                Serial.println(">>> Target Tanaka Gate device found! <<<");
                Serial.flush();
                
                BLEDevice::getScan()->stop();
                pServerAddressTanakaGate = new BLEAddress(advertisedDevice.getAddress());
                doConnectTanakaGate = true;
            }
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
    // transmitter接続
    if (doConnectTransmitter) {
        if (connectToTransmitter()) {
            Serial.println("*** Transmitter connection successful! ***");
            Serial.flush();
        } else {
            Serial.println("*** Transmitter connection failed! ***");
            Serial.flush();
            deviceConnectedTransmitter = false;
        }
        doConnectTransmitter = false;
    }

    // tanaka_gate接続
    if (doConnectTanakaGate) {
        if (connectToTanakaGate()) {
            Serial.println("*** Tanaka Gate connection successful! ***");
            Serial.flush();
        } else {
            Serial.println("*** Tanaka Gate connection failed! ***");
            Serial.flush();
            deviceConnectedTanakaGate = false;
        }
        doConnectTanakaGate = false;
    }

    // transmitter切断検知
    if (deviceConnectedTransmitter && pClientTransmitter && !pClientTransmitter->isConnected()) {
        Serial.println("Transmitter connection lost! Restarting scan...");
        Serial.flush();
        deviceConnectedTransmitter = false;
        if (pClientTransmitter) {
            delete pClientTransmitter;
            pClientTransmitter = nullptr;
        }
        pRemoteCharacteristicTransmitter = nullptr;
        
        delay(2000);
        Serial.println("Starting scan again...");
        Serial.flush();
        BLEDevice::getScan()->start(5, false); // 5秒間スキャン
    }
    // tanaka_gate切断検知
    if (deviceConnectedTanakaGate && pClientTanakaGate && !pClientTanakaGate->isConnected()) {
        Serial.println("Tanaka Gate connection lost! Restarting scan...");
        Serial.flush();
        deviceConnectedTanakaGate = false;
        if (pClientTanakaGate) {
            delete pClientTanakaGate;
            pClientTanakaGate = nullptr;
        }
        pRemoteCharacteristicTanakaGate = nullptr;
        
        delay(2000);
        Serial.println("Starting scan again...");
        Serial.flush();
        BLEDevice::getScan()->start(5, false); // 5秒間スキャン
    }

    // 未接続状態での再スキャン制御
    static unsigned long lastScanTime = 0;
    const long scanInterval = 5000;  // スキャン間隔（5秒）
    
    if ((!deviceConnectedTransmitter || !deviceConnectedTanakaGate) && !doConnectTransmitter && !doConnectTanakaGate && (millis() - lastScanTime >= scanInterval)) {
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
