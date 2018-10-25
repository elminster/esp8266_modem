

/*
   ESP8266 based virtual modem
   Original Source Copyright (C) 2016 Jussi Salin <salinjus@gmail.com>
   Additions (C) 2018 Daniel Jameson, Stardot Contributors

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/****** Elminster WiFiManager Dev Verson ******/

#include <FS.h> //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>
#include <algorithm>

//Added for WifiManager https://github.com/tzapu/WiFiManager
#include <WiFiManager.h>
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson

//for LED status
#include <Ticker.h>

#include "Gsender.h"    // Include header with email config

//#defines

#define USE_HW_FLOW_CTRL   // Use hardware (RTS/CTS) flow control - controlled by HW_FLOW_SELECT GPIO being pulled LOW.
//#undef USE_HW_FLOW_CTRL      // comment out line above to disable HW flow and uncomment this one.
#ifdef USE_HW_FLOW_CTRL
#include <uart_register.h>
#endif

//#define DEBUG 1          // Print additional debug information to serial channel
#undef DEBUG
#define DEFAULT_BPS 2400   // 2400 safe for all old computers including C64
#define LISTEN_PORT 23     // Listen to this if not connected. Set to zero to disable.
#define WEB_PORT 80        // Port for web server
#define RING_INTERVAL 3000 // How often to print RING when having a new incoming connection (ms)
#define MAX_CMD_LENGTH 256 // Maximum length for AT command
#define LED_PIN 2          // Status LED
#define LED_TIME 1         // How many ms to keep LED on at activity
#define TX_BUF_SIZE 256    // Buffer where to read from serial before writing to TCP
// (that direction is very blocking by the ESP TCP stack,
// so we can't do one byte a time.)
// Telnet codes
#define DO 0xfd
#define WONT 0xfc
#define WILL 0xfb
#define DONT 0xfe

//ESP8266 pins
#define HW_FLOW_SELECT 16 //high=off, low=on
#define ESP_RTS 15 //output
#define ESP_CTS 13 //input
#define ESP_RING 12 //output
#define ESP_DSR 4  //output
#define ESP_DTR 14 //input
#define ESP_DCD 5  //output



// Global variables

WiFiClient tcpClient;
WiFiClient webClient;
WiFiServer tcpServer(LISTEN_PORT);
WiFiServer webServer(WEB_PORT);

String cmd = "";                // Gather a new AT command to this string from serial
bool cmdMode = true;            // Are we in AT command mode or connected mode
bool dacomMode = false;         // Are we in DACOM compatible mode?
bool telnet = true;             // Is telnet control code handling enabled
bool dacomAutoAnswer = false;   // are we looking to auto-answer in dacom mode?
bool shouldSaveConfig = false;  //flag for saving data

unsigned long lastRingMs = 0;   // Time of last "RING" message (millis())
long myBps;                     // What is the current BPS setting
char plusCount = 0;             // Go to AT mode at "+++" sequence, that has to be counted
char ctrlACount = 0;            // Go to DaCom command mode with 4xCTRL-A
//char defurl[40] = "GLASSTTY.COM:6503";  //Default site to login to
char defurl[40] = "192.168.1.93:23";  //Default site to login to
unsigned long plusTime = 0;     // When did we last receive a "+++" sequence
unsigned long ctrlATime = 0;    //When did we last receive a 4xCTRL-A sequence?
unsigned long ledTime = 0;      // Counter for LED flashing
uint8_t txBuf[TX_BUF_SIZE];     // Transmit Buffer

int hwFlowOff = 0;


Ticker ticker; //for LED status

/**
   Arduino main init function
*/
void setup()
{
  Serial.begin(DEFAULT_BPS);
  myBps = DEFAULT_BPS;
  pinMode(HW_FLOW_SELECT, INPUT);
  pinMode(ESP_RING, OUTPUT);
  pinMode(ESP_DCD, OUTPUT);
  pinMode(ESP_DSR, OUTPUT);
  pinMode(ESP_DTR, INPUT);
  hwFlowOff = digitalRead(HW_FLOW_SELECT);
#ifdef USE_HW_FLOW_CTRL
  if (hwFlowOff == 0) {
    setHardwareFlow();
  }
#endif

 //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");
  mountMyFlash();
  
  // id/name, placeholder/prompt, default, length
  WiFiManagerParameter default_url("defurl", "default url", defurl, 20);

  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&default_url);
  
  helpMessage();

  digitalWrite(ESP_RING, HIGH); // Phone not ringing
  digitalWrite(ESP_DCD, HIGH);  // We're not connected to an external host
  digitalWrite(ESP_DSR, LOW);   // We are happy to talk though!

  if (LISTEN_PORT > 0)
  {
    Serial.print("Listening to Telnet connections at port ");
    Serial.print(LISTEN_PORT);
    Serial.println(", which result in RING and you can answer with ATA.");
    tcpServer.begin();
  }
  else
  {
    Serial.println("Incoming Telnet connections are disabled.");
  }

  if (LISTEN_PORT > 0)
  {
    Serial.print("Listening for Web connections on port ");
    Serial.println(WEB_PORT);    
    webServer.begin();
  }
  else
  {
    Serial.println("Incoming Web connections are disabled.");
  }
  
  Serial.println("");
  Serial.println("OK");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  wifiManager.setConfigPortalTimeout(180);
  if(!wifiManager.autoConnect("APFreeFi232")) {
    Serial.println("failed to connect and hit timeout");
    Serial.println("config from terminal with ATWIFI command");
  } else {
    //if you get here you have connected to the WiFi
    Serial.println("connected to network ... ");

    cmd="ATDEFURL\n";  //call default stored URL
    command();

    Serial.println("Emailing IP Address ....");
    Gsender *gsender = Gsender::Instance();    // Getting pointer to class instance
    String subject = "FreeFi232 IP ADDR: ";
    String myip = WiFi.localIP().toString();
    subject.concat(myip);
     
    if(gsender->Subject(subject)->Send("duncan@elminster.com", "Setup test")) {
        Serial.println("Message sent.");
    } else {
        Serial.print("Error sending message: ");
        Serial.println(gsender->getError());
    }
    

  
  }

  ticker.detach();
  //Turn LED off
  digitalWrite(LED_PIN, HIGH);

  if (shouldSaveConfig) {
    saveFlashConfig();
  }

   strcpy(defurl, default_url.getValue());  //set variable to stored value
    
}

