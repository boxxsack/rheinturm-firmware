#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <WiFi.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include <time.h>

#define DEBUG

#define LED_PIN 5
#define LED_COUNT 41
#define LENGTH(x) (strlen(x) + 1)
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

const char *NTP_SERVER = "de.pool.ntp.org";
const char *TZ_INFO = "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00";

int blinkgeschwindigkeit = 1; // hier kann man die roten Trenn-LEDs blinken lassen – wenn man das nicht will, kann man die Variable auf 1 setzen
int theLEDsAus[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int theLEDs[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int brightness = 100;

tm timeinfo;
time_t now;
time_t lastUpdate;
long unsigned lastNTPtime;
unsigned long lastEntryTime;
String scanState = "";

BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic_confState = NULL;
BLECharacteristic *pCharacteristic_ssid = NULL;
BLECharacteristic *pCharacteristic_password = NULL;
BLECharacteristic *pCharacteristic_scanState = NULL;
BLECharacteristic *pCharacteristic_scanList = NULL;
BLECharacteristic *pCharacteristic_brightness = NULL;
BLEDescriptor *pDescr;
BLE2902 *pBLE2902;

bool deviceConnected = false;
bool oldDeviceConnected = false;
uint32_t value = 0;

static BLEUUID BLESERVICE_UUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CONF_STATE_UUID "4fafff01-1fb5-459e-8fcc-c5c9c331914b"
#define CONF_SSID_UUID "4fafff02-1fb5-459e-8fcc-c5c9c331914b"
#define CONF_PASSWORD_UUID "4fafff03-1fb5-459e-8fcc-c5c9c331914b"
#define SCAN_STATE_UUID "4fafff04-1fb5-459e-8fcc-c5c9c331914b"
#define SCAN_LIST_UUID "4fafff05-1fb5-459e-8fcc-c5c9c331914b"
#define BRIGHTNESS_UUID "4fafff06-1fb5-459e-8fcc-c5c9c331914b"

Preferences preferences;
#define MAX_SSID_LENGTH 20
String ssid, password;

bool isWiFiConnected()
{
  return WiFi.status() == WL_CONNECTED;
}

void attemptWiFiConnection()
{
  Serial.println("Attempting to connect to WiFi...");
  unsigned long startMillis = millis();
  WiFi.begin(ssid, password);
  int i = 0;
  while (!isWiFiConnected())
  {
    if (millis() - startMillis >= 10000)
    {
      Serial.println("Connection timeout.");
      WiFi.disconnect();
      break;
    }
    delay(1000);
    Serial.print(".");
    strip.setPixelColor(LED_COUNT - i, strip.Color(0, 0, 255));
    strip.show();
    i++;
  }
  if (isWiFiConnected())
  {
    Serial.println("\nWiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    configTime(0, 0, NTP_SERVER);
    setenv("TZ", TZ_INFO, 1);

    lastNTPtime = time(&now);
    lastEntryTime = millis();
  }
}

uint32_t Wheel(byte WheelPos)
{
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85)
  {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if (WheelPos < 170)
  {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void rainbow(uint16_t duration)
{
  uint32_t startMillis = millis();
  while (millis() - startMillis < duration)
  {
    for (int j = 0; j < 256; j++)
    {
      for (int i = 0; i < strip.numPixels(); i++)
      {
        strip.setPixelColor(i, Wheel((i + j) & 255));
        if (i == 11 || i == 26)
        {
          strip.setPixelColor(i, strip.Color(0, 0, 0));
        }
      }
      strip.show();
      delay(10);
    }
  }
}

bool getNTPtime(int timeoutSeconds)
{
  const int minValidYear = 2016;
  const int yearOffset = 1900;
  uint32_t startTime = millis();

  do
  {
    time(&now);
    localtime_r(&now, &timeinfo);
    delay(10); // Let NTP sync in background
  } while ((millis() - startTime < timeoutSeconds * 1000) &&
           (timeinfo.tm_year < (minValidYear - yearOffset)));

  if (timeinfo.tm_year < (minValidYear - yearOffset))
  {
    return false; // NTP sync failed
  }

// Optional: Print or log the current time
#ifdef DEBUG
  char timeStr[30];
  strftime(timeStr, sizeof(timeStr), "%a %d-%m-%y %T", &timeinfo);
  Serial.println(timeStr);
#endif

  return true;
}

void updateTime(tm localTime)
{
  /* Reset aller LED Einträge */
  for (int i = 0; i < strip.numPixels(); i++)
  {
    theLEDs[i] = theLEDsAus[i];
  }

  /* Sekunden Einer */
  for (int i = 0; i < localTime.tm_sec % 10; i++)
  {
    theLEDs[40 - i] = 1;
  }
  /* Sekunden Zehner */
  for (int i = 0; i < localTime.tm_sec / 10; i++)
  {
    theLEDs[31 - i] = 1;
  }

  /* Minuten Einer */
  for (int i = 0; i < localTime.tm_min % 10; i++)
  {
    theLEDs[25 - i] = 1;
  }
  /* Minuten Zehner */
  for (int i = 0; i < localTime.tm_min / 10; i++)
  {
    theLEDs[16 - i] = 1;
  }

  /* Stunden Einer */
  for (int i = 0; i < localTime.tm_hour % 10; i++)
  {
    theLEDs[10 - i] = 1;
  }
  /* Stunden Zehner */
  for (int i = 0; i < localTime.tm_hour / 10; i++)
  {
    theLEDs[1 - i] = 1;
  }
};

void showTime()
{

  strip.clear();

  for (int i = 0; i < strip.numPixels(); i++)
  {
    switch (theLEDs[i])
    {
    case 0:
      strip.setPixelColor(i, strip.Color(0, 0, 0));
      break;
    case 1:
      strip.setPixelColor(i, strip.Color(255, 243, 170));
      break;
    case 2:
      break;
    case 3:
      if (millis() % blinkgeschwindigkeit < blinkgeschwindigkeit / 2)
      {
        strip.setPixelColor(i, strip.Color(0, 0, 0));
      }
      else
      {
        strip.setPixelColor(i, strip.Color(255, 0, 0));
      }
      break;
    }
  }
  strip.show();
}

uint8_t *buildWiFiScanList(int &arraySize)
{
  WiFi.disconnect();
  delay(1000);
  int n = WiFi.scanNetworks(); // Scan for available networks
  Serial.println("Scan done");

  if (n <= 0)
  {
    Serial.println("No networks found");
    arraySize = 0;
    return nullptr;
  }
  else
  {
    Serial.print(n);
    Serial.println(" networks found:");

    // Create a dynamic array to store the byte array
    arraySize = n * (1 + MAX_SSID_LENGTH + 1);
    uint8_t *byteArray = new uint8_t[arraySize];
    int index = 0;

    for (int i = 0; i < n; ++i)
    {
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      Serial.println("ssid: " + WiFi.SSID(i) + "(" + WiFi.RSSI(i) + ")");
      // Ensure SSID is not longer than 20 characters
      if (ssid.length() > MAX_SSID_LENGTH)
      {
        ssid = ssid.substring(0, MAX_SSID_LENGTH);
      }

      // Add SSID length
      byteArray[index++] = ssid.length();

      // Add SSID characters
      for (int j = 0; j < ssid.length(); ++j)
      {
        byteArray[index++] = ssid[j];
      }

      // Add padding if SSID is shorter than 20 characters
      for (int j = ssid.length(); j < MAX_SSID_LENGTH; ++j)
      {
        byteArray[index++] = 0x00; // Null padding
      }

      // Add RSSI value
      byteArray[index++] = (uint8_t)rssi;
    }

    Serial.print("Byte size: ");
    Serial.println(arraySize);
    // Print the byte array before returning
    Serial.println("Byte array:");
    for (int i = 0; i < arraySize; ++i)
    {
      Serial.print("0x");
      if (byteArray[i] < 0x10)
        Serial.print("0"); // Leading zero for single digit hex values
      Serial.print(byteArray[i], HEX);
      if (i < arraySize - 1)
        Serial.print(", ");
    }
    Serial.println(); // New line after printing the array
    return byteArray;
  }
}

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    deviceConnected = true;
  };

  void onDisconnect(BLEServer *pServer)
  {
    deviceConnected = false;
  }
};

class ssidCallBack : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pChar) override
  {
    std::string ssid_stdstr = pChar->getValue();
    String ssid_string = String(ssid_stdstr.c_str());
    ssid = ssid_string;
    Serial.println("SSID: " + ssid_string);
  }
};

class passwordCallBack : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pChar) override
  {
    std::string password_stdstr = pChar->getValue();
    password = String(password_stdstr.c_str());
    Serial.println("SSID: " + ssid);
    Serial.println("Password: " + password);

    attemptWiFiConnection();
    if (isWiFiConnected())
    {
      pCharacteristic_confState->setValue("connected");
      preferences.putString("ssid", ssid);
      preferences.putString("password", password);
      Serial.println("");
      Serial.println("WiFi connected");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
    }
    else
    {
      WiFi.disconnect();
    }
  }
};

