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

// 接続管理用の構造体
struct DeviceConnection {
  BLEClient* pClient;
  BLERemoteCharacteristic* pRemoteCharacteristic;
  bool connected;
  String deviceName;
  BLEAddress* address;  // ポインタに変更
  unsigned long lastConnectionAttempt;
  int retryCount;
};

// 4台のデバイス接続管理
DeviceConnection devices[4];
int connectedDevices = 0;

// 各デバイスのカウント状態管理
int deviceCounts[4] = {0, 0, 0, 0};  // 各デバイスの最新カウント値
int lastDeviceCounts[4] = {0, 0, 0, 0}; // 前回のカウント値（変化検出用）

// LED制御用変数
unsigned long ledStartTime = 0;
bool ledOn = false;
const int LED_DURATION = 100;  // LED点灯時間（ms）

// BLEクライアント接続状態管理用コールバッククラス
class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
        Serial.println("クライアント接続確立");
    }

    void onDisconnect(BLEClient* pclient) {
        Serial.println("クライアント接続切断検出");
        // 該当するデバイスを見つけて状態をリセット
        for (int i = 0; i < 4; i++) {
            if (devices[i].pClient == pclient) {
                devices[i].connected = false;
                connectedDevices--;
                Serial.print("デバイス");
                Serial.print(i + 1);
                Serial.println("の切断を処理しました");
                break;
            }
        }
    }
};

// 通知コールバック
static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    // データを文字列として表示
    String receivedData = "";
    for (int i = 0; i < length; i++) {
        receivedData += (char)pData[i];
    }

    // 受信データの形式: "デバイス番号:カウント値" (例: "1:5")
    int colonIndex = receivedData.indexOf(':');
    if (colonIndex > 0) {
        int deviceNum = receivedData.substring(0, colonIndex).toInt();
        int count = receivedData.substring(colonIndex + 1).toInt();
        
        // デバイス番号が有効範囲かチェック
        if (deviceNum >= 1 && deviceNum <= 4) {
            int deviceIndex = deviceNum - 1; // 0-3のインデックスに変換
            
            // カウントの変化をチェック
            if (count != deviceCounts[deviceIndex]) {
                Serial.print("デバイス");
                Serial.print(deviceNum);
                Serial.print(": ");
                Serial.print(deviceCounts[deviceIndex]);
                Serial.print(" -> ");
                Serial.println(count);
                
                // カウント値を更新
                deviceCounts[deviceIndex] = count;
                
                // LED点灯開始
                digitalWrite(LED_PIN, HIGH);
                ledOn = true;
                ledStartTime = millis();
            }
        }
    } else {
        // 旧形式のデータ（デバイス番号のみ）の場合の処理
        Serial.print("旧形式データ受信: ");
        Serial.println(receivedData);
    }
}

