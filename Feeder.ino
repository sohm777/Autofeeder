/*
  Скетч к проекту "Автокормушка Wi-Fi"
  - Создан на основе оригинального скетча AlexGyver
  - Страница проекта (схемы, описания): https://alexgyver.ru/gyverfeed2/
  - Исходники на GitHub: https://github.com/AlexGyver/GyverFeed2/
*/

// Click - extraordinary feeding
// Retention - set the portion size
const byte feedTime[][2] = {
  {23, 0},                  // hours, minutes. DO NOT START NUMBER FROM ZERO
  //  {12, 0},
  //  {17, 0},
  //  {21, 0},
};

#define EE_RESET 1          // any number 0-255. Change to reset settings and update time
#define FEED_SPEED 3000     // delay between motor steps (microseconds)
#define BTN_PIN 0           // button, pin D3
#define STEPS_FRW 18        // steps forward
#define STEPS_BKW 10        // steps back
#define EEPROM_SIZE 12

#define SCL 5               // D1
#define SDA 4               // D2

// driver - pins on the board (phaseА1 D5, phaseА2 D6, phaseВ1 D7, phaseВ2 D8)
const byte drvPins[] = {14, 12, 13, 15};

// =========================================================
#include "EncButton.h"
#include <EEPROM.h>
#include <RTClib.h>
RTC_DS3231 rtc;
#include <ESP8266WiFi.h>
#include <Wire.h>
#include <SPI.h>  // not used here, but needed to prevent a RTClib compile error

const char* ssid = "Your_SSID";
const char* password = "Your_PASS";

int EE_ADDR = 0;

WiFiServer web(80);                   // starts web on 80-port

String header;

EncButton<BTN_PIN> btn;
int feedAmount = 25;

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  WIFIinit();                         // start Wi-Fi
  Wire.begin();                       // I2C
  rtc.begin();
  int read_RESET;
  EEPROM.get(EE_ADDR, read_RESET);
  if (read_RESET != EE_RESET) {       // first start
    Serial.println("First start");
    EEPROM.put(EE_ADDR, EE_RESET);
    EE_ADDR += sizeof(EE_RESET);      // update address value
    EEPROM.put(EE_ADDR, feedAmount);
    EEPROM.commit();
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  } else {
    EE_ADDR += sizeof(EE_RESET);      // update address value
  }
  EEPROM.get(EE_ADDR, feedAmount);
  Serial.print("feedAmount: ");
  Serial.println(feedAmount);
  for (byte i = 0; i < 4; i++) pinMode(drvPins[i], OUTPUT);   // out pins
  web.begin();                        // start web
}

void loop() {
  // listening new clients
  WiFiClient client = web.available();
  if (client) {
    bool blank_line = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        header += c;
        if (c == '\n' && blank_line) {
          Serial.print(header);
          // 'user:pass' (admin:admin) base64 encode
          if (header.indexOf("YWRtaW46YWRtaW4=") >= 0) {
            // successful login
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/html");
            client.println("Connection: close");
            client.println();

            // button click handling on web
            if (header.indexOf("GET / HTTP/1.1") >= 0) {
              Serial.println("Main Web Page");
              // your main web page
              client.println("<!DOCTYPE HTML>");
              client.println("<html>");
              client.println("<head>");
              client.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
              client.println("<link rel=\"stylesheet\" href=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.4/css/bootstrap.min.css\">");
              client.println("</head><div class=\"container\">");
              client.println("<h1>Panda's feeder</h1>");
              client.println("<div class=\"row\">");
              client.println("<div class=\"col-md-2\"><a href=\"/feed\" class=\"btn btn-block btn-lg btn-success\" role=\"button\">FEED</a></div>");
              client.println("</div>");
              client.print("<h2>Dose: ");
              client.print(feedAmount);
              client.println("</h2>");
              client.println("</div></html>");
            }
            else if (header.indexOf("GET /feed HTTP/1.1") >= 0) {
              Serial.println("Feeding");
              feed();
              client.println("<!DOCTYPE HTML>");
              client.println("<html>");
              client.println("<head>");
              client.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
              client.println("<link rel=\"stylesheet\" href=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.4/css/bootstrap.min.css\">");
              client.println("</head><div class=\"container\">");
              client.println("<h1>The cat is fed!</h1>");
              client.println("<div class=\"row\">");
              client.println("<div class=\"col-md-2\"><a href=\"/\" class=\"btn btn-block btn-lg btn-success\" role=\"button\">Main Page</a></div>");
              client.println("</div>");
              client.println("</div></html>");
            }
          } else {
            // wrong log:pass
            client.println("HTTP/1.1 401 Unauthorized");
            client.println("WWW-Authenticate: Basic realm=\"Secure\"");
            client.println("Content-Type: text/html");
            client.println();
            client.println("<html>Authentication failed</html>");
          }
          header = "";
          break;
        }
        if (c == '\n') {
          // when starts reading a new line
          blank_line = true;
        } else if (c != '\r') {
          // when finds a character on the current line
          blank_line = false;
        }
      }
    }
    // closing the client connection
    delay(1);
    client.stop();
    Serial.println("Client disconnected.");
  }

  static uint32_t tmr = 0;
  if (millis() - tmr > 500) {          // twice per second
    static byte prevMin = 0;
    tmr = millis();
    DateTime now = rtc.now();
    if (prevMin != now.minute()) {
      prevMin = now.minute();
      for (byte i = 0; i < sizeof(feedTime) / 2; i++)    // for the whole schedule
        if (feedTime[i][0] == now.hour() && feedTime[i][1] == now.minute())    // time to feed
          feed();
    }
  }

  btn.tick();
  if (btn.isClick()) {
    Serial.println("Start engine with BTN");
    feed();
  }
  if (btn.isHold()) {
    int newAmount = 0;
    while (btn.isHold()) {
      btn.tick();
      oneRev();
      newAmount++;
    }
    disableMotor();
    feedAmount = newAmount;
    EEPROM.put(EE_ADDR, feedAmount);
    EEPROM.commit();
  }
}

void feed() {
  for (int i = 0; i < feedAmount; i++) oneRev();      // rotate the engine by feedAmount
  disableMotor();
}

void disableMotor() {
  for (byte i = 0; i < 4; i++) digitalWrite(drvPins[i], 0); // turn off the power to the engine
}

void oneRev() {
  yield();              // to avoid soft WDT reset
  static byte val = 0;
  for (byte i = 0; i < STEPS_BKW; i++) runMotor(val--);
  for (byte i = 0; i < STEPS_FRW; i++) runMotor(val++);
}

void runMotor(byte thisStep) {
  static const byte steps[] = {0b1010, 0b0110, 0b0101, 0b1001};
  for (byte i = 0; i < 4; i++)
    digitalWrite(drvPins[i], bitRead(steps[thisStep & 0b11], i));
  delayMicroseconds(FEED_SPEED);
}

void WIFIinit() {
  byte tries = 11;
  Serial.println();
  Serial.printf("Connecting to %s ", ssid);
  WiFi.begin(ssid, password);
  while (--tries && WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Not connected");
  } else {
    Serial.print("Connected. IP: ");
    Serial.println(WiFi.localIP());
  }
  // Change Station Hostname
  WiFi.hostname("Feeder");
}
