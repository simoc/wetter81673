/*
 * Copyright 2012 Simon Chenery
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <SPI.h>
#include <Ethernet.h>

/*
 * Sensor pins.
 */
#define SLEEP_SENSOR_PIN 2
#define RADIO_PIN 12

/*
 * Output pins.
 */
#define LED_PIN 13

EthernetClient ethernetClient;

void setup()
{
  /*
   * IP address for my network.
   */
  byte mac[] = {0x63, 0x5A, 0xA7, 0x80, 0xB1, 0x3F};
  IPAddress ip(192, 168, 2, 223);

  Ethernet.begin(mac, ip);

  pinMode(LED_PIN, OUTPUT);
  pinMode(RADIO_PIN, INPUT);

  /*
   * Ethernet needs some time to setup.
   */
  delay(3000);
}

byte headerBits[11] = {1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1};
byte dataBits[2 + 10 + 4];

int ledValue = LOW;
int ledCounter = 0;

int lowSampleCount = 0;
int highSampleCount = 0;

int bitReadCount = 0;

bool isReadingHeader = true;

int noDataCounter = 0;

String httpData;

#define APIKEY         "000000000000000000000000000000000000000000000000" // your cosm api key
#define FEEDID         75941 // your feed ID
#define USERAGENT      "Cosm Arduino Example (75941)" // user agent is the project name

void uploadData(String message)
{
  char serverName[] = "api.cosm.com";

  if (ethernetClient.connect(serverName, 80))
  {
   
    /*
     * Send the HTTP PUT request.
     */
    ethernetClient.print("PUT /v2/feeds/");
    ethernetClient.print(FEEDID);
    ethernetClient.println(".csv HTTP/1.1");
    ethernetClient.println("Host: api.cosm.com");
    ethernetClient.print("X-ApiKey: ");
    ethernetClient.println(APIKEY);
    ethernetClient.print("User-Agent: ");
    ethernetClient.println(USERAGENT);
    ethernetClient.print("Content-Length: ");
    ethernetClient.println(message.length());
    ethernetClient.println("Content-Type: text/csv");
    ethernetClient.println("Connection: close");
    ethernetClient.println();
    /*
     * Add data to end of PUT request.
     */
    ethernetClient.println(message);
    ethernetClient.flush();

    /*
     * Throw away any response to HTTP request.
     */
    while (ethernetClient.available()) {
      char c = ethernetClient.read();
    }
  }
  ethernetClient.stop();
}

void loop()
{
  ledValue = LOW;

  /*
   * Each bit of message is sent for 10ms.  Sample each bit 10 times.
   */
  delay(1);
  int sample = digitalRead(RADIO_PIN);

  if (sample == HIGH)
  {
    lowSampleCount = 0;
    highSampleCount++;
    if (highSampleCount > 8)
    {
      if (isReadingHeader && headerBits[bitReadCount] == 1)
      {
        bitReadCount++;
        highSampleCount = 0;
      }
      else if (isReadingHeader)
      {
        /*
         * We were not expecting a HIGH bit.
         * Start reading again right from the beginning.
         */
        highSampleCount = 0;
        bitReadCount = 0;
      }
      else
      {
        /*
         * We have read a complete 1 data bit.  Save it.
         */
        dataBits[bitReadCount] = 1;
        bitReadCount++;
        highSampleCount = 0;
      }
    }
  }
  else /* LOW */
  {  
    highSampleCount = 0;
    lowSampleCount++;
    if (lowSampleCount > 8)
    {
      if (isReadingHeader && headerBits[bitReadCount] == 0)
      {
        bitReadCount++;
        lowSampleCount = 0;
      }
      else if (isReadingHeader)
      {
        /*
         * We were not expecting a LOW bit.
         * Start reading again right from the beginning.
         */
        lowSampleCount = 0;
        bitReadCount = 0;
      }
      else
      {
        /*
         * We have read a complete 0 data bit.  Save it.
         */
        dataBits[bitReadCount] = 0;
        bitReadCount++;
        lowSampleCount = 0;
      }
    }
  }

  if (isReadingHeader && bitReadCount == sizeof(headerBits))
  {
    /*
     * Successfully read the header bits, now setup to read the message bits.
     */
    isReadingHeader = false;
    ledValue = HIGH;
    bitReadCount = 0;
    lowSampleCount = highSampleCount = 0;
  }
  else if ((!isReadingHeader) && bitReadCount == sizeof(dataBits))
  {
    /*
     * We have finished reading a complete message.
     */
    int dataValue = 0;
    int dataType = 0;

    for (int i = 0; i < sizeof(dataBits); i++)
    {
      int dataBit = dataBits[i];

      if (i == 0 || i == 1)
      {
        /*
         * First two bits are the message type.
         */
        dataType = (dataType << 1);
        dataType = dataType | dataBit;
      }
      else if (!(i == 4 || i == 7 || i == 10 || i == 13))
      {
        /*
	 * Synchronisation bits that were sent to improve message
	 * reception can be skippped.
	 */
        dataValue = (dataValue << 1);
        dataValue = dataValue | dataBit;
      }
    }

    if (dataType == 1)
    {
      if (httpData.indexOf("light") < 0)
      {
        int brightness = 1023 - dataValue;
        httpData += "light,";
        httpData += brightness;
        httpData += "\n";
      }
    }
    if (dataType == 2)
    {
      if (httpData.indexOf("temperature") < 0)
      {
        /*
         * Get temperature in degrees Celsius, rounded to one decimal place.
         */
        float temperature = dataValue / 10.0;
        temperature = temperature - 50;
        int t = (int)(temperature * 10);
        httpData += "temperature,";
        httpData += (t / 10);
        httpData += ".";
        httpData += (t % 10);
        httpData += "\n";
      }
    }
    else if (dataType == 3)
    {
      if (httpData.indexOf("battery") < 0)
      {
        httpData += "battery,";
        long milliVolts = 1125300L / dataValue;
        httpData += milliVolts;
        httpData += "\n";
      }
    }

    /*
     * Finished reading all data bits.
     * Start again looking for a new header.
     */
    bitReadCount = 0;
    lowSampleCount = highSampleCount = 0;
    isReadingHeader = true;
    ledCounter = 20;

    noDataCounter = 0;
  }
  else
  {
    noDataCounter++;
    if (noDataCounter >= 10 * 1000 && httpData.length() > 0)
    {
      /*
       * We have received some data, but nothing in the last 10 seconds.
       * So take the opportunity to upload our results to internet.
       */
      httpData.trim();
      uploadData(httpData);
      httpData = String();

      /*
       * If A2 is connected to ground then sleep for nearly one hour
       * (this is the frequency of the transmissions).
       * Otherwise, keep polling for the next transmission immediately.
       */
      int sleepReading = analogRead(SLEEP_SENSOR_PIN);
      if (sleepReading == 0)
      {
        delay(3500 * 1000);
      }
    }
  }

  if (ledCounter > 0)
  {
    ledCounter--;
    if (ledCounter == 0)
      ledValue = LOW;
  }

  digitalWrite(LED_PIN, ledValue);
}