// デバイスに接続する関数
bool connectToDevice(int deviceIndex, BLEAdvertisedDevice* advertisedDevice) {
  Serial.print("デバイス");
  Serial.print(deviceIndex + 1);
  Serial.println("に接続試行中...");
  Serial.print("接続先アドレス: ");
  Serial.println(advertisedDevice->getAddress().toString().c_str());
  
  // BLEクライアント作成
  devices[deviceIndex].pClient = BLEDevice::createClient();
  if (devices[deviceIndex].pClient == nullptr) {
    Serial.println("エラー: BLEクライアント作成失敗");
    return false;
  }
  Serial.println("BLEクライアント作成完了");
  
  // 接続状態コールバックを設定
  devices[deviceIndex].pClient->setClientCallbacks(new MyClientCallback());
  Serial.println("クライアントコールバック設定完了");
  
  // 接続試行（ブロッキング処理）
  Serial.println("BLE接続開始...");
  Serial.flush(); // シリアルバッファをフラッシュ
  
  try {
    bool connectResult = devices[deviceIndex].pClient->connect(advertisedDevice);
    
    if (!connectResult) {
      Serial.println("BLE接続失敗 - connect()がfalseを返しました");
      if (devices[deviceIndex].pClient) {
        delete devices[deviceIndex].pClient;
        devices[deviceIndex].pClient = nullptr;
      }
      return false;
    }
    
    Serial.println("connect()成功");
    Serial.flush();
    
    // 接続状態を確認
    delay(100); // 接続安定化待機
    if (!devices[deviceIndex].pClient->isConnected()) {
      Serial.println("BLE接続失敗 - isConnected()がfalse");
      devices[deviceIndex].pClient->disconnect();
      delete devices[deviceIndex].pClient;
      devices[deviceIndex].pClient = nullptr;
      return false;
    }
    
    Serial.println("BLE接続確認成功");
    
  } catch (const std::exception& e) {
    Serial.print("BLE接続例外: ");
    Serial.println(e.what());
    if (devices[deviceIndex].pClient) {
      delete devices[deviceIndex].pClient;
      devices[deviceIndex].pClient = nullptr;
    }
    return false;
  }
  
  Serial.println("サービス取得開始...");
  Serial.flush();
  
  BLERemoteService* pRemoteService = nullptr;
  try {
    pRemoteService = devices[deviceIndex].pClient->getService(SERVICE_UUID);
    if (pRemoteService == nullptr) {
      Serial.println("サービスが見つかりません");
      Serial.print("探索対象UUID: ");
      Serial.println(SERVICE_UUID);
      devices[deviceIndex].pClient->disconnect();
      delete devices[deviceIndex].pClient;
      devices[deviceIndex].pClient = nullptr;
      return false;
    }
    Serial.println("サービス取得成功");
  } catch (const std::exception& e) {
    Serial.print("サービス取得例外: ");
    Serial.println(e.what());
    devices[deviceIndex].pClient->disconnect();
    delete devices[deviceIndex].pClient;
    devices[deviceIndex].pClient = nullptr;
    return false;
  }
  
  Serial.println("キャラクタリスティック取得開始...");
  Serial.flush();
  
  try {
    devices[deviceIndex].pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
    if (devices[deviceIndex].pRemoteCharacteristic == nullptr) {
      Serial.println("キャラクタリスティックが見つかりません");
      Serial.print("探索対象UUID: ");
      Serial.println(CHARACTERISTIC_UUID);
      devices[deviceIndex].pClient->disconnect();
      delete devices[deviceIndex].pClient;
      devices[deviceIndex].pClient = nullptr;
      return false;
    }
    Serial.println("キャラクタリスティック取得成功");
  } catch (const std::exception& e) {
    Serial.print("キャラクタリスティック取得例外: ");
    Serial.println(e.what());
    devices[deviceIndex].pClient->disconnect();
    delete devices[deviceIndex].pClient;
    devices[deviceIndex].pClient = nullptr;
    return false;
  }
  
  // 通知機能設定
  if (devices[deviceIndex].pRemoteCharacteristic->canNotify()) {
    Serial.println("通知登録開始...");
    try {
      devices[deviceIndex].pRemoteCharacteristic->registerForNotify(notifyCallback);
      Serial.println("通知登録成功");
    } catch (const std::exception& e) {
      Serial.print("通知登録例外: ");
      Serial.println(e.what());
      // 通知失敗でも接続は継続
    }
  } else {
    Serial.println("警告: 通知機能が利用できません");
  }
  
  devices[deviceIndex].connected = true;
  devices[deviceIndex].deviceName = advertisedDevice->getName().c_str();
  devices[deviceIndex].address = new BLEAddress(advertisedDevice->getAddress());
  connectedDevices++;
  
  Serial.print("*** デバイス");
  Serial.print(deviceIndex + 1);
  Serial.print("に接続成功: ");
  Serial.print(devices[deviceIndex].deviceName);
  Serial.println(" ***");
  Serial.flush();
  
  return true;
}