void setHardwareFlow() {
  // Enable flow control of DTE -> ESP8266 data with RTS
  // RTS on the EPS8266 is pin GPIO15 which is physical pin 16
  // RTS on the ESP8266 is an output and should be connected to CTS on the RS423
  // The ESP8266 has a 128 byte receive buffer, so a threshold of 64 is half full
  pinMode(ESP_RTS, FUNCTION_4); // make pin U0RTS
  SET_PERI_REG_BITS(UART_CONF1(0), UART_RX_FLOW_THRHD, 64, UART_RX_FLOW_THRHD_S);
  SET_PERI_REG_MASK(UART_CONF1(0), UART_RX_FLOW_EN);

  // Enable flow control of ESP8266 -> DTE data with CTS
  // CTS on the EPS8266 is pin GPIO13 which is physical pin 7
  // CTS on the ESP8266 is an input and should be connected to RTS on the RS423
  pinMode(ESP_CTS, FUNCTION_4); // make pin U0CTS
  SET_PERI_REG_MASK(UART_CONF0(0), UART_TX_FLOW_EN);
}


void helpMessage()
{
  Serial.println("FreeFi232 Firmware v0.2 (WiFiManager Edition)");
  Serial.println("=============================================\n");
  Serial.println("Based on ESP8266 Virtual Modem (C) 2016 Jussi Salin");
  Serial.println("Additions (C) 2018 Daniel Jameson and Stardot Contributors");
  Serial.println("Connect to WIFI: ATWIFI<ssid>,<key>");
  Serial.println("Scan for Available Networks: ATSCAN");
  Serial.println("Change terminal baud rate: AT<baud>");
  Serial.println("Connect by TCP: ATDT<host>:<port>");
  Serial.println("See my IP address: ATIP");  
  Serial.println("See my Network: ATMYNET");
  Serial.println("Disable telnet command handling: ATNET0");
  Serial.println("HTTP GET: ATGET<URL>");
  Serial.print("MAC:");
  Serial.println(WiFi.macAddress());
  Serial.print("Hardware Flow control: ");
  if (hwFlowOff == 0) {
    Serial.println("ON");
  } else {
    Serial.println("OFF");
  }
  Serial.print("Default URL: ");
  Serial.println(defurl);
  Serial.println();

}

/**
   Turn on the LED and store the time, so the LED will be shortly after turned off
*/
void led_on(void)
{
  digitalWrite(LED_PIN, LOW);
  ledTime = millis();
}

