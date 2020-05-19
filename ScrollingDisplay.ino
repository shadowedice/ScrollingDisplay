#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

#include <ArduinoJson.h>

#define MAX_BUFFERS 3
#define MAX_PACKET_SIZE 2047
#define MAX_JSON_SIZE 2048
#define MAX_COLORS 10

#define LED_MATRIX_WIDTH 64
#define LED_BRIGHTNESS 1
#define LED_UPDATE_TIME 75 // In ms

#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>

StaticJsonDocument<MAX_JSON_SIZE> jsonDoc;

struct DisplayMsg
{
  String msg[MAX_COLORS];
  uint8_t r[MAX_COLORS];
  uint8_t g[MAX_COLORS];
  uint8_t b[MAX_COLORS];
  uint16_t totalSize;
  uint16_t numColors;
  IPAddress ip;
  uint16_t port;
  uint16_t id;
  bool processed;
};

WiFiUDP Udp;
unsigned int localUdpPort = 4210;
char incomingPacket[MAX_PACKET_SIZE];

DisplayMsg msgBuffers[MAX_BUFFERS];
int currentBuffIndex = 0;
int currentDisplayCursor = LED_MATRIX_WIDTH;
unsigned long updateLEDTime = 0;

Adafruit_NeoMatrix *matrix = new Adafruit_NeoMatrix(32, 8, 2, 1, 4,
  NEO_MATRIX_TOP     + NEO_MATRIX_LEFT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800);

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  // Setup WiFi connection
  WiFiManager wifiManager;
  wifiManager.autoConnect("Scrolling-Display");

  Serial.println(WiFi.localIP().toString().c_str());

  // Setup UDP
  Udp.begin(localUdpPort);

  //Set all buffers to processed
  for(int i=0; i < MAX_BUFFERS; i++)
  {
    msgBuffers[i].processed = true;
  }

   //Setup the LED matrix
  matrix->begin();
  matrix->setTextWrap(false);
  matrix->setBrightness(LED_BRIGHTNESS);

  displayIP();
}

void loop() {
  // put your main code here, to run repeatedly:
  checkForMessages();

  if(millis() - updateLEDTime > LED_UPDATE_TIME)
  {
    displayUpdate();
    updateLEDTime = millis();
  }
}

void checkForMessages()
{
  int packetSize = Udp.parsePacket();
  if (packetSize)
  {
    // receive incoming UDP packets
    Serial.printf("Received %d bytes from %s, port %d\n", packetSize, Udp.remoteIP().toString().c_str(), Udp.remotePort());
    int len = Udp.read(incomingPacket, MAX_PACKET_SIZE);
    //Serial.printf("UDP packet contents: %s\n", incomingPacket);

    // Look for an empty buffer
    int buffIndex = 0;
    while(buffIndex < MAX_BUFFERS)
    {
      if(msgBuffers[buffIndex].processed)
      {
        break;
      }
      buffIndex++;
    }

    // If a free buffer was found, stick data into it
    if(buffIndex < MAX_BUFFERS)
    {
      DeserializationError error = deserializeJson(jsonDoc, incomingPacket);
      if(error)
      {
        //Serial.print(F("deserializeJson() failed: "));
        //Serial.println(error.c_str());
        sendResponse("Deserialize Json failed", Udp.remoteIP(), Udp.remotePort());
        return;
      }

      // Parse the message and color components
      msgBuffers[buffIndex].totalSize = 0;
      JsonArray msg = jsonDoc["body"].as<JsonArray>();
      int index = 0;
      for(JsonVariant v : msg)
      {
        msgBuffers[buffIndex].msg[index] = v["msg"].as<String>();
        msgBuffers[buffIndex].totalSize += msgBuffers[buffIndex].msg[index].length();
        uint32_t rgb = v["color"].as<uint32_t>();
        msgBuffers[buffIndex].r[index] = rgb >> 16;
        msgBuffers[buffIndex].g[index] = (rgb & 0x00ff00) >> 8;
        msgBuffers[buffIndex].b[index] = (rgb & 0x0000ff);
        
        index++;
        if(index >= MAX_COLORS)
        {
          Serial.println("Hit max number of colors");
        }
      }
      msgBuffers[buffIndex].numColors = index;

      // Set up the rest of the data for responding back
      msgBuffers[buffIndex].processed = false;
      msgBuffers[buffIndex].ip = Udp.remoteIP();
      msgBuffers[buffIndex].port = Udp.remotePort();
      msgBuffers[buffIndex].id = jsonDoc["id"].as<int>();
    }
    //Send message saying queue is full
    else
    {
      sendResponse("The queue is currently full", Udp.remoteIP(), Udp.remotePort());
    }
  }
}

void displayUpdate()
{  
  // Current buffer needs to be processed
  if(!msgBuffers[currentBuffIndex].processed)
  {
    // Display scrolling text
    int bits = msgBuffers[currentBuffIndex].totalSize * -6;
    if(currentDisplayCursor > bits)
    {
      matrix->clear();
      matrix->setCursor(currentDisplayCursor,0);

      // Set color and text loop
      for(uint16_t i=0; i < msgBuffers[currentBuffIndex].numColors; i++)
      {
        matrix->setTextColor(matrix->Color(msgBuffers[currentBuffIndex].r[i], msgBuffers[currentBuffIndex].g[i], msgBuffers[currentBuffIndex].b[i]));
        matrix->print(msgBuffers[currentBuffIndex].msg[i]);
      }

      matrix->show();
      currentDisplayCursor--;
    }
    // Just finished displaying text, reset values and send message that it was processed
    else
    {
      currentDisplayCursor = LED_MATRIX_WIDTH;
      msgBuffers[currentBuffIndex].processed = true;
      String process = "Processed Message: " + String(msgBuffers[currentBuffIndex].id);
      sendResponse(process.c_str(), msgBuffers[currentBuffIndex].ip, msgBuffers[currentBuffIndex].port);
    
      currentBuffIndex++;
    }
  }
  // Current buffer is processed, try next
  else
  {
    currentBuffIndex++;
  }

  // Make sure we arent going out of buffer bounds
  if(currentBuffIndex >= MAX_BUFFERS)
  {
    currentBuffIndex = 0;
  }
}

void sendResponse(const char* response, IPAddress ip, uint16_t port)
{
  Udp.beginPacket(ip, port);
  Udp.write(response);
  Udp.endPacket();
}

void displayIP()
{
  int cur = LED_MATRIX_WIDTH;
  String ip = WiFi.localIP().toString();
  int chars = ip.length() * -6;
  
  while(cur > chars)
  {
    matrix->clear();
    matrix->setCursor(cur,0);
    matrix->setTextColor(matrix->Color(255,255,255));
    matrix->print(ip);
    matrix->show();
    delay(LED_UPDATE_TIME);
    cur--;
  }
}