class scanStateCallBack : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pChar) override
  {
    std::string scanState_stdstr = pChar->getValue();
    scanState = String(scanState_stdstr.c_str());
    Serial.println("ScanState: " + scanState);
    if (scanState == "scan-start")
    {
      pCharacteristic_scanState->setValue("scanning");
      pCharacteristic_scanState->notify();
      scanState = "scanning";
      Serial.println("Initiate Scan...");

      // Scan for networks and set the characteristic value
      int arraySize = 0;
      uint8_t *byteArray = buildWiFiScanList(arraySize);
      if (byteArray != nullptr)
      {
        // Split the byteArray into chunks if it exceeds the MTU size (default 20 bytes)
        int offset = 0;
        const int chunkSize = 20; // Adjust based on the negotiated MTU size if necessary

        while (offset < arraySize)
        {
          int bytesToSend = min(chunkSize, arraySize - offset);
          pCharacteristic_scanList->setValue(byteArray + offset, bytesToSend);
          pCharacteristic_scanList->notify(); // Notify the client
          offset += bytesToSend;
          delay(100); // Short delay to ensure proper transmission
        }
        delete[] byteArray; // Clean up dynamic array
        pCharacteristic_scanState->setValue("scan-end");
        pCharacteristic_scanState->notify();
        Serial.println("ScanState: scan-end");
        scanState = "scan-end";
      }
      else
      {
        pCharacteristic_scanState->setValue("scan-end");
        pCharacteristic_scanState->notify();
        Serial.println("ScanState: scan-end");
        scanState = "scan-end";
      }
    }
  }
};

