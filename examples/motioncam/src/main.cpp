#include <Arduino.h>
#include <WiFi.h> // WiFi functions
#include <WiFiClient.h> // WiFi Client functions
#include <ESPmDNS.h> // mDNS, to be visible under friendly name
#include <ESPAsyncWebServer.h> // Async Web Server and websockets
#include <LittleFS.h> // File system to load webpage from
#include <esp_camera.h> // ESP Camera library
#include <ArduinoJson.h> // Transmit JSON over websocket
#include "jpegstream.h"

// Sizes can be calculated with the tool: https://arduinojson.org/v6/assistant/
#define JSON_DOC_SIZE JSON_OBJECT_SIZE(64) // Max number of objects in JSON doc

// Enter WiFi credentials (SSID, password):
const char* ssid = "workshop";
const char* password = "workshop";

// Declare a webserver listening on port 80 with websocket:
const char* hostname = "motioncam";
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// PIR pin:
const int pirPin = 13;
bool pirState = LOW;
bool prevPirState = LOW;

// OV2640 camera config:
camera_config_t config = {
	.pin_pwdn = 32, // GPIO pin for camera power down line
	.pin_reset = -1,// GPIO pin for camera reset line
	.pin_xclk = 0, // GPIO pin for camera XCLK line
	.pin_sccb_sda = 26, // GPIO pin for camera SDA line
	.pin_sccb_scl = 27, // GPIO pin for camera SCL line

	.pin_d7 = 35, // GPIO pin for camera D7 line
	.pin_d6 = 34, // GPIO pin for camera D6 line
	.pin_d5 = 39, // GPIO pin for camera D5 line
	.pin_d4 = 36, // GPIO pin for camera D4 line
	.pin_d3 = 21, // GPIO pin for camera D3 line
	.pin_d2 = 19, // GPIO pin for camera D2 line
	.pin_d1 = 18, // GPIO pin for camera D1 line
	.pin_d0 = 5, // GPIO pin for camera D0 line
	.pin_vsync = 25,// GPIO pin for camera VSYNC line
	.pin_href = 23, // GPIO pin for camera HREF line
	.pin_pclk = 22, // GPIO pin for camera PCLK line

	.xclk_freq_hz = 20000000, // Frequency of XCLK signal, in Hz. EXPERIMENTAL: Set to 16MHz on ESP32-S2 or ESP32-S3 to enable EDMA mode

	.ledc_timer = LEDC_TIMER_0,// LEDC timer to be used for generating XCLK
	.ledc_channel = LEDC_CHANNEL_0,// LEDC channel to be used for generating XCLK

	.pixel_format = PIXFORMAT_JPEG, // Format of the pixel data: PIXFORMAT_ + YUV422|GRAYSCALE|RGB565|JPEG
	.frame_size = FRAMESIZE_UXGA, // Size of the output image: FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
	.jpeg_quality = 10, // Quality of JPEG output. 0-63 lower means higher quality
	.fb_count = 2, // Number of frame buffers to be allocated. If more than one, then each frame will be acquired (double speed)
	.grab_mode = CAMERA_GRAB_LATEST, // When buffers should be filled
};

#define MAXPHOTOS 5
int photoNum = 0; // Keep photos until MAXPHOTOS

void takePhotoToFS(String &path);
void sendPhotoFromFS(String &path);
void streamJpg(AsyncWebServerRequest *request);

// Websocket event handler prototype:
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

void setup() {
	// Begin serial:
	Serial.begin(115200);

	// Set pirPin to OUTPUT:
	pinMode(pirPin, INPUT);

	// Mount the LittleFS file system:
	LittleFS.begin();
	Serial.println("Total bytes available: " + String(LittleFS.totalBytes()));
	Serial.println("Used: " + String(LittleFS.usedBytes()));

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

	// Initialize the camera:
	esp_camera_init(&config);

	server.on("/stream", HTTP_GET, streamJpg);

	// Serve all statically from "/", default to "/rfidlock.html":
	server.serveStatic("/", LittleFS, "/").setDefaultFile("/motioncam.html");

	// Register websocket events and handler:
	ws.onEvent(onWsEvent);
	server.addHandler(&ws);

	// Start server
	server.begin();
}

void loop() {

	// Clean up clients every loop:
	ws.cleanupClients();
	
	// Check if PIR has gone HIGH from LOW:
	pirState = digitalRead(pirPin);
	if(prevPirState == LOW && pirState == HIGH){
		// Check if current photoNum is too high:
		if(photoNum >= MAXPHOTOS){
			photoNum = 0;
		}
		delay(500); // Allow time to capture newly entered activity in frame

		// Take photo, and save to current number in FS:
		String path = "/photo" + String(photoNum) + ".jpg";
		takePhotoToFS(path);

		// Send photo to all clients:
		sendPhotoFromFS(path);

		// Increment photoNum:
		photoNum++;
	}
	prevPirState = pirState; // Update prevPirState
	
	delay(2);
}

void takePhotoToFS(String &path){
	camera_fb_t * fb = NULL; // Framebuffer pointer for camera
	Serial.println("Taking photo!");

	// Attempt to get framebuffer:
	do{
		Serial.println("Trying to get framebuffer");
		fb = esp_camera_fb_get();
	}
	while(!fb);

	// Open photo for writing straight from buffer:
	File photo = LittleFS.open(path, "w");
	photo.write(fb->buf, fb->len);
	Serial.println("Written photo to: " + path + " - " + String(fb->len) + " bytes!");
	Serial.println("Used: " + String(LittleFS.usedBytes()));

	// Close file:
	photo.close();
	// Return fb pointer:
	esp_camera_fb_return(fb);
}

void sendPhotoFromFS(String &path){
	// Open file and stream over websocket:
	File photo = LittleFS.open(path, "r");
	size_t size = photo.size();
	uint8_t * buffer = (uint8_t *)malloc(size*sizeof(uint8_t));
	for(unsigned long long i = 0; i < size; i++){
		buffer[i] = (uint8_t)photo.read();
	}
	// Send to all clients:
	ws.binaryAll(buffer, size);
	free(buffer);
	photo.close();
}

// Response for client requesting MJPEG stream:
void streamJpg(AsyncWebServerRequest *request){
	AsyncJpegStreamResponse *response = new AsyncJpegStreamResponse();
	if(!response){
		request->send(501);
		return;
	}
	response->addHeader("Access-Control-Allow-Origin", "*");
	request->send(response);
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){				
	switch (type) {
	case WS_EVT_CONNECT:
	{
		Serial.printf("WebSocket client #%d connected from: %s\n", client->id(), client->remoteIP().toString());

		// On connect, send client a list of photos on filesystem to request:
		DynamicJsonDocument photoData(JSON_DOC_SIZE);
		photoData.createNestedArray("photos");

		// For each allowed photo on FS,check if it exists:
		for(int i = 0; i < MAXPHOTOS; i++){
			String photo = "/photo" + String(i) + ".jpg";
			// If photo exists, add to JSON array:
			if(LittleFS.exists(photo)){
				photoData["photos"].add(photo);
			}
		}

		String photoStr = "";
		serializeJson(photoData, photoStr); // Serialize
		Serial.println("Sending: " + photoStr);
		// Send JSON array to client:
		ws.text(client->id(), photoStr);
	}
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
		}
	}
		break;
	default:
		break;
	}
}