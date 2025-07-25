#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// BLEの設定
#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  // Nordic UART Service UUID
#define CHARACTERISTIC_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // TX Characteristic UUID

// LED設定
#define LED_PIN 21  // 内蔵LED

// UART設定
#define UART_RX_PIN 44  // UART受信ピン
#define UART_TX_PIN 43  // UART送信ピン
#define UART_BAUD_RATE 115200

// サーバー接続管理
struct ServerConnection {
  BLEClient* pClient;
  BLERemoteCharacteristic* pRemoteCharacteristic;
  bool connected;
  String deviceName;
  String address;
  BLEAddress* pServerAddress;
  bool doConnect;
};

ServerConnection server;

// データ管理用変数
String lastReceivedData = "-";  // 最新の受信データ（初期値は「-」）
String previousData = "";       // 前回の受信データ（重複チェック用）

// LED制御用変数
unsigned long ledStartTime = 0;
bool ledOn = false;
const int LED_DURATION = 100;  // LED点灯時間（ms）

// データポーリング用変数
unsigned long lastPollingTime = 0;
const unsigned long POLLING_INTERVAL = 100;  // 100ms間隔でポーリング

// BLEクライアントコールバッククラス
class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
        Serial.println("*** Connected to Tanaka Gate Server ***");
    }

    void onDisconnect(BLEClient* pclient) {
        Serial.println("*** Disconnected from Tanaka Gate Server ***");
        server.connected = false;
        
        // 切断時はデータを「-」にリセット
        lastReceivedData = "-";
        previousData = "";
        Serial.println("Latest Data: -");
        // Serial2.println("-");
    }
};

// グローバルコールバックインスタンス
MyClientCallback clientCallback;

// 通知コールバック（使用しない - ポーリングベースに変更）
static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    // ポーリングベースに変更したため、この関数は使用しない
}

// サーバーからデータを読み取る関数
bool pollServerData() {
    if (!server.connected || !server.pRemoteCharacteristic) {
        return false;
    }
    
    try {
        // キャラクタリスティックからデータを読み取り
        std::string value = server.pRemoteCharacteristic->readValue();
        
        if (value.length() > 0) {
            String receivedData = String(value.c_str());
            
            // データが変化した場合のみ処理（タイムスタンプにより常に変化）
            if (receivedData != previousData) {
                previousData = receivedData;
                
                // タイムスタンプ付きデータから実際のデータを抽出
                // 形式: "timestamp:actualData"
                int colonIndex = receivedData.indexOf(':');
                String actualData;
                if (colonIndex > 0) {
                    actualData = receivedData.substring(colonIndex + 1);
                } else {
                    actualData = receivedData; // タイムスタンプがない場合はそのまま
                }
                
                lastReceivedData = actualData;
                
                // 受信データを表示
                Serial.print("Latest Data: ");
                Serial.println(lastReceivedData);
                Serial.flush();
                
                // UARTで実際のデータを送信
                Serial2.println(lastReceivedData);
                Serial2.flush();
                
                // LED点灯開始
                digitalWrite(LED_PIN, HIGH);
                ledOn = true;
                ledStartTime = millis();
                
                return true;
            }
        } else {
            // データがない場合は「-」に設定
            if (lastReceivedData != "-") {
                lastReceivedData = "-";
                Serial.println("Latest Data: -");
                Serial.flush();
                
                // UARTで「-」を送信
                // Serial2.println("-");
                // Serial2.flush();
            }
        }
    } catch (const std::exception& e) {
        // 読み取りエラーの場合も「-」に設定
        if (lastReceivedData != "-") {
            lastReceivedData = "-";
            Serial.println("Latest Data: -");
            Serial.flush();
            
            // UARTで「-」を送信
            // Serial2.println("-");
            // Serial2.flush();
        }
        return false;
    }
    
    return false;
}

// Tanaka Gate Serverへの接続
bool connectToServer() {
    Serial.print("接続先サーバー: ");
    Serial.println(server.pServerAddress->toString().c_str());
    Serial.flush();
    
    server.pClient = BLEDevice::createClient();
    server.pClient->setClientCallbacks(&clientCallback);
    
    // サーバーに接続
    if (!server.pClient->connect(*server.pServerAddress)) {
        Serial.println("ERROR: Failed to connect to server");
        Serial.flush();
        server.pClient = nullptr;
        return false;
    }
    Serial.println("✓ Connected to server");
    Serial.flush();

    // サービスの取得
    BLERemoteService* pRemoteService = server.pClient->getService(BLEUUID(SERVICE_UUID));
    if (pRemoteService == nullptr) {
        Serial.println("ERROR: Service not found");
        Serial.flush();
        server.pClient->disconnect();
        server.pClient = nullptr;
        return false;
    }
    Serial.println("✓ Service found");
    Serial.flush();

    // キャラクタリスティックの取得
    server.pRemoteCharacteristic = pRemoteService->getCharacteristic(BLEUUID(CHARACTERISTIC_UUID));
    if (server.pRemoteCharacteristic == nullptr) {
        Serial.println("ERROR: Characteristic not found");
        Serial.flush();
        server.pClient->disconnect();
        server.pClient = nullptr;
        return false;
    }
    Serial.println("✓ Characteristic found");
    Serial.flush();

    // 通知の登録
    if(server.pRemoteCharacteristic->canNotify()) {
        server.pRemoteCharacteristic->registerForNotify(notifyCallback);
        Serial.println("✓ Notifications enabled");
        Serial.flush();
    } else {
        Serial.println("WARNING: Server does not support notifications");
        Serial.flush();
    }
    
    server.connected = true;
    
    Serial.println("*** Successfully connected to Tanaka Gate Server! ***");
    Serial.flush();
    
    return true;
}

// BLEスキャンコールバッククラス
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // デバイス名の取得（空の場合の処理）
    String deviceName = advertisedDevice.getName().c_str();
    if (deviceName.length() == 0) {
      deviceName = "Unknown";
    }
    
    // TanakaGateServerデバイスのみログ出力
    if (advertisedDevice.haveName() && deviceName.startsWith("TanakaGateServer")) {
      Serial.printf("Found Target: %s (%s) RSSI: %d\n", 
                   deviceName.c_str(),
                   advertisedDevice.getAddress().toString().c_str(),
                   advertisedDevice.getRSSI());
      Serial.flush();
    }
    
    // TanakaGateServerデバイスを見つけたら接続準備
    if (advertisedDevice.haveName() && 
        deviceName.startsWith("TanakaGateServer")) {
      
      if (!server.connected && !server.doConnect) {
        Serial.println(">>> Target TanakaGateServer found! <<<");
        Serial.flush();
        
        BLEDevice::getScan()->stop();
        server.pServerAddress = new BLEAddress(advertisedDevice.getAddress());
        server.deviceName = deviceName;
        server.address = advertisedDevice.getAddress().toString().c_str();
        server.doConnect = true;
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(2000); // 安定化のための待機時間を延長
  Serial.println("Tanaka Gate Client Starting...");
  Serial.flush();
  
  // UART初期化
  Serial2.begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.println("UART initialized");
  
  // LED初期化
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // サーバー接続状態初期化
  server.pClient = nullptr;
  server.pRemoteCharacteristic = nullptr;
  server.connected = false;
  server.deviceName = "";
  server.address = "";
  server.pServerAddress = nullptr;
  server.doConnect = false;
  
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
  BLEDevice::init("TanakaGateClient");
  
  // BLE送信パワーを最大に設定
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  
  // BLEスタックの安定化待機
  delay(1000);
  Serial.println("BLE initialized");
  Serial.flush();
  
  // BLEスキャン設定
  Serial.println("Setting up BLE scan...");
  Serial.flush();
  BLEScan* pBLEScan = BLEDevice::getScan();
  if (pBLEScan == nullptr) {
    Serial.println("ERROR: Failed to get BLE scan");
    Serial.flush();
    return;
  }
  
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);  // スキャン間隔
  pBLEScan->setWindow(449);     // スキャンウィンドウ
  pBLEScan->setActiveScan(true); // アクティブスキャン有効
  Serial.println("BLE scan configured");
  Serial.flush();
  
  Serial.println("Scanning for TanakaGateServer...");
  Serial.flush();
  
  // 初回スキャン開始
  pBLEScan->start(10, false); // 10秒間スキャン
  
  Serial.println("Tanaka Gate Client setup complete");
  Serial.println("Waiting for TanakaGateServer...");
  Serial.println("=====================================");
  Serial.flush();
}

void loop() {
  // LED制御（一定時間後に消灯）
  if (ledOn && (millis() - ledStartTime) > LED_DURATION) {
    digitalWrite(LED_PIN, LOW);
    ledOn = false;
  }
  
  // 100ms間隔でサーバーからデータをポーリング
  if (millis() - lastPollingTime >= POLLING_INTERVAL) {
    lastPollingTime = millis();
    
    // サーバーからデータを読み取り
    pollServerData();
  }
  
  // サーバーへの接続が必要な場合
  if (server.doConnect) {
    if (connectToServer()) {
      Serial.println("Connected to TanakaGateServer!");
    } else {
      Serial.println("Failed to connect to TanakaGateServer");
    }
    server.doConnect = false;
  }

  // 接続状態監視と再接続処理
  if (server.connected && server.pClient && !server.pClient->isConnected()) {
    Serial.println("Server disconnected, cleaning up...");
    
    server.connected = false;
    if (server.pClient) {
      delete server.pClient;
      server.pClient = nullptr;
    }
    server.pRemoteCharacteristic = nullptr;
    
    delay(1000);
    BLEDevice::getScan()->start(10, false); // 再スキャン開始
  }

  // 未接続状態での再スキャン制御
  static unsigned long lastScanTime = 0;
  const long scanInterval = 8000;  // スキャン間隔（8秒）
  
  if (!server.connected && !server.doConnect && (millis() - lastScanTime >= scanInterval)) {
    lastScanTime = millis();
    Serial.println("Starting rescan for TanakaGateServer...");
    BLEDevice::getScan()->start(10, false);  // 10秒間スキャン
  }
  
  // 接続状況の定期的な表示
  static unsigned long lastStatusTime = 0;
  const long statusInterval = 30000;  // 30秒間隔で状況表示
  
  if (millis() - lastStatusTime >= statusInterval) {
    lastStatusTime = millis();
    Serial.println("--------------------");
    Serial.print("Server connection status: ");
    if (server.connected) {
      Serial.println("Connected");
      Serial.print("  Server: ");
      Serial.println(server.deviceName);
      Serial.print("  Latest Data: ");
      Serial.println(lastReceivedData);
    } else {
      Serial.println("Disconnected");
      Serial.println("  Latest Data: -");
    }
    Serial.println("--------------------");
  }
  
  delay(10);  // 短い遅延でCPU使用率を下げる
}
