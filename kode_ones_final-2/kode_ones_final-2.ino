#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <TinyGPSPlus.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "OakOLED.h"
#include <WiFiClient.h>
#include <ESP8266HTTPClient.h>

//Constants
const char* SSID = "realme C11 2021"; //ssid wifi
const char* PASSWORD = "ergopythongolangsocialdistancingdetect"; //password wifi
const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
const int BUTTON_PIN = D3;
const int BUZZER_PIN = D6;
const int UTC_OFFSET = 25200;

// Class Initiation
TinyGPSPlus gps;  // The TinyGPS++ object
SoftwareSerial ss(13, 15); // Sofware Serial Object D7-D6
MAX30105 particleSensor;
OakOLED oled;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", UTC_OFFSET);
WiFiClient client;
// Variables
char timeBuffer[40];
String latitude, longitude;
byte rates[RATE_SIZE]; //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred
float beatsPerMinute;
int beatAvg;
long samplesTaken = 0; //Counter for calculating the Hz or read rate
long unblockedValue; //Average IR at power up
long startTime; //Used to calculate measurement rate
int percent; 
int degOffset = 1; //calibrated Farenheit degrees
int irOffset = 2100;
int count;
int noFinger;
int avgIr;
int avgTemp;
long delta;
long irValue;
float temperature = 0;
String temperatureChar;
String heartRateChar;

ICACHE_RAM_ATTR void handleInterrupt() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(3000);
  digitalWrite(BUZZER_PIN,LOW);
}


void setup()
{
  // pin setup
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  attachInterrupt(digitalPinToInterrupt( D3 ), handleInterrupt, FALLING ); 
  digitalWrite(BUZZER_PIN , LOW);
  // Hardware and Software Serial
  Serial.begin(19200);
  ss.begin(9600);

  // Finger sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) //Use default I2C port, 400kHz speed
  {
    Serial.println("MAX30105 was not found. Please check wiring/power. ");
    while (1);
  }
  Serial.println("Place your index finger on the sensor with steady pressure.");
  particleSensor.setup(0); //Configure sensor. Turn off LEDs
  particleSensor.enableDIETEMPRDY(); //Enable the temp ready interrupt. This is required.
  particleSensor.setup(25, 4, 2, 400, 411, 2048); //Configure sensor with these settings
  particleSensor.setPulseAmplitudeRed(0x0A); //Turn Red LED to low to indicate sensor is running
  particleSensor.setPulseAmplitudeGreen(0); //Turn off Green LED
  particleSensor.enableDIETEMPRDY(); //Enable the temp ready interrupt. This is required.
  const byte avgAmount = 64;
  long baseValue = 0;
  for (byte x = 0 ; x < avgAmount ; x++)
  {
    baseValue += particleSensor.getIR(); //Read the IR value
  }
  baseValue /= avgAmount;

  //OLED
  oled.begin();
  oled.clearDisplay();
  
  // Internet
  WiFi.begin(SSID, PASSWORD);

  while ( WiFi.status() != WL_CONNECTED ) {
    delay ( 500 );
    Serial.print ( "." );
  }

  timeClient.begin();

}

void loop()
{
  while(ss.available() > 0){
    if(gps.encode(ss.read())){
      if (gps.location.isValid()){
        latitude = String(gps.location.lat(), 6);  
        longitude = String(gps.location.lng() , 6);
        Serial.flush();
        break;      
      }
    }
  }
  irValue = particleSensor.getIR();
  if (checkForBeat(irValue)){
    delta = millis() - lastBeat;    
    lastBeat = millis();
    beatsPerMinute = 60 / (delta / 1000.0);
    if (beatsPerMinute < 255 && beatsPerMinute > 20){
      rates[rateSpot++] = (byte)beatsPerMinute; 
      rateSpot %= RATE_SIZE; 
      beatAvg = 0;
      for (byte x = 0 ; x < RATE_SIZE ; x++)
        beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }
  }

  percent = irValue / irOffset; //offset provides percent
  temperature = particleSensor.readTemperature(); //Because I am a bad global citizen
  temperature += degOffset;
  if (irValue < 50000) {
    noFinger = noFinger+1;
  } 
  else {
    count++;
    avgIr = avgIr + irValue;
    avgTemp = avgTemp + temperature;
  }
  if (count == 500) {
    avgIr = avgIr / count;
    avgIr = avgIr / irOffset; //offset provides percent
    avgTemp = avgTemp / count;
    count = 0; 
    avgIr = 0;
    avgTemp = 0;
  }
  temperatureChar = String(temperature,2);
  heartRateChar = String(beatsPerMinute,2);
  timeClient.update();
  updateSerialMonitor();
  updateOLED();
  updateThingSpeak();
}

void updateSerialMonitor(){
  Serial.print("Latitude=");
  Serial.print(latitude);   
  Serial.print(", Longitude="); 
  Serial.println(longitude);
  Serial.print("IR=");
  Serial.print(irValue);
  Serial.print(", BPM=");
  Serial.print(heartRateChar);
  Serial.print(", Avg BPM=");
  Serial.print(beatAvg);
  Serial.println();
  Serial.print("Oxygen=");
  Serial.print(percent);
  Serial.print("%"); 
  Serial.print(" Temp=");
  Serial.print(temperatureChar);
  Serial.print("°C");
  Serial.print(" avgO=");
  Serial.print(avgIr);
  Serial.print(" avgTemperature=");
  Serial.print(avgTemp);
  Serial.print(", count=");
  Serial.print(count);
  Serial.println();
}

void updateOLED(){
  String timer = timeClient.getFormattedTime();
  oled.clearDisplay();
  oled.setCursor(15,10);
  oled.setTextSize(2);
  oled.println(timer);
  oled.setTextSize(1);
  oled.setCursor(65, 42);
  oled.println("Heart BPM");
  oled.setCursor(80,55);
  oled.println(heartRateChar);
  oled.setCursor(15, 42);
  oled.println("Spo2");
  oled.setCursor(22,55);
  oled.print(String(percent));
  oled.display();
  
}

void updateThingSpeak(){
  char thingspeakUpdateQuery[150];
  HTTPClient http;
  sprintf(thingspeakUpdateQuery, "http://api.thingspeak.com/update?api_key=RKPSBJZZCTSJ6A9X&field1=%d&field2=%s&field3=%s&field5=%s&field6=%s", percent, temperatureChar, heartRateChar, latitude, longitude);
  http.begin(client, thingspeakUpdateQuery);
  http.GET();
  http.end();
}