class brightnessCallBack : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pChar) override
  {
    std::string brightness_stdstr = pChar->getValue();
    int brightness_value_int = static_cast<uint8_t>(brightness_stdstr[0]);

    brightness_value_int = constrain(brightness_value_int, 0, 255);

    Serial.println("Set Brightness to: " + String(brightness_value_int));
    strip.setBrightness(brightness_value_int);
  }
};

void setup()
{
  Serial.begin(115200);
  while (!Serial)
  {
    delay(100);
  }
  Serial.println("*****SETUP START*****");

  Serial.println("Starting BLE!");

  BLEDevice::init("Rheinturm");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(BLESERVICE_UUID, 30, 0);

  // Create a BLE Characteristic
  pCharacteristic_confState = pService->createCharacteristic(
      CONF_STATE_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pCharacteristic_ssid = pService->createCharacteristic(
      CONF_SSID_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pCharacteristic_password = pService->createCharacteristic(
      CONF_PASSWORD_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pCharacteristic_scanState = pService->createCharacteristic(
      SCAN_STATE_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pCharacteristic_scanList = pService->createCharacteristic(
      SCAN_LIST_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pCharacteristic_brightness = pService->createCharacteristic(
      BRIGHTNESS_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);

  pCharacteristic_ssid->setCallbacks(new ssidCallBack());
  pCharacteristic_password->setCallbacks(new passwordCallBack());
  pCharacteristic_scanState->setCallbacks(new scanStateCallBack());
  pCharacteristic_brightness->setCallbacks(new brightnessCallBack());

  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x0); // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");

  Serial.println("*****GET CREDS*****");
  preferences.begin("credentials", false);

  ssid = preferences.getString("ssid", "").c_str();
  password = preferences.getString("password", "").c_str();

  // Check if the preferences were retrieved properly and handle missing SSID
  if (ssid.length() == 0)
  {
    Serial.println("SSID not found, using default SSID.");
    ssid = "defaultSSID"; // Set a default SSID
  }

  if (password.length() == 0)
  {
    Serial.println("Password not found, using default Password.");
    password = "defaultPassword"; // Set a default Password
  }

  // Print lengths of ssid and password for debugging
  // Serial.print("Retrieved SSID length: ");
  // Serial.println(ssid.length());
  // Serial.print("Retrieved Password length: ");
  // Serial.println(password.length());

  Serial.println("*****GOT CREDS*****");
  Serial.println("SSID: " + ssid);
  Serial.println("Password: " + password);

  delay(500);
  Serial.println("*****Strip.begin*****");
  strip.begin();                   // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();                    // Turn OFF all pixels ASAP
  strip.setBrightness(brightness); // Set BRIGHTNESS to about 1/5 (max = 255)
  pCharacteristic_brightness->setValue(brightness);
  pCharacteristic_brightness->notify();
  Serial.println("*****SETUP END*****");
}

void loop()
{
  delay(100);
  Serial.println("*****LOOP*****");

  while(scanState == "scanning") {
    Serial.println("Scanning");
    delay(1000);
  }

  if (!isWiFiConnected())
  {
    pCharacteristic_confState->setValue("not_connected");
    pCharacteristic_confState->notify();
    attemptWiFiConnection();
    if (getNTPtime(10))
    {
      time(&now); // refresh after sync
      localtime_r(&now, &timeinfo);
      Serial.println("NTP synced after successfully connection to the internet");
    }
    else
    {
      Serial.println("NTP sync failed");
    }
  }

  pCharacteristic_confState->setValue("connected");
  pCharacteristic_confState->notify();

  time(&now);
  localtime_r(&now, &timeinfo);

  if (now != lastUpdate)
  {
    lastUpdate = now;

    updateTime(timeinfo);
    showTime();

    if (timeinfo.tm_min == 0 && timeinfo.tm_sec == 0)
    {
      if (getNTPtime(10))
      {
        time(&now); // refresh after sync
        localtime_r(&now, &timeinfo);
        Serial.println("NTP synced at top of hour");
      }
      else
      {
        Serial.println("NTP sync failed");
      }
      rainbow(60000);
    }
  }

  // disconnecting
  if (!deviceConnected && oldDeviceConnected)
  {
    delay(500);
    pServer->startAdvertising();
    Serial.println("start advertising");
    oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected)
  {
    oldDeviceConnected = deviceConnected;
  }
}
