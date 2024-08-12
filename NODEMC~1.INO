#include <arduinoFFT.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>

#include "ESPAsyncWebServer.h"

#define SAMPLES 128
#define SAMPLING_FREQUENCY 1000
arduinoFFT FFT = arduinoFFT();

// NodeMCU access point credentials
const char* ssid = "ESP8266_AP";
const char* password = "ProjectCPRBaby";

// Parameters from HTML
const char* param_ssid = "ssid";
const char* param_pass = "pass";
const char* param_server = "server";
const char* param_port = "port";

// Received WiFi and Server credentials
String receivedSSID;
String receivedPassword;
String receivedServerIP;
int receivedServerPort;

short int wifiState = 0;

AsyncWebServer server(80);

// Flask server endpoints
const char *endpoint = "/arduino_endpoint";

// Sensor Pins
const int analogInPin = A0;
const int flowSensorPin = D2;
const int greenLEDPin = D3;
const int redLEDPin = D4;
const int buzzerPin = D5;

// Sensor Variables
volatile unsigned int pulseCount = 0;
unsigned int peakFlow = 0;
unsigned long lastSecond = 0;
float smoothedFlow = 0.0;
float scalingFactor = 0.125;
float minFlowRate = 80.00;
float maxFlowRate = 100.00;
float filteredFrequency = 0.0;

int sensorValue = 0;
float filteredSensorVoltage = 0.2;
const float alphaP = 0.95;
const float alphaF = 0.95;

const float thresholdMin = 0.38;
const float thresholdMax = 0.50;

const char index_html[] PROGMEM = R"rawliteral(
  <!DOCTYPE HTML>
  <html>
    <head>
      <title>ESP8266 WiFi Credentials Setup</title>
      <meta name="viewport" content="width=device-width, initial-scale=1">

      <style>
        html {
          font-family: Times New Roman, Georgia, Garamond;
          font-size: 1rem;
          background-color: #fce4ec;
        }
        
        body {
          display: flex;
          flex-direction: column;
          justify-content: center;
          align-items: center;
          height: 100%;
          flex: 1;
        }
        
        h1 {
          font-size: 1.2rem;
          font-weight: 600;
        }

        h2 {
          font-size: 1rem;
          font-weight: 500; 
          color: #ff80ab; 
          margin-bottom: 0.5rem;
        }

        input {
          margin-top: 0.5rem;
          margin-bottom: 0.75rem;
        }

        .wifi-credentials {
          width: 100%;
        }

        .save-button {
          padding: 0.7rem 1.2rem; 
          margin-top: 1rem;
          border: none;
          background-color: #ff80ab;
          color: #fff; 
          border-radius: 20px; 
          cursor: pointer;
          transition: background-color 0.3s ease;
        }

        .save-button:hover {
          background-color: #ff99bb; 
        }
      </style>

    </head>
    <body>
      <h1>Setup your Wi-Fi Credentials</h1>
      <h2>Enter the following to get started:</h2>
      <form action="/save">
        <label for="ssid">SSID/Wi-Fi Name: </label>
        <input class="wifi-credentials" type="text" name="ssid" id="ssid"> </br>
        <label for="pass">Wi-Fi Password: </label>
        <input class="wifi-credentials" type="password" name="pass" id="pass"> </br>
        <label for="server">Server IP: </label>
        <input class="wifi-credentials" type="text" name="server" id="server"> </br>
        <label for="ssid">Port: </label>
        <input class="wifi-credentials" type="text" name="port" id="port"> </br>
        <input class="save-button" type="submit" value="Save">
      </form>
    </body>    
  </html>
)rawliteral";

void ICACHE_RAM_ATTR pulseCounter()
{
    pulseCount++;
}

void ICACHE_RAM_ATTR sendToFlask(const String &data)
{
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;

    // Send POST Request to the Flask server
    String url = "http://" + String(receivedServerIP)+ ":" + String(receivedServerPort) + String(endpoint);

    // For debugging purposes
    Serial.print("Sending to: ");
    Serial.println(url);
    
    http.begin(client, url);
    http.addHeader("Content-Type", "text/plain");
    http.addHeader("Content-Length", String(data.length()));

    int responseCode = http.POST(data);

    if (responseCode > 0) {
      Serial.print("HTTP POST Request was successful. Response code: ");
      Serial.println(responseCode);
      String payload = http.getString();
      Serial.println("Response payload: " + payload);
    } else {
      Serial.print("HTTP POST Request failed. Error code: ");
      Serial.println(responseCode);
    }

    client.stop();
    http.end();
  }
}

