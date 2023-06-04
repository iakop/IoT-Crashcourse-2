#include <Arduino.h>
#include <WiFi.h> // WiFi functions
#include <WiFiClient.h> // WiFi Client functions
#include <ESPAsyncWebServer.h> // Async Web Server and websockets
#include <ArduinoJson.h> // Transmit JSON over websocket
#include <ESPmDNS.h> // mDNS, to be visible under friendly name
#include <LittleFS.h> // File system to load webpage from
#include <SPI.h> // SPI for RFID reader
#include <MFRC522.h> // RFID Reader

// Sizes can be calculated with the tool: https://arduinojson.org/v6/assistant/
#define JSON_DOC_SIZE JSON_OBJECT_SIZE(64) // Max number of objects in JSON doc
#define ENROLLED_DOC_SIZE JSON_OBJECT_SIZE(512) // Max number of objects in JSON doc
StaticJsonDocument<ENROLLED_DOC_SIZE> enrolledData; // Static JSON doc to keep enrolled cards
String enrolledStr; // String for holding stringified data
const String enrolledFile = "/secrets/enrolled.json"; // Enrolled file location on LittleFS

// Enter WiFi credentials (SSID, password):
const char* ssid = "workshop";
const char* password = "workshop";

// Declare a webserver listening on port 80 with websocket:
const char* hostname = "rfidlock";
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Declare an MFRC522 RFID reader:
MFRC522 mfrc522(27 /*CS*/, 26 /*RST*/);

// Electronic lock pin:
const int lockPin = 25;
const int LATCHTIME = 5000;
unsigned long latchTimer = 0;

// File handler prototypes:
String readFile(const String& path);
void writeFile(const String& path, String& in);

// Dumping UID from card reader:
String dumpByteArray(byte *buffer, byte bufferSize);

// Websocket event handler prototype:
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

void setup() {
	// Begin serial:
	Serial.begin(115200);

	// Lock pin to output:
	pinMode(lockPin, OUTPUT);
	digitalWrite(lockPin, LOW); // Default locked

	// Begin SPI:
	SPI.begin(14 /*SCLK*/, 12 /*MISO*/, 13 /*MOSI*/, 27 /*CS*/);

	// Init MFRC522 RFID reader:
	mfrc522.PCD_Init();

	// Mount the LittleFS file system:
	LittleFS.begin();

	// Set WiFi mode to station, and connect to WiFi:
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, password);
	while(WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}

	// Begin MDNS hostname for network discovery:
	MDNS.begin(hostname);

	// When connected, print info:
	Serial.println(); Serial.println("Connected to " + String(ssid) + ", IP address: " + WiFi.localIP().toString() + " - " + String(hostname));

	// Populate enrolled cards:
	if(LittleFS.exists(enrolledFile)){
		String enrolledStr = readFile(enrolledFile);
		deserializeJson(enrolledData, enrolledStr); // Deserialize data into JSON object
	}
	else{
		enrolledData.createNestedArray("enrolled"); // Create empty nested JSON array in empty object
	}

	// Serve all statically from "/", default to "/rfidlock.html":
	server.serveStatic("/", LittleFS, "/").setDefaultFile("/rfidlock.html");

	// Register websocket events and handler:
	ws.onEvent(onWsEvent);
	server.addHandler(&ws);
	
	// Start server:
  	server.begin();
}

void loop() {
	// Clean up clients every loop:
	ws.cleanupClients();

	// Take time sample:
	long curTime = millis();

	// If latch has timed out:
	if((curTime - latchTimer) >= LATCHTIME){
		digitalWrite(lockPin, LOW); // Locked
	}

	// If card present:
	if(mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()){
		// Get card UID for authentication:
		String uid = dumpByteArray(mfrc522.uid.uidByte, mfrc522.uid.size);

		Serial.println("Scanned: " + uid);

		// Check if enrolled:
		for(int i = 0; i < enrolledData["enrolled"].size(); i++){
			if(enrolledData["enrolled"][i] == uid){
				// Set latchTimer and unlock:
				latchTimer = curTime;
				Serial.println("Unlocked!");
				digitalWrite(lockPin, HIGH); // Unlocked
				return; // No more to do
			}
		}

		// If not enrolled:
		// Send scanned card to connected clients:
		String scannedStr = "";
		DynamicJsonDocument scannedData(JSON_DOC_SIZE);
		scannedData["scanned"] = uid; // Set uid in scanned member
		serializeJson(scannedData, scannedStr); // Serialize
		Serial.println("Sending: " + scannedStr);
		ws.textAll(scannedStr);
	}

	// Delay to give background processes (WiFi handling etc.) more processing time.
	delay(2);
}

// Creates a hex string from the buffer 
String dumpByteArray(byte *buffer, byte bufferSize) {
	String byteStr = "";
	for (byte i = 0; i < bufferSize; i++) {
		byteStr.concat(String(buffer[i], HEX));
 	}
	return byteStr;
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

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){				
	switch (type) {
	case WS_EVT_CONNECT:
		Serial.printf("WebSocket client #%d connected from: %s\n", client->id(), client->remoteIP().toString());
		
		// Send enrolled cards to client:
		enrolledStr = ""; // Clear enrolledStr, then serialize
		serializeJson(enrolledData, enrolledStr);
		Serial.println("Sending: " + enrolledStr);
		ws.text(client->id(), enrolledStr);

		break;
	case WS_EVT_DISCONNECT:
		Serial.printf("WebSocket client #%d disconnected...", client->id());
		break;
	case WS_EVT_DATA:
	{
		// Check if all data received, and matches datatype:
		AwsFrameInfo *info = (AwsFrameInfo*)arg;
		if((info->final) && (info->opcode == WS_TEXT)){
			// Terminate, cast and print data:
			data[len] = '\0';
			String recvStr((char*)data);
			Serial.printf("WebSocket client #%d data: %s\n", client->id(), recvStr.c_str());
			
			// Create and deserialize into Json Object:
			DynamicJsonDocument recvData(JSON_DOC_SIZE);
			deserializeJson(recvData, recvStr);

			// If enroll:
			if(recvData.containsKey("enroll")){
				// Add enrolled object to the enrolled list in JSON:
				enrolledData["enrolled"].add(recvData["enroll"]);
				enrolledStr = "";
				serializeJson(enrolledData, enrolledStr);
				writeFile(enrolledFile, enrolledStr); // Write list to file
				ws.textAll(enrolledStr); // Text out new list to all clients
			} // Else if delete:
			else if(recvData.containsKey("delete")){
				// Check enrolled list for entry:
				for(int i = 0; i < enrolledData["enrolled"].size(); i++){
					// If found remove:
					if(enrolledData["enrolled"][i] == recvData["delete"]){
						enrolledData["enrolled"].remove(i);
					}
				}
				// Re-serialize enrolled list
				enrolledStr = "";
				serializeJson(enrolledData, enrolledStr);
				writeFile(enrolledFile, enrolledStr); // Write to file
				ws.textAll(enrolledStr); // Text to all cients
			}
		}
	}
		break;
	default:
		break;
	}
}
