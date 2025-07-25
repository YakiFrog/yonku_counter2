#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// BLEの設定
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// LED設定
#define LED_PIN 21  // 内蔵LED

// UART設定
#define UART_RX_PIN 44  // UART受信ピン
#define UART_TX_PIN 43  // UART送信ピン
#define UART_BAUD_RATE 115200

// ゲートに対応するUART送信文字
const char gateChars[4] = {'a', 's', 'd', 'f'}; // ゲート1,2,3,4に対応

// 4台のデバイス接続管理（単純化）
struct DeviceConnection {
  BLEClient* pClient;
  BLERemoteCharacteristic* pRemoteCharacteristic;
  bool connected;
  String deviceName;
  String address;
  BLEAddress* pServerAddress;  // 接続用アドレス
  bool doConnect;              // 接続フラグ
};

// 4台のデバイス接続管理
DeviceConnection devices[4];
int connectedDevices = 0;

// 各デバイスのカウント状態管理
int deviceCounts[4] = {0, 0, 0, 0};  // 各デバイスの最新カウント値

// LED制御用変数
unsigned long ledStartTime = 0;
bool ledOn = false;
const int LED_DURATION = 100;  // LED点灯時間（ms）

// 順次ポーリング用変数
int currentPollingDevice = 0;  // 現在ポーリング中のデバイス（0-3）
unsigned long lastPollingTime = 0;
const unsigned long POLLING_INTERVAL = 25;  // 25ms間隔でポーリング

// 単一のコールバックインスタンス
class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
        // Serial.println("*** Client connected successfully ***");
    }

    void onDisconnect(BLEClient* pclient) {
        // Serial.println("*** Client disconnected ***");
        // 該当するデバイスを見つけて状態をリセット
        for (int i = 0; i < 4; i++) {
            if (devices[i].pClient == pclient) {
                devices[i].connected = false;
                connectedDevices--;
                // Serial.print("Device ");
                // Serial.print(i + 1);
                // Serial.println(" disconnection handled");
                break;
            }
        }
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

// 特定のデバイスからデータを読み取る関数
bool pollDeviceData(int deviceIndex) {
    if (!devices[deviceIndex].connected || 
        !devices[deviceIndex].pRemoteCharacteristic) {
        return false;
    }
    
    try {
        // キャラクタリスティックからデータを読み取り
        std::string value = devices[deviceIndex].pRemoteCharacteristic->readValue();
        
        if (value.length() > 0) {
            String receivedData = String(value.c_str());
            
            // 受信データの形式: "デバイス番号:カウント値" (例: "1:5")
            int colonIndex = receivedData.indexOf(':');
            if (colonIndex > 0) {
                int deviceNum = receivedData.substring(0, colonIndex).toInt();
                int count = receivedData.substring(colonIndex + 1).toInt();
                
                // デバイス番号が有効範囲かチェック
                if (deviceNum >= 1 && deviceNum <= 4) {
                    int targetDeviceIndex = deviceNum - 1; // 0-3のインデックスに変換
                    
                    // カウントの変化をチェック
                    if (count != deviceCounts[targetDeviceIndex]) {
                        // カウント値を更新
                        deviceCounts[targetDeviceIndex] = count;
                        
                        // ゲート番号のみを出力（yonku_counterと同じ方式）
                        Serial.println(deviceNum);
                        Serial.flush();
                        
                        // UARTで対応する文字を送信（改行付き）
                        Serial2.println(gateChars[targetDeviceIndex]);
                        Serial2.flush();
                        
                        // LED点灯開始
                        digitalWrite(LED_PIN, HIGH);
                        ledOn = true;
                        ledStartTime = millis();
                        
                        return true;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        // 読み取りエラーの場合は静かに無視
        return false;
    }
    
    return false;
}

// BLEサーバーへの接続（single_testと同じ方式）
bool connectToDevice(int deviceIndex) {
    // Serial.print("接続先デバイス ");
    // Serial.print(deviceIndex + 1);
    // Serial.print(": ");
    // Serial.println(devices[deviceIndex].pServerAddress->toString().c_str());
    // Serial.flush();
    
    devices[deviceIndex].pClient = BLEDevice::createClient();
    devices[deviceIndex].pClient->setClientCallbacks(&clientCallback);
    
    // サーバーに接続
    if (!devices[deviceIndex].pClient->connect(*devices[deviceIndex].pServerAddress)) {
        // Serial.println("ERROR: Failed to connect to server");
        // Serial.flush();
        devices[deviceIndex].pClient = nullptr;
        return false;
    }
    // Serial.println("✓ Connected to server");
    // Serial.flush();

    // サービスの取得
    BLERemoteService* pRemoteService = devices[deviceIndex].pClient->getService(BLEUUID(SERVICE_UUID));
    if (pRemoteService == nullptr) {
        // Serial.println("ERROR: Service not found");
        // Serial.flush();
        devices[deviceIndex].pClient->disconnect();
        devices[deviceIndex].pClient = nullptr;
        return false;
    }
    // Serial.println("✓ Service found");
    // Serial.flush();

    // キャラクタリスティックの取得
    devices[deviceIndex].pRemoteCharacteristic = pRemoteService->getCharacteristic(BLEUUID(CHARACTERISTIC_UUID));
    if (devices[deviceIndex].pRemoteCharacteristic == nullptr) {
        // Serial.println("ERROR: Characteristic not found");
        // Serial.flush();
        devices[deviceIndex].pClient->disconnect();
        devices[deviceIndex].pClient = nullptr;
        return false;
    }
    // Serial.println("✓ Characteristic found");
    // Serial.flush();

    // 通知の登録（ポーリングベースなので不要だが、互換性のため残す）
    if(devices[deviceIndex].pRemoteCharacteristic->canNotify()) {
        devices[deviceIndex].pRemoteCharacteristic->registerForNotify(notifyCallback);
        // Serial.println("✓ Notifications enabled");
        // Serial.flush();
    } else {
        // Serial.println("WARNING: Device does not support notifications");
        // Serial.flush();
    }
    
    devices[deviceIndex].connected = true;
    connectedDevices++;
    
    // Serial.print("*** Device ");
    // Serial.print(deviceIndex + 1);
    // Serial.print(" connected successfully! Total: ");
    // Serial.print(connectedDevices);
    // Serial.println("/4 ***");
    // Serial.flush();
    
    return true;
}

// BLEスキャンコールバッククラス（single_testと同じ方式）
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // デバイス名の取得（空の場合の処理）
    String deviceName = advertisedDevice.getName().c_str();
    if (deviceName.length() == 0) {
      deviceName = "Unknown";
    }
    
    // Serial.printf("Found: %s (%s) RSSI: %d\n", 
    //              deviceName.c_str(),
    //              advertisedDevice.getAddress().toString().c_str(),
    //              advertisedDevice.getRSSI());
    // Serial.flush();
    
    // YonkuCounterデバイスを見つけたら接続準備
    if (advertisedDevice.haveName() && 
        deviceName.startsWith("YonkuCounter_")) {
      
      // デバイス番号を抽出（YonkuCounter_1 -> 1）
      int deviceNum = deviceName.substring(13).toInt(); // "YonkuCounter_" の後の数字
      
      if (deviceNum >= 1 && deviceNum <= 4) {
        int deviceIndex = deviceNum - 1; // 0-3のインデックスに変換
        
        if (!devices[deviceIndex].connected && !devices[deviceIndex].doConnect) {
          // Serial.print(">>> Target YonkuCounter device ");
          // Serial.print(deviceNum);
          // Serial.println(" found! <<<");
          // Serial.flush();
          
          BLEDevice::getScan()->stop();
          devices[deviceIndex].pServerAddress = new BLEAddress(advertisedDevice.getAddress());
          devices[deviceIndex].deviceName = deviceName;
          devices[deviceIndex].address = advertisedDevice.getAddress().toString().c_str();
          devices[deviceIndex].doConnect = true;
        }
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(2000); // 安定化のための待機時間を延長
  // Serial.println("Yonku Counter Transmitter Starting...");
  // Serial.flush();
  
  // UART初期化
  Serial2.begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  
  // LED初期化
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // デバイス接続状態初期化
  for (int i = 0; i < 4; i++) {
    devices[i].pClient = nullptr;
    devices[i].pRemoteCharacteristic = nullptr;
    devices[i].connected = false;
    devices[i].deviceName = "";
    devices[i].address = "";
    devices[i].pServerAddress = nullptr;
    devices[i].doConnect = false;
  }
  
  // 起動LED表示
  for(int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }
  
  // BLE初期化
  // Serial.println("Initializing BLE...");
  // Serial.flush();
  BLEDevice::init("YonkuTransmitter");
  
  // BLE送信パワーを最大に設定
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  
  // BLEスタックの安定化待機
  delay(1000);
  // Serial.println("BLE initialized");
  // Serial.flush();
  
  // BLEスキャン設定
  // Serial.println("Setting up BLE scan...");
  // Serial.flush();
  BLEScan* pBLEScan = BLEDevice::getScan();
  if (pBLEScan == nullptr) {
    // Serial.println("ERROR: Failed to get BLE scan");
    // Serial.flush();
    return;
  }
  
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);  // スキャン間隔
  pBLEScan->setWindow(449);     // スキャンウィンドウ
  pBLEScan->setActiveScan(true); // アクティブスキャン有効
  // Serial.println("BLE scan configured");
  // Serial.flush();
  
  // Serial.println("Scanning for YonkuCounter devices (1-4)...");
  // Serial.flush();
  
  // 初回スキャン開始
  pBLEScan->start(10, false); // 10秒間スキャン（より長い時間でデバイス発見を確実に）
  
  // Serial.println("Transmitter setup complete");
  // Serial.println("Waiting for signals from 4 counters...");
  // Serial.println("=====================================");
  // Serial.flush();
}

void loop() {
  // LED制御（一定時間後に消灯）
  if (ledOn && (millis() - ledStartTime) > LED_DURATION) {
    digitalWrite(LED_PIN, LOW);
    ledOn = false;
  }
  
  // シリアル通信からの入力を確認
  if (Serial.available() > 0) {
    // 1文字読み取り
    char inputChar = Serial.read();
    
    // 有効な文字の場合のみUART経由で送信
    if (isalpha(inputChar) || isdigit(inputChar)) {
      // UART経由でtanaka_gate_serverに送信（改行付き）
      Serial2.println(inputChar);
      Serial2.flush();
      
      Serial.printf("Manual sent: %c\n", inputChar);
      Serial.flush();
      
      // LED点灯
      digitalWrite(LED_PIN, HIGH);
      ledOn = true;
      ledStartTime = millis();
    }
    
    // バッファをクリア
    while(Serial.available()) {
      Serial.read();
    }
  }
  
  // 50ms間隔で順次ポーリング
  if (millis() - lastPollingTime >= POLLING_INTERVAL) {
    lastPollingTime = millis();
    
    // 現在のデバイスをポーリング
    pollDeviceData(currentPollingDevice);
    
    // 次のデバイスに移動（0-3を循環）
    currentPollingDevice = (currentPollingDevice + 1) % 4;
  }
  
  // 各デバイスの接続が必要な場合
  for (int i = 0; i < 4; i++) {
    if (devices[i].doConnect) {
      if (connectToDevice(i)) {
        // 接続成功
      } else {
        // 接続失敗
      }
      devices[i].doConnect = false;
    }
  }

  // 接続状態監視
  for (int i = 0; i < 4; i++) {
    if (devices[i].connected && devices[i].pClient && !devices[i].pClient->isConnected()) {
      devices[i].connected = false;
      if (devices[i].pClient) {
        delete devices[i].pClient;
        devices[i].pClient = nullptr;
      }
      devices[i].pRemoteCharacteristic = nullptr;
      connectedDevices--;
      
      delay(1000);
      BLEDevice::getScan()->start(10, false); // 10秒間スキャン
    }
  }

  // 未接続状態での再スキャン制御
  static unsigned long lastScanTime = 0;
  const long scanInterval = 8000;  // スキャン間隔（8秒）
  
  bool needRescan = false;
  for (int i = 0; i < 4; i++) {
    if (!devices[i].connected && !devices[i].doConnect) {
      needRescan = true;
      break;
    }
  }
  
  if (needRescan && (millis() - lastScanTime >= scanInterval)) {
    lastScanTime = millis();
    BLEDevice::getScan()->start(10, false);  // 10秒間スキャン
  }
  
  delay(10);  // 短い遅延でCPU使用率を下げる（100ms -> 10ms）
}
