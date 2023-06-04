#include <Arduino.h>
#include <WiFi.h> // WiFi functions
#include <WiFiClientSecure.h> // WiFi Client functions encrypted
#include <LittleFS.h> // File system to load SSL certificate
#include <ArduinoJson.h> // Calibration stored in JSON document
#include <MQTT.h> // MQTT client library

#include <DHT.h> // DHT sensor library to read values
#include <MQUnifiedsensor.h> // MQ Air quality sensors
#include <LiquidCrystal_I2C.h> // LCD over I2C

#define JSON_DOC_SIZE JSON_OBJECT_SIZE(8) // Max number of objects in JSON doc

// Enter WiFi credentials (SSID, password):
const char* ssid = "workshop";
const char* password = "workshop";

// Define client connections for MQTT:
WiFiClientSecure wifiSecure;
MQTTClient mqtt;

// DHT11 we control is on pin 26:
DHT myDHT11(27, DHT11);
long dht11ReadTime = 0;

// MQ135 air quality sensor:
MQUnifiedsensor myMQ135("ESP-32", 3.3, 12, 32, "MQ-135");
long mq135ReadTime = 0;

// I2C LCD at address 0x27, 16 chars, 2 rows:
LiquidCrystal_I2C lcd(0x27, 16, 2);
#define I2C_SDA 25
#define I2C_SCL 26

// File handler prototypes:
String readFile(const String& path);
void writeFile(const String& path, String& in);

// Calibration subroutine prototype:
float mq135Cal();
// MsgRecv prototype:
void msgRecv(String &topic, String &payload);

void setup(){
	// Start Serial interface at 115200 baud:
	Serial.begin(115200);

	// Mount the LittleFS file system:
	LittleFS.begin();

	// Initialize LCD:
	lcd.init(I2C_SDA, I2C_SCL);
	lcd.backlight();

	// Begin DHT:
	myDHT11.begin();

	// Set math model to calculate the PPM concentration and the value of constants
	myMQ135.setRegressionMethod(1); // PPM = A *(normalized sensor resistance)^B
	myMQ135.init();

	// If calibration data exists:
	float R0; // Normalisation factor for MQ-135
	String calStr; // Calibration JSON string

	// If calibration exists, use it:
	DynamicJsonDocument calData(JSON_DOC_SIZE);
	if(LittleFS.exists("/calibration.json")){
		calStr = readFile("/calibration.json");
		deserializeJson(calData, calStr);
		R0 = calData["MQ-135-R0"];
	}
	else{ // Else, calibrate, create a calibration JSON document and save it:
		R0 = mq135Cal();
		calData["MQ-135-R0"] = R0;
		serializeJson(calData, calStr);
		writeFile("/calibration.json", calStr);
	}
	Serial.println("R0 = " + String(R0));
	myMQ135.setR0(R0);

	// Set WiFi mode to station, and connect to WiFi:
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, password);
	while(WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}

	// When connected, print info:
	Serial.println("Connected to " + String(ssid) + ", IP address: " + WiFi.localIP().toString());

	// Load rootCA:
	String rootCAStr = readFile("/rootCA.pem");
	wifiSecure.setCACert(rootCAStr.c_str());

	// Setup the MQTT client, connect to port 8883 on secure network:
	mqtt.begin("mqtt.bechmann.xyz", 8883, wifiSecure);
	mqtt.onMessage(msgRecv);

	Serial.print("MQTT Connect");
	// Connect to MQTT server:
	while(!mqtt.connect("ESP32", "public", "public")) {
		delay(500);
		Serial.print(".");
	}

	// Subscribe to interesting topics:
	mqtt.subscribe("OtherESP32/mq135/co2");
	mqtt.subscribe("OtherESP32/dht11/temp");
}

void loop(){
	// Our ticker, keeps time in sketch:
	long curTime;

	// Update mqtt client loop, runs constantly:
	mqtt.loop();
	
	// Check if 500ms have passed, if true, then update MQ-135:
	curTime = millis();
	if((curTime - mq135ReadTime) >= 1000){
		mq135ReadTime = curTime;
		// Update reading on MQ-135:
		myMQ135.update();

		// Read CO2 PPM:
		myMQ135.setA(110.47); myMQ135.setB(-2.862);
		float CO2 = myMQ135.readSensor() + 421; // Known value offset: https://www.co2.earth/

		// CO: A: 605.18 B: -3.937
		// Alcohol: A: 77.255 B: -3.18
		// Toluene: A: 44.947 B: -3.445
		// NH4: A: 102.2 B: -2.473
		// Acetone: 34.668 B: -3.369

		Serial.println("Sending: ESP32/mq135/co2: " + String(CO2));
		mqtt.publish("ESP32/mq135/co2", String(CO2));

		lcd.setCursor(7,0);
		lcd.print(String(CO2, 1) + "PPM");
	}

	// Check if 2 seconds have passed, if true, then update DHT:
	curTime = millis();
	if((curTime - dht11ReadTime) >= 2000){
		dht11ReadTime = curTime;
		float temp;
		float hum;

		// Read data:
		do{
			// Read temperature as Celsius (the default), along with humidity:
			temp = myDHT11.readTemperature();
			hum = myDHT11.readHumidity();
		}while(isnan(temp) or isnan(hum));

		Serial.println("Sending: ESP32/dht11/temp: " + String(temp));
		mqtt.publish("ESP32/dht11/temp", String(temp));
		Serial.println("Sending: ESP32/dht11/hum: " + String(hum));
		mqtt.publish("ESP32/dht11/hum", String(hum));

		lcd.setCursor(0,0);
		lcd.print(String(temp, 1) + String((char)223) + "C");
	}

	// Delay to give background processes (WiFi handling etc.) more processing time.
	delay(2);
}

// Calibration for MQ-135:
float mq135Cal(){
	// R0 used for MQ-135 calibration:
	float R0 = 0;
	bool calDone = false;

	// Calculate R0 for calibrating air module:
	#define RatioMQ135CleanAir 3.6 //RS / R0 = 3.6 ppm
	#define NUM_SAMPLES 1000

	Serial.print("Calibrating please wait.");
	float calcR0 = 0;
	for(int i = 1; i<=NUM_SAMPLES; i ++){
		myMQ135.update();
		calcR0 += myMQ135.calibrate(RatioMQ135CleanAir);
		Serial.print(".");
		delay(1);
	}
	R0 = calcR0/NUM_SAMPLES;

	return R0;
}

// Reads file from LittleFS into string
String readFile(const String& path){
	// Load file for reading:
	File toRead = LittleFS.open(path, "r");
	String readStr = toRead.readString();
	toRead.close();
	return readStr;
}

// Writes file from string to LittleFS
void writeFile(const String& path, String& in){
	// Load file for writing:
	File toWrite = LittleFS.open(path, "w");
	toWrite.write((uint8_t*)in.c_str(), in.length());
	toWrite.close();
}

// Filters messages received and prints on LCD:
void msgRecv(String &topic, String &payload){
	Serial.println("Received: " + topic + ": " + payload);
	if(topic == "OtherESP32/mq135/co2"){
		lcd.setCursor(7,1);
		lcd.print(String(payload.toFloat(), 1) + "PPM");
	}
	else if(topic == "OtherESP32/dht11/temp"){
		lcd.setCursor(0,1);
		lcd.print(String(payload.toFloat(), 1) + String((char)223) + "C");
	}
}