/**
   Arduino main loop function
*/
void loop()
{
 
  /**** AT command mode ****/
  if (cmdMode == true)
  {
    // In command mode but new unanswered incoming connection on server listen socket
    if ((LISTEN_PORT > 0) && (tcpServer.hasClient()))
    {
      // Print RING every now and then while the new incoming connection exists
      if ((millis() - lastRingMs) > RING_INTERVAL)
      {
        if (dacomMode) {
          Serial.println("RINGING");
        }
        else {
          Serial.println("RING");
        }
        digitalWrite(ESP_RING, LOW); //Someone's trying to call
        lastRingMs = millis();
        if (dacomAutoAnswer) {
          dacomAnswer(); // answer it if in auto answer mode for dacom
        }
      }
    } else {
      digitalWrite(ESP_RING, HIGH); //Someone's not trying to call
    }

    if (WEB_PORT > 0) { webClient = webServer.available(); } //Listen for Web Connections

    if (webClient) 
    { 
      call_webserver();
      webClient.stop();
      Serial.println("Client disconnected.");
      Serial.println("");
     } 
    
    // In command mode - don't exchange with TCP but gather characters to a string
    if (Serial.available())
    {
      char chr = Serial.read();

      // Return, enter, new line, carriage return.. anything goes to end the command
      if ((chr == '\n') || (chr == '\r'))
      {
        if (dacomMode) {
          dacomCommand();
        } else {
          command();
        }
      }
      // Backspace or delete deletes previous character
      else if ((chr == 8) || (chr == 127))
      {
        cmd.remove(cmd.length() - 1);
        // We don't assume that backspace is destructive
        // Clear with a space
        Serial.write(8);
        Serial.write(' ');
        Serial.write(8);
      }
      else
      {
        if (cmd.length() < MAX_CMD_LENGTH) cmd.concat(chr);
        Serial.print(chr);
      }
    }
  }
  /**** Connected mode ****/
  else
  {
    // Transmit from terminal to TCP
    if (Serial.available())
    {
      led_on();

      // In telnet in worst case we have to escape every byte
      // so leave half of the buffer always free
      int max_buf_size;
      if (telnet == true)
        max_buf_size = TX_BUF_SIZE / 2;
      else
        max_buf_size = TX_BUF_SIZE;

      // Read from serial, the amount available up to
      // maximum size of the buffer
      size_t len = std::min(Serial.available(), max_buf_size);
      Serial.readBytes(&txBuf[0], len);

      // Disconnect if going to AT mode with "+++" sequence or DaCom mode with CTRL-A sequence
      for (int i = 0; i < (int)len; i++)
      {
        if (txBuf[i] == '+') plusCount++; else plusCount = 0;
        if (txBuf[i] == 1) ctrlACount++; else ctrlACount = 0;
        if (plusCount >= 3) plusTime = millis();
        if (ctrlACount >= 4) ctrlATime = millis();
        if (txBuf[i] != '+') plusCount = 0;
        if (txBuf[i] != 1) ctrlACount = 0;
      }


      // Double (escape) every 0xff for telnet, shifting the following bytes
      // towards the end of the buffer from that point
      if (telnet == true)
      {
        for (int i = len - 1; i >= 0; i--)
        {
          if (txBuf[i] == 0xff)
          {
            for (int j = TX_BUF_SIZE - 1; j > i; j--)
            {
              txBuf[j] = txBuf[j - 1];
            }
            len++;
          }
        }
      }

      // Write the buffer to TCP finally
      tcpClient.write(&txBuf[0], len);
      yield();
    }

    // Transmit from TCP to terminal
    while (tcpClient.available())
    {
      led_on();
      uint8_t rxByte = tcpClient.read();

      // Is a telnet control code starting?
      if ((telnet == true) && (rxByte == 0xff))
      {
#ifdef DEBUG
        Serial.print("<t>");
#endif
        rxByte = tcpClient.read();
        if (rxByte == 0xff)
        {
          // 2 times 0xff is just an escaped real 0xff
          Serial.write(0xff); Serial.flush();
        }
        else
        {
          // rxByte has now the first byte of the actual non-escaped control code
#ifdef DEBUG
          Serial.print(rxByte);
          Serial.print(",");
#endif
          uint8_t cmdByte1 = rxByte;
          rxByte = tcpClient.read();
          uint8_t cmdByte2 = rxByte;
          // rxByte has now the second byte of the actual non-escaped control code
#ifdef DEBUG
          Serial.print(rxByte); Serial.flush();
#endif
          // We are asked to do some option, respond we won't
          if (cmdByte1 == DO)
          {
            tcpClient.write((uint8_t)255); tcpClient.write((uint8_t)WONT); tcpClient.write(cmdByte2);
          }
          // Server wants to do any option, allow it
          else if (cmdByte1 == WILL)
          {
            tcpClient.write((uint8_t)255); tcpClient.write((uint8_t)DO); tcpClient.write(cmdByte2);
          }
        }
#ifdef DEBUG
        Serial.print("</t>");
#endif
      }
      else
      {
        // Non-control codes pass through freely
        Serial.write(rxByte);
        yield();
        Serial.flush();
      }
    }
  }


  // If we have received "+++" as last bytes from serial port and there
  // has been over a second without any more bytes, disconnect
  if (plusCount >= 3 && !dacomMode)
  {
    if (millis() - plusTime > 1000)
    {
      tcpClient.stop();
      plusCount = 0;
    }
  }

  if (ctrlACount >= 4 && dacomMode)
  {
    if (millis() - ctrlATime > 1000)
    {
      tcpClient.stop();
      ctrlACount = 0;
    }
  }

  // Go to command mode if TCP disconnected and not in command mode
  if ((!tcpClient.connected()) && (cmdMode == false))
  {
    cmdMode = true;
    if (dacomMode) {
      Serial.println("DISCONNECTED");
    } else {
      Serial.println("NO CARRIER");
    }
    digitalWrite(ESP_DCD, HIGH); //We're disconnected from carrier
    if (LISTEN_PORT > 0) tcpServer.begin();
  }

  // Turn off tx/rx led if it has been lit long enough to be visible
  if (millis() - ledTime > LED_TIME) digitalWrite(LED_PIN, HIGH);
}
