/*
 * Weather sensor and radio transmitter.
 *
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
#include <avr/sleep.h>
#include <avr/power.h>

/*
 * Sensor pins.
 */
#define LIGHT_SENSOR_PIN 0
#define TEMPERATURE_SENSOR_PIN 1
#define SLEEP_SENSOR_PIN 2

/*
 * Output pins.
 */
#define RADIO_PIN 12
#define LED_PIN 13

volatile int f_wdt = 1;

/*
 * The number of times we have slept for 8 seconds.
 */
volatile int sleepCounter = 0;

/*
 * The number of times to sleep before transmitting.
 */
volatile int sleepLimit = 1;

/*
 * Watchdog Interrupt Service. This is executed when watchdog times out.
 */
ISR(WDT_vect)
{
  if (f_wdt == 0)
  {
    f_wdt=1;
  }
}

void
enterSleep(void)
{
  set_sleep_mode(SLEEP_MODE_PWR_SAVE);
  
  sleep_enable();
  
  /* Now enter sleep mode. */
  sleep_mode();
  
  /* The program will continue from here after the WDT timeout*/
  sleep_disable(); /* First thing to do is disable sleep. */
  
  /* Re-enable the peripherals. */
  power_all_enable();
}

void
setup()
{
  /*** Setup the WDT ***/

  /* Clear the reset flag. */
  MCUSR &= ~(1<<WDRF);

  /* In order to change WDE or the prescaler, we need to
   * set WDCE (This will allow updates for 4 clock cycles).
   */
  WDTCSR |= (1<<WDCE) | (1<<WDE);

  /* set new watchdog timeout prescaler value */
  WDTCSR = 1<<WDP0 | 1<<WDP3; /* 8.0 seconds */
  
  /* Enable the WD interrupt (note no reset). */
  WDTCSR |= _BV(WDIE);
}

byte dataBits[11 + 2 + 10 + 4] = {1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1};

void
transmitData(int readingType, int readingValue)
{
  /*
   * Add sensor type and sensor reading value to list of bits to send
   * after the header (which is always the same).
   */
  dataBits[11] = ((readingType >> 1) & 0x1);
  dataBits[12] = (readingType & 0x1);

  int dataBitsIndex = 13;
  for (int i = 0; i < 10; i++)
  {
    /*
     * Add the opposite of second bit value after each pair of bits to
     * avoid a long series of bits with the same value that are difficult
     * for radio receiver to synchronize with.
     */
    if (i > 0 && i % 2 == 0)
    {
      int synchronisationBit = !dataBits[dataBitsIndex - 1];
      dataBits[dataBitsIndex] = synchronisationBit;
      dataBitsIndex++;
    }

    if (((readingValue >> (9 - i)) & 0x1) == 0x1)
      dataBits[dataBitsIndex] = 1;
    else
      dataBits[dataBitsIndex] = 0;
    dataBitsIndex++;
  }

  /*
   * Send each bit.
   */
  for (int i = 0; i < sizeof(dataBits); i++)
  {
    digitalWrite(RADIO_PIN, dataBits[i]);
    digitalWrite(LED_PIN, dataBits[i]);
    delay(10);
  }
  
  /*
   * Always return LED and radio to LOW.
   */
  digitalWrite(RADIO_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
}

long
readVcc()
{
  // See http://hacking.majenko.co.uk/node/57 for explanation
  long result;
  // Read 1.1V reference against AVcc
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2);
  // Wait for Vref to settle
  ADCSRA |= _BV(ADSC);
  // Convert
  while (bit_is_set(ADCSRA,ADSC))
    ;
  result = ADCL;
  result |= ADCH<<8;
  return result;
}

int
max2(int n1, int n2)
{
  return (n1 > n2 ? n1 : n2);
}

int
min2(int n1, int n2)
{
  return (n1 < n2 ? n1 : n2);
}

/*
 * Return median of 3 values.
 */
int
median3(int n1, int n2, int n3)
{
  if (n1 > n2 && n1 > n3)
  {
    return (max2(n2, n3));
  }
  else if (n1 > n2)
  {
    return n1;
  }
  else
  {
    return min2(n2, n3);
  }
}

void
loop()
{
  if (f_wdt == 1)
  {
    sleepCounter++;
    if (sleepCounter >= sleepLimit)
    {
      /*
       * Send everything two times, as sometimes messages are lost.
       * Temperature measurements fluctuate a lot, so take median of 3 values.
       */
      int temperatureReading1 = analogRead(TEMPERATURE_SENSOR_PIN);
      int lightReading = analogRead(LIGHT_SENSOR_PIN);
      transmitData(1, lightReading);
      int temperatureReading2 = analogRead(TEMPERATURE_SENSOR_PIN);
      delay(50);
      int temperatureReading3 = analogRead(TEMPERATURE_SENSOR_PIN);
      int temperatureReading = median3(temperatureReading1, temperatureReading2, temperatureReading3);
      transmitData(2, temperatureReading);
      delay(50);
      long vccReading = readVcc();
      transmitData(3, vccReading);
      delay(50);
      transmitData(1, lightReading);
      delay(50);
      transmitData(2, temperatureReading);
      delay(50);
      transmitData(3, vccReading);

      /*
       * If A2 is connected to ground then sleep for one hour.
       * Otherwise transmit every 4 * 8 = 32 seconds (useful
       * for testing/debugging).
       */
      int sleepReading = analogRead(SLEEP_SENSOR_PIN);
      if (sleepReading == 0)
      {
        /*
         * Calibrated to approximately one hour with
         * lost time for waking up and transmission included.
         */
        sleepLimit = 408;
      }
      else
        sleepLimit = 4;

      sleepCounter = 0;
    }

    /* Don't forget to clear the flag. */
    f_wdt = 0;
    
    /* Re-enter sleep mode. */
    enterSleep();
  }
  else
  {
    /* Do nothing. */
  }
}