void playBeat() {
  for (int i = 0; i < 2; i++) {
    tone(buzzerPin, 1000);
    delay(500); 
    noTone(buzzerPin);
    delay(100);
    tone(buzzerPin, 1000);
    delay(500); 
    noTone(buzzerPin);
    delay(100);
  }
  delay(100);
}


void setupWebServer() {
  // Setup access point
  Serial.println("Setting up access point...");
  WiFi.softAP(ssid, password);

  // Getting the IP address of the access point
  IPAddress IP = WiFi.softAPIP();
  Serial.print("Access Point IP Address: ");
  Serial.println(IP);

  // Server endpoints
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", index_html);
  });

  server.on("/save", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (request->hasParam(param_ssid) && request->hasParam(param_pass)) {

      // Saves the received credentials
      receivedSSID = request->getParam(param_ssid)->value();
      receivedPassword = request->getParam(param_pass)->value();
      receivedServerIP = request->getParam(param_server)->value();
      
      String portString = request->getParam(param_port)->value();
      receivedServerPort = portString.toInt();

      // For debugging purposes
      Serial.print("Received SSID: ");
      Serial.println(receivedSSID);
      Serial.print("Received Password: ");
      Serial.println(receivedPassword);
      Serial.print("Received Server IP: ");
      Serial.println(receivedServerIP);
      Serial.print("Received Server Port: ");
      Serial.println(receivedServerPort);

      // Updates WiFi state
      wifiState = 1;
      request->send(200, "text/plain", "Connecting ESP8266 to the WiFi...");
    } else {
      request->send(400, "text/plain", "Please input both SSID and Password of the WiFi.");
    }
  });

  // Start Server
  server.begin();
}

void setup() {
  // Serial for debugging purposes
  Serial.begin(9600);
  Serial.println();

  // Setting up Web Server
  setupWebServer();

  // Setting up Node Pins
  attachInterrupt(digitalPinToInterrupt(flowSensorPin), pulseCounter, FALLING);

  pinMode(greenLEDPin, OUTPUT);
  pinMode(redLEDPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT); 
}

