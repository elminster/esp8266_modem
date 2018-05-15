/*
   ESP8266 based virtual modem
   Copyright (C) 2016 Jussi Salin <salinjus@gmail.com>
   Copyright (C) 2018 Daniel Jameson <djameson@gmail.com>

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


/**** Dial to host ****/
void dialHost(String cmd)
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
      Serial.println(myBps);
      cmdMode = false;
      Serial.flush();
      if (LISTEN_PORT > 0) tcpServer.stop();
    }
    else
    {
      Serial.println("NO CARRIER");
    }
    delete hostChr;
  return;
}

  
/**** Connect to WIFI ****/
void wifiConnect(String cmd) 
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
    return;
}

int changeBaud(String cmd)
{
  int newBps = DEFAULT_BPS;
  String bpsString = cmd.substring(cmd.indexOf("B")+1, cmd.length());
  if (bpsString == "300") newBps = 300;
  else if (bpsString == "1200") newBps = 1200;
  else if (bpsString == "2400") newBps = 2400;
  else if (bpsString == "9600") newBps = 9600;
  else if (bpsString == "19200") newBps = 19200;
  else if (bpsString == "38400") newBps = 38400;
  else if (bpsString == "57600") newBps = 57600;
  else if (bpsString == "115200") newBps = 115200;
  return newBps;
}

