// MACアドレス確認用の簡単なスキャナー
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveName()) {
      String name = advertisedDevice.getName().c_str();
      if (name.startsWith("YonkuCounter_")) {
        Serial.println("=================================");
        Serial.print("デバイス名: ");
        Serial.println(name);
        Serial.print("MACアドレス: ");
        Serial.println(advertisedDevice.getAddress().toString().c_str());
        Serial.print("RSSI: ");
        Serial.println(advertisedDevice.getRSSI());
        if (advertisedDevice.haveServiceUUID()) {
          Serial.print("サービスUUID: ");
          Serial.println(advertisedDevice.getServiceUUID().toString().c_str());
        }
        Serial.println("=================================");
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("YonkuCounterデバイスのMACアドレススキャナー");
  Serial.println("=====================================");
  
  BLEDevice::init("MACScanner");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  
  Serial.println("スキャン開始...");
}

void loop() {
  BLEDevice::getScan()->start(5, false);
  delay(6000);
  Serial.println("再スキャン...");
}