void loop() {
  switch(wifiState) {
    case 1:
      if (WiFi.status() != WL_CONNECTED) {
        WiFi.begin(receivedSSID.c_str(), receivedPassword.c_str());

        Serial.print("Attempting to connect to WiFi: ");
        Serial.println(receivedSSID);
       
        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
          delay(500);
          Serial.println(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("WiFi connected");
          Serial.println("IP address: ");
          Serial.println(WiFi.localIP());
          wifiState = 2;
          server.end();
        } else {
          Serial.println("Failed to connect to WiFi.");
          wifiState = 0;
        }
      }
      break;
    case 2:
      // NodeMCU operation on connect to WiFi
      unsigned long currentMillis = millis();

      float millilitersPerSecond = pulseCount * scalingFactor;
  
      smoothedFlow = 0.9 * smoothedFlow + 0.1 * millilitersPerSecond;
  
      sensorValue = analogRead(A0); 
  
      float sensorVoltage = sensorValue * (3.3 / 1023.0); 
    
      filteredSensorVoltage = alphaP * sensorVoltage + (1 - alphaP) * filteredSensorVoltage;
  
      float pressure_difference = (filteredSensorVoltage - 0.2) / 0.018; 
      float kPa = pressure_difference + 0.2; 
  
      float PSI = kPa * 0.145037738;

      float calibratedPSI = (0.3401 * pow(PSI, 2)) + (0.2569 * PSI) + 0.3836;
      float newCalibratedPSI = (-0.3298 * pow(calibratedPSI, 2)) + (1.295 * calibratedPSI) + 0.8505;
      float newerCalibratedPSI = abs((-4.2968 * pow(newCalibratedPSI, 2)) + (17.6025 * newCalibratedPSI) - 15.9738);
      float newestCalibratedPSI = (0.3338 * pow(newerCalibratedPSI, 2)) + (0.2481 * newerCalibratedPSI)  + 0.038;
  
      if (newestCalibratedPSI >= thresholdMin && PSI <= thresholdMax)
      {
          Serial.println("Pressure reached!");
      }
      else if (newestCalibratedPSI >= thresholdMax)
      {
          Serial.println("Too much Pressure!!!!");
          playBeat();
      }
      else
      {
          Serial.println("Pressure NOT enough");
      }
  
      if (currentMillis - lastSecond >= 1000) {
        float millilitersPerSecond = pulseCount * scalingFactor;

        float calibratedValue = (-12.7507 * pow(millilitersPerSecond, 2)) + (74.9753 * millilitersPerSecond) + 31.4014;
        float newCalibratedValue = (0.0002 * pow(calibratedValue, 2)) + (1.4602 * calibratedValue) - 45.7881;
        float newerCalibratedValue = (-0.0048 * pow(newCalibratedValue, 2)) + (1.5379 * newCalibratedValue) + 3.9049;
        float newestCalibratedValue = (-0.0015 * pow(newerCalibratedValue, 2)) + (1.0246 * newerCalibratedValue) - 2.1916;
        
        smoothedFlow = alphaF * newestCalibratedValue + (1 - alphaF) * smoothedFlow;

          sendToFlask("Flow Rate: " + String(smoothedFlow, 2) + " mL/s");
  
          Serial.print("Flow Rate: ");
          Serial.print(smoothedFlow);
          Serial.print(" mL/s, ");
  
          if (smoothedFlow >= minFlowRate && smoothedFlow <= maxFlowRate)
          {
              Serial.print(" (HIT) ");
              digitalWrite(greenLEDPin, HIGH);
              digitalWrite(redLEDPin, LOW);
          }
          else if (smoothedFlow >= maxFlowRate)
          {
              Serial.print(" (MAX) ");
              digitalWrite(greenLEDPin, LOW);
              digitalWrite(redLEDPin, HIGH);
          }
          else if (smoothedFlow <= minFlowRate)
          {
              Serial.print(" (MIN) ");
              digitalWrite(greenLEDPin, LOW);
              digitalWrite(redLEDPin, HIGH);
          }
  
          if (smoothedFlow > peakFlow)
          {
              peakFlow = smoothedFlow;
          }
  
          Serial.print("Peak Flow Rate: ");
          Serial.print(peakFlow);
          Serial.print(" mL/s, ");
  
          float currentFrequency = pulseCount / ((currentMillis - lastSecond) / 1000.0);
          filteredFrequency = 0.9 * filteredFrequency + 0.1 * currentFrequency;
  
          Serial.print("Frequency: ");
          Serial.print(filteredFrequency);
          Serial.println(" Hz");
  
          pulseCount = 0;
          smoothedFlow = 0.0;
          peakFlow = 0;
          lastSecond = currentMillis;
        }

        // Send PSI
        WiFiClient client;
        HTTPClient http;

        // Send POST Request to the Flask server
        String url = "http://" + String(receivedServerIP) + ":" + String(receivedServerPort) + "/receive_data";
        
        // For debugging purposes
        Serial.print("Sending to: ");
        Serial.println(url);

        // Send POST Request to the Flask server
        http.begin(client, url);
        http.addHeader("Content-Type", "application/json");
    
        int responseCode = http.POST(String(PSI));
    
        if (responseCode > 0) {
          Serial.print("HTTP POST Request was successful. Response code: ");
          Serial.println(responseCode);
          String payload = http.getString();
          Serial.println("Response payload: " + payload);
        } else {
          Serial.print("HTTP POST Request failed. Error code: ");
          Serial.println(responseCode);
        }

        client.stop();
        http.end();

        double vReal[SAMPLES];
        double vImag[SAMPLES];
    
        for (int i = 0; i < SAMPLES; i++)
        {
            vReal[i] = sensorVoltage; 
            vImag[i] = 0;
        }
    
        FFT.Windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
        FFT.Compute(vReal, vImag, SAMPLES, FFT_FORWARD);
        FFT.ComplexToMagnitude(vReal, vImag, SAMPLES);
        double peak = FFT.MajorPeak(vReal, SAMPLES, SAMPLING_FREQUENCY);
    
        Serial.println("Peak frequency: " + String(peak) + " Hz");

        Serial.println();
        Serial.println();
    
        delay(100); 
      break;
  }
}