// BLEスキャンコールバッククラス
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // スキャンしたデバイスの情報を表示（デバッグ用）
    Serial.printf("デバイス発見: %s ", advertisedDevice.toString().c_str());
    Serial.printf("(RSSI: %d)", advertisedDevice.getRSSI());
    
    if (advertisedDevice.haveName()) {
      Serial.printf(" 名前: %s", advertisedDevice.getName().c_str());
    }
    Serial.println();
    
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
      String deviceName = advertisedDevice.getName().c_str();
      
      // YonkuCounter_1〜4のデバイス名を探す
      for (int i = 1; i <= 4; i++) {
        String targetName = "YonkuCounter_" + String(i);
        if (deviceName == targetName && !devices[i-1].connected) {
          Serial.println(">>> " + targetName + "が見つかりました! <<<");
          Serial.flush();
          
          // スキャンを停止
          BLEDevice::getScan()->stop();
          
          // 接続前に安定化待機
          delay(1000);
          
          Serial.println("接続処理開始...");
          Serial.flush();
          
          if (connectToDevice(i-1, &advertisedDevice)) {
            Serial.print("接続済みデバイス数: ");
            Serial.print(connectedDevices);
            Serial.println("/4");
            Serial.flush();
          } else {
            Serial.println("接続に失敗しました。次のスキャンで再試行します。");
            Serial.flush();
          }
          
          // 他のデバイスも探すためにスキャンを再開
          if (connectedDevices < 4) {
            delay(3000); // 接続後の安定化待機時間をさらに延長
            Serial.println("他のデバイスを探索中...");
            Serial.flush();
            BLEDevice::getScan()->start(5, false);
          } else {
            Serial.println("全デバイス接続完了！");
            Serial.flush();
          }
          return; // 重要: onResultから抜ける
        }
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Yonku Counter Transmitter (統合表示) プログラム開始");
  
  // LED初期化
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // デバイス接続状態初期化
  for (int i = 0; i < 4; i++) {
    devices[i].pClient = nullptr;
    devices[i].pRemoteCharacteristic = nullptr;
    devices[i].connected = false;
    devices[i].deviceName = "";
    devices[i].address = nullptr;
    devices[i].lastConnectionAttempt = 0;
    devices[i].retryCount = 0;
  }
  
  // 起動LED表示
  for(int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }
  
  // BLE初期化
  Serial.println("BLE初期化開始...");
  BLEDevice::init("YonkuTransmitter");
  
  // BLE送信パワーを最大に設定（接続安定性向上）
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  
  // BLEコアの安定化待機
  delay(1000);
  Serial.println("BLE初期化完了");
  
  // BLEスキャン設定
  Serial.println("BLEスキャン設定開始...");
  BLEScan* pBLEScan = BLEDevice::getScan();
  if (pBLEScan == nullptr) {
    Serial.println("エラー: BLEスキャン取得失敗");
    return;
  }
  
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  Serial.println("BLEスキャン設定完了");
  
  Serial.println("YonkuCounterデバイス(1-4)をスキャン中...");
  Serial.println("近くのBLEデバイスを探索中...");
  Serial.flush();
  
  // 初回スキャン開始
  pBLEScan->start(5, false);
  Serial.println("初回スキャン開始");
  
  Serial.println("トランスミッターセットアップ完了");
  Serial.println("4台のカウンターからの信号を待機中...");
  Serial.println("=====================================");
  Serial.flush();
}

void loop() {
  // LED制御（一定時間後に消灯）
  if (ledOn && (millis() - ledStartTime) > LED_DURATION) {
    digitalWrite(LED_PIN, LOW);
    ledOn = false;
  }
  
  // 接続状態チェックと再接続試行
  static unsigned long lastConnectionCheck = 0;
  if (millis() - lastConnectionCheck > 10000) { // 10秒ごとにチェック
    for (int i = 0; i < 4; i++) {
      if (devices[i].connected && devices[i].pClient && !devices[i].pClient->isConnected()) {
        Serial.print("デバイス");
        Serial.print(i + 1);
        Serial.println("との接続が切断されました");
        devices[i].connected = false;
        connectedDevices--;
        
        // リソースのクリーンアップ
        if (devices[i].pClient) {
          delete devices[i].pClient;
          devices[i].pClient = nullptr;
        }
        devices[i].pRemoteCharacteristic = nullptr;
        if (devices[i].address) {
          delete devices[i].address;
          devices[i].address = nullptr;
        }
      }
    }
    
    // 未接続のデバイスがある場合、再スキャン
    if (connectedDevices < 4) {
      Serial.print("再スキャン開始 (接続済み: ");
      Serial.print(connectedDevices);
      Serial.println("/4)");
      Serial.println("近くのBLEデバイスを探索中...");
      BLEDevice::getScan()->start(5, false);
    }
    
    lastConnectionCheck = millis();
  }
  
  // カウント変化検出とシリアル出力
  static unsigned long lastChangeCheck = 0;
  if (millis() - lastChangeCheck > 50) { // 50msごとにチェック
    String changedLanes = "";
    
    for (int i = 0; i < 4; i++) {
      if (deviceCounts[i] != lastDeviceCounts[i]) {
        changedLanes += String(i + 1); // レーン番号（1-4）を追加
        lastDeviceCounts[i] = deviceCounts[i]; // 前回値を更新
      }
    }
    
    // 変化があった場合のみシリアル出力
    if (changedLanes.length() > 0) {
      Serial.println(changedLanes);
    }
    
    lastChangeCheck = millis();
  }
  
  // ステータス表示
  static unsigned long lastStatusPrint = 0;
  if (millis() - lastStatusPrint > 30000) { // 30秒ごとにステータス表示
    Serial.print("=== ステータス ===");
    Serial.print(" 接続デバイス: ");
    Serial.print(connectedDevices);
    Serial.println("/4");
    
    // 各デバイスのカウント状況を表示
    for (int i = 0; i < 4; i++) {
      Serial.print("デバイス");
      Serial.print(i + 1);
      Serial.print(": ");
      if (devices[i].connected) {
        Serial.print("接続済み カウント=");
        Serial.print(deviceCounts[i]);
        Serial.print(" (");
        Serial.print(devices[i].deviceName);
        Serial.println(")");
      } else {
        Serial.println("未接続");
      }
    }
    Serial.println("=================");
    lastStatusPrint = millis();
  }
  
  delay(100);
}
