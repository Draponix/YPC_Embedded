#include <SPI.h>
#include <MFRC522.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <FastLED.h>
#include "time.h"
#include <esp_sleep.h>

#define uS_TO_S_FACTOR 1000000 /* Conversion factor for microseconds to seconds */
#define TIME_TO_SLEEP 43200    /* Time in seconds (12 hours) */
#define SS_PIN 5               // Define the SS pin for the MFRC522
#define RST_PIN 2              // Define the RST pin for the MFRC522
#define DATA_PIN 16            // Define the Neopixel data pin
#define NUM_LEDS 32            // Number of Neopixel LEDs
#define BRIGHTNESS 255         // Neopixel brightness
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define CLOSING_HOUR 20

MFRC522 rfid(SS_PIN, RST_PIN);
WiFiMulti wifiMulti;
HTTPClient http;
CRGB leds[NUM_LEDS];
String cardID, postData;

const char* ssid1 = "Zyxel_A221";
const char* password1 = "HKYMNJ7F4J";
const char* ssid2 = "BT-8CCJKS";
const char* password2 = "TYrUXrXLUm6cuY";
const char* actionAddress = "http://192.168.1.181/sys/dbwrite.php";
const char* outageAddress = "http://192.168.1.181/sys/dbout.php";
const char* schedulerAddress = "http://192.168.1.181/sys/ResetScheduler.php";

volatile bool accessGranted = false;
volatile int timeCheck = 0;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS)
    .setCorrection(TypicalLEDStrip)
    .setDither(BRIGHTNESS < 255);

  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);

  // Add multiple WiFi networks for redundancy
  wifiMulti.addAP(ssid1, password1);
  wifiMulti.addAP(ssid2, password2);
  Serial.println("Connecting to Wifi...");
  if (wifiMulti.run(1000) == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Failed to connect to WiFi.");
  }

  setAll(CRGB::DarkBlue);  // Set Neopixels to initial state (blue)
  FastLED.show();          // Initialize Neopixels

  enableScheduler();

  initTime("Europe/London");
}

void loop() {
  // Connect to WiFi networks
  if (timeCheck == 60) {
    timeCheck = 0;
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      int currentHour = timeinfo.tm_hour;
      int currentMinute = timeinfo.tm_min;
      int isDST = timeinfo.tm_isdst;  // Check if DST is in effect

      if (isDST) {
        Serial.println(" (DST in effect)");
      } else {
        Serial.println(" (Standard Time)");
        currentHour += 1;
      }

      if (currentHour >= CLOSING_HOUR) {
        signOutAll();
        setAll(CRGB::DarkMagenta);
        delay(1000);
        setAll(CRGB::Black);
        delay(1000);
        setAll(CRGB::DarkMagenta);
        delay(1000);
        setAll(CRGB::Black);
        delay(1000);
        setAll(CRGB::DarkMagenta);
        delay(1000);
        setAll(CRGB::Black);
        esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
        esp_deep_sleep_start();
      }

    } else {
      Serial.println("Failed to obtain the time.");
    }
  } else {
    timeCheck++;
    Serial.println(timeCheck);
  }


  if (wifiMulti.run() == WL_CONNECTED) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      cardID = "";
      for (byte i = 0; i < rfid.uid.size; i++) {
        cardID.concat(String(rfid.uid.uidByte[i] < 0x10 ? "0" : ""));
        cardID.concat(String(rfid.uid.uidByte[i], HEX));
      }
      Serial.println(cardID);
      rfid.PICC_HaltA();       // halt PICC
      rfid.PCD_StopCrypto1();  // stop encryption on PCD

      if (sendHTTPPostRequest(cardID)) {
        Serial.println("Access granted.");
        setAll(CRGB::Green);  // Set Neopixels to green for access granted
        accessGranted = true;
      } else {
        Serial.println("Access denied.");
        setAll(CRGB::Red);  // Set Neopixels to red for access denied
        accessGranted = false;
      }
      FastLED.show();
      delay(3000);  // Delay to prevent multiple scans from the same card.
      resetLEDs();  // Reset Neopixels to blue or initial state
    }
  }
  //printCurrentTime();
  delay(500);
}

bool sendHTTPPostRequest(String cardID) {
  WiFiClient client;
  HTTPClient http;

  if (http.begin(client, actionAddress)) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String postData = "cardID=" + cardID;
    int httpCode = http.POST(postData);

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println("Server Response: " + payload);
      http.end();
      return true;
    } else {
      Serial.println("HTTP POST request failed with code: " + httpCode);
      http.end();
    }
  } else {
    Serial.println("Connection to the server failed.");
  }

  return false;
}

void setAll(CRGB color) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = color;
  }
  FastLED.show();
}

void resetLEDs() {
  setAll(CRGB::Blue);
  FastLED.show();
}

void signOutAll() {
  WiFiClient wifiClient;
  HTTPClient http;

  http.begin(wifiClient, outageAddress);                                // Connect to host where MySQL databse is hosted
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");  // Specify content-type header
  int httpCode = http.POST("A");                                        // Send POST request to php file and store server response code in variable named httpCode
  // if connection eatablished then do this
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
  } else {
    Serial.println(httpCode);
    Serial.println("Failed to upload values.");
    http.end();
  }
}

void enableScheduler() {

  WiFiClient wifiClient;
  HTTPClient http;

  http.begin(wifiClient, "http://192.168.1.181/sys/ResetScheduler.php");  // Connect to host where MySQL databse is hosted
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");    // Specify content-type header
  int httpCode = http.POST("A");
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println(payload);
    http.end();
    return;
  } else {
    Serial.println(httpCode);
    Serial.println("Failed to upload values.");
    http.end();
    return;
  }
}

void printCurrentTime() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    int currentHour = timeinfo.tm_hour;
    int currentMinute = timeinfo.tm_min;
    int isDST = timeinfo.tm_isdst;  // Check if DST is in effect

    if (isDST) {
      Serial.println(" (DST in effect)");
    } else {
      Serial.println(" (Standard Time)");
      currentHour += 1;
    }

    Serial.print("Current time: ");
    Serial.print(currentHour);
    Serial.print(" HRS ");
    Serial.print(currentMinute);
    Serial.print(" MINS");

  } else {
    Serial.println("Failed to obtain the time.");
  }
}

void initTime(const char* timezone) {
  Serial.printf("Setting up time for timezone: %s\n", timezone);

  configTime(0, 0, "pool.ntp.org");  // First connect to NTP server, with 0 TZ offset

  // Optional: Set up the timezone rules for daylight saving time (DST) to "auto" for automatic DST handling
  setenv("TZ", "auto", 1);
  tzset();

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    Serial.println("Got the time from NTP.");
  } else {
    Serial.println("Failed to obtain time.");
  }
}
