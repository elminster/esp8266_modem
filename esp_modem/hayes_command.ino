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

/**
   Perform a command given in command mode
*/
void command()
{
  cmd.trim();
  if (cmd == "") return;
  Serial.println();
  String upCmd = cmd;
  upCmd.toUpperCase();

  long newBps = 0;

  /**** Just AT ****/
  if (upCmd == "AT") Serial.println("OK");

  /**** Help message ****/
  else if (upCmd == "ATHELP") helpMessage();

  /**** Dial to host ****/
  else if ((upCmd.indexOf("ATDT") == 0) || (upCmd.indexOf("ATDP") == 0) || (upCmd.indexOf("ATDI") == 0))
  {
    int portIndex = cmd.indexOf(":");
    String host, port;
    if (portIndex != -1)
    {
      host = cmd.substring(4, portIndex);
      port = cmd.substring(portIndex + 1, cmd.length());
    }
    else
    {
      host = cmd.substring(4, cmd.length());
      port = "23"; // Telnet default
    }
    Serial.print("Connecting to ");
    Serial.print(host);
    Serial.print(":");
    Serial.println(port);
    char *hostChr = new char[host.length() + 1];
    host.toCharArray(hostChr, host.length() + 1);
    int portInt = port.toInt();
    tcpClient.setNoDelay(true); // Try to disable naggle
    if (tcpClient.connect(hostChr, portInt))
    {
      tcpClient.setNoDelay(true); // Try to disable naggle
      Serial.print("CONNECT ");
      digitalWrite(ESP_DCD, LOW);
      Serial.println(myBps);
      cmdMode = false;
      Serial.flush();
      if (LISTEN_PORT > 0) tcpServer.stop();
    }
    else
    {
      Serial.println("NO CARRIER");
      digitalWrite(ESP_DCD, HIGH);
    }
    delete hostChr;
  }

  /**** Connect to WIFI ****/
  else if (upCmd.indexOf("ATWIFI") == 0)
  {
    int keyIndex = cmd.indexOf(",");
    String ssid, key;
    if (keyIndex != -1)
    {
      ssid = cmd.substring(6, keyIndex);
      key = cmd.substring(keyIndex + 1, cmd.length());
    }
    else
    {
      ssid = cmd.substring(6, cmd.length());
      key = "";
    }
    char *ssidChr = new char[ssid.length() + 1];
    ssid.toCharArray(ssidChr, ssid.length() + 1);
    char *keyChr = new char[key.length() + 1];
    key.toCharArray(keyChr, key.length() + 1);
    Serial.print("Connecting to ");
    Serial.print(ssid);
    Serial.print("/");
    Serial.println(key);
    WiFi.begin(ssidChr, keyChr);
    for (int i = 0; i < 100; i++)
    {
      delay(100);
      if (WiFi.status() == WL_CONNECTED)
      {
        Serial.println("OK");
        break;
      }
    }
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("ERROR");
    }
    delete ssidChr;
    delete keyChr;
  }

  /**** Change baud rate from default ****/
  else if (upCmd == "AT300") newBps = 300;
  else if (upCmd == "AT1200") newBps = 1200;
  else if (upCmd == "AT2400") newBps = 2400;
  else if (upCmd == "AT9600") newBps = 9600;
  else if (upCmd == "AT19200") newBps = 19200;
  else if (upCmd == "AT38400") newBps = 38400;
  else if (upCmd == "AT57600") newBps = 57600;
  else if (upCmd == "AT115200") newBps = 115200;

  /**** Change telnet mode ****/
  else if (upCmd == "ATNET0")
  {
    telnet = false;
    Serial.println("OK");
  }
  else if (upCmd == "ATNET1")
  {
    telnet = true;
    Serial.println("OK");
  }

  /**** Answer to incoming connection ****/
  else if ((upCmd == "ATA") && tcpServer.hasClient())
  {
    tcpClient = tcpServer.available();
    tcpClient.setNoDelay(true); // try to disable naggle
    tcpServer.stop();
    digitalWrite(ESP_RING, HIGH); // we've picked up so ringing stops
    Serial.print("CONNECT ");
    digitalWrite(ESP_DCD, LOW); // we've got a carrier signal
    Serial.println(myBps);
    cmdMode = false;
    Serial.flush();
  }

  /**** See my IP address ****/
  else if (upCmd == "ATIP")
  {
    Serial.print("IP:");
    Serial.println(WiFi.localIP());
    Serial.print("MAC:");
    Serial.println(WiFi.macAddress());
    Serial.println("OK");
  }

  /**** See my Network ****/
  else if (upCmd == "ATNET")
  {
    // print the SSID of the network you're attached to:
    // From https://www.arduino.cc/en/Tutorial/ConnectWithWPA
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());

    // print the MAC address of the router you're attached to:
    Serial.println(WiFi.BSSIDstr());
    
    // print the received signal strength:
    long rssi = WiFi.RSSI();
    Serial.print("signal strength (RSSI):");
    Serial.println(rssi);

    Serial.println("OK");
  }

    /**** See my IP address ****/
  else if (upCmd == "ATSCAN")
  {
    // From https://www.arduino.cc/en/Tutorial/ScanNetworks
    Serial.println("** Scan Networks **");
    byte numSsid = WiFi.scanNetworks();
    if (numSsid == -1) {
      Serial.println("Couldn't get a wifi connection");
      while (true);
    }
  
    Serial.print("SSID List:");
    Serial.println(numSsid);

      // print the network number and name for each network found:
    for (int thisNet = 0; thisNet < numSsid; thisNet++) {
      Serial.print(thisNet);
      Serial.print(") ");
      Serial.print(WiFi.SSID(thisNet));
      Serial.print("\tSignal: ");
      Serial.print(WiFi.RSSI(thisNet));
      Serial.print(" dBm");
      Serial.print("\tEncryption: ");
      printEncryptionType(WiFi.encryptionType(thisNet));
    }
  
    Serial.println("OK");
  }
  
  /**** HTTP GET request ****/
  else if (upCmd.indexOf("ATGET") == 0)
  {
    // From the URL, aquire required variables
    // (12 = "ATGEThttp://")
    int portIndex = cmd.indexOf(":", 12); // Index where port number might begin
    int pathIndex = cmd.indexOf("/", 12); // Index first host name and possible port ends and path begins
    int port;
    String path, host;
    if (pathIndex < 0)
    {
      pathIndex = cmd.length();
    }
    if (portIndex < 0)
    {
      port = 80;
      portIndex = pathIndex;
    }
    else
    {
      port = cmd.substring(portIndex + 1, pathIndex).toInt();
    }
    host = cmd.substring(12, portIndex);
    path = cmd.substring(pathIndex, cmd.length());
    if (path == "") path = "/";
    char *hostChr = new char[host.length() + 1];
    host.toCharArray(hostChr, host.length() + 1);

    // Debug
    Serial.print("Getting path ");
    Serial.print(path);
    Serial.print(" from port ");
    Serial.print(port);
    Serial.print(" of host ");
    Serial.print(host);
    Serial.println("...");

    // Establish connection
    if (!tcpClient.connect(hostChr, port))
    {
      Serial.println("NO CARRIER");
      digitalWrite(ESP_DCD, 1);
    }
    else
    {
      Serial.print("CONNECT ");
      digitalWrite(ESP_DCD, 0);
      Serial.println(myBps);
      cmdMode = false;

      // Send a HTTP request before continuing the connection as usual
      String request = "GET ";
      request += path;
      request += " HTTP/1.1\r\nHost: ";
      request += host;
      request += "\r\nConnection: close\r\n\r\n";
      tcpClient.print(request);
    }
    delete hostChr;
  }

  /**** Place modem in DACOM mode ****/
  else if (upCmd.indexOf("ATDACOM") == 0) {
    dacomMode=true;
    Serial.println("DaCom Mode Activated - changing to 1200-8N1");
    newBps=1200;
  }

  /**** Print help mesage ****/
  else if (upCmd.indexOf("ATHELP") == 0) {
    helpMessage();
  }

  /**** Unknown command ****/
  else Serial.println("ERROR");

  /**** Tasks to do after command has been parsed ****/
  if (newBps)
  {
    Serial.println("OK");
    delay(150); // Sleep enough for 4 bytes at any previous baud rate to finish ("\nOK\n")
    Serial.begin(newBps);
#ifdef USE_HW_FLOW_CTRL
    if (hwFlowOff == 0) {
      setHardwareFlow();
    }
#endif
    myBps = newBps;
  }
  cmd = "";
}

void printEncryptionType(int thisType) {
  // read the encryption type and print out the name:
  switch (thisType) {
    case ENC_TYPE_WEP:
      Serial.println("WEP");
      break;
    case ENC_TYPE_TKIP:
      Serial.println("WPA");
      break;
    case ENC_TYPE_CCMP:
      Serial.println("WPA2");
      break;
    case ENC_TYPE_NONE:
      Serial.println("None");
      break;
    case ENC_TYPE_AUTO:
      Serial.println("Auto");
      break;
  }

}
