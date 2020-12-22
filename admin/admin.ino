#include "FirebaseESP8266.h"
#include "FirebaseJson.h"
#include <ESP8266WiFi.h>
#include <Arduino.h>
#include <IRsend.h>
#include <IRremoteESP8266.h>
#include <string.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>

//sensors declaration
const int motionSensor = 12; //D6
const int magneticSensor = 13; //D7
const int oneWireBus = 14; //D5
const uint16_t kIrLed = 4; //D2
IRsend irsend(kIrLed);
OneWire oneWire(oneWireBus);
DallasTemperature tempSensor(&oneWire);
/*wifi settings*/
#define FIREBASE_HOST "****"
#define FIREBASE_AUTH "****"
#define WIFI_SSID "****"
#define WIFI_PASSWORD "****"
FirebaseData firebaseData;
/*wifi settings*/

/************************************************************************************variables declaration*********************************/
/************************************************************************************variables declaration*********************************/
/************************************************************************************variables declaration*********************************/
/************************************************************************************variables declaration*********************************/
/************************************************************************************variables declaration*********************************/

//database variables
String Streampath = "/Reference/"+refBoitier+"/Buttons";
String protocol ="";
String myStringCode="";
String refClim="";
const unsigned char * myScode;
String lastCommand="24";
//sensors variables
boolean acStateON=false;
boolean acWorking=false;
boolean turnOnAC=false;
bool getTemper=false;
bool windowOpen=false;
unsigned long lastMotionDetectionTime = 0;
unsigned long lastWindowDetectionTime = 0;
unsigned long motionInterval=1;
unsigned long windowInterval=1;

/************************************************************************************functions declaration*********************************/
/************************************************************************************functions declaration*********************************/
/************************************************************************************functions declaration*********************************/
/************************************************************************************functions declaration*********************************/
/************************************************************************************functions declaration*********************************/
String getChildValue(String path);
unsigned long stringToHex(char* s);
uint64_t convertCodeToData(String str);
int charToInt(char c);
uint8_t charToUINT8(char c);

void streamCallback(StreamData data){
  Serial.println("EVENT PATH: " + data.streamPath() + data.dataPath());
  Serial.println();
  if(data.dataPath()!="/"){
    if(data.dataPath().indexOf("config")>-1){
      String generalTemperature=getChildValue(Streampath+"/config/generalTemperature");
      if(generalTemperature!=""){
        lastCommand=generalTemperature;
        Serial.print("generalTemperature ");
        Serial.println(generalTemperature);
      }
      String magneticSensor=getChildValue(Streampath+"/config/magneticSensor");
      if(magneticSensor!=""){
        windowInterval=atol(magneticSensor.c_str());
        Serial.print("windowInterval ");
        Serial.println(windowInterval);
      }
      String presenceSensor=getChildValue(Streampath+"/config/presenceSensor");
      if(presenceSensor!=""){
        motionInterval=atol(presenceSensor.c_str());
        Serial.print("motionInterval ");
        Serial.println(motionInterval);
      }
    }
    else if(data.dataPath().indexOf("getTemp/state")>-1){
      if(data.stringData()=="clicked"){
        getTemper=true;
      }
    }
    else{
      String myDataPath=data.dataPath();
      String myCommand = myDataPath.substring(1,myDataPath.lastIndexOf("/"));
      lastCommand=myCommand;
      if(myCommand=="Off"){
        acStateON=false;
      }
      else{
        acStateON=true;
      }
      if(refClim==""){
        refClim = getChildValue(Streampath+"/refClim");
      }
      String myCodePath = "/acRef/"+refClim+"/hexCodes/"+myCommand;
      String hexCodeString = getChildValue(myCodePath);
      myStringCode=hexCodeString;
      if(protocol==""){
        protocol=getChildValue(Streampath+"/Protocol");
      }
      if((data.dataType() == "string") && (data.stringData()=="clicked") ){
        Firebase.set(firebaseData, data.streamPath() + data.dataPath(),"unclicked");
      }
      if(hexCodeString.length()>18){
        String sstr=hexCodeString+"z";
        String str=sstr.substring(2);
        int len=str.length();
        uint8_t myCode[len/2];
        int j=0;
        for(int i=0;i<len-2;i+=2){
          myCode[j]=charToUINT8(str[i])*16+charToUINT8(str[i+1]);
          j++;
        }
        sendStateCode(protocol,myCode);
      }
      else{
        uint64_t myCode=convertCodeToData(hexCodeString);
        sendDataCode(protocol,myCode);
      }
    }
  }
}

void streamTimeoutCallback(bool timeout)
{
  if (timeout)
  {
    Serial.println();
    Serial.println("Stream timeout, resume streaming...");
    Serial.println();
  }
}

// Checks if motion was detected
ICACHE_RAM_ATTR void detectsMovement() {
  lastMotionDetectionTime=millis();
  Serial.println("motion detedcted");
  if(!acWorking && !windowOpen){
    turnOnAC=true;
  }
}

//window actions
ICACHE_RAM_ATTR void detectwindow() {
  if(digitalRead(magneticSensor)){
    Serial.println("window oponed");
    lastWindowDetectionTime=millis();
    windowOpen=true;
  }
  else{
    Serial.println("window closed");
    lastMotionDetectionTime=millis();
    if(!acWorking){
      turnOnAC=true;
    }
    windowOpen=false;
  }
}

/************************************************************************************Setup*************************************************/
/************************************************************************************Setup*************************************************/
/************************************************************************************Setup*************************************************/
/************************************************************************************Setup*************************************************/
/************************************************************************************Setup*************************************************/
void setup() {
  tempSensor.begin();
  irsend.begin();
#if ESP8266
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
#else  // ESP8266
  Serial.begin(115200, SERIAL_8N1);
#endif  // ESP8266

  /*sensors part*/
    pinMode(motionSensor, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(motionSensor), detectsMovement, RISING);
  pinMode(magneticSensor, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(magneticSensor), detectwindow, CHANGE);
  /*sensors part*/
  
  /*firebase part*/
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  Firebase.reconnectWiFi(true);

  //Set the size of WiFi rx/tx buffers in the case where we want to work with large data.
  firebaseData.setBSSLBufferSize(4096, 4096);

  //Set the size of HTTP response buffers in the case where we want to work with large data.
  firebaseData.setResponseSize(4096);


  if (!Firebase.beginStream(firebaseData, Streampath))
  {
    Serial.println("------------------------------------");
    Serial.println("Can't begin stream connection...");
    Serial.println("REASON: " + firebaseData.errorReason());
    Serial.println("------------------------------------");
    Serial.println();
  }
  Firebase.setStreamCallback(firebaseData, streamCallback, streamTimeoutCallback);
/*firebase part*/

    refClim = getChildValue(Streampath+"/refClim");
    protocol=getChildValue(Streampath+"/Protocol");
    String generalTemperature=getChildValue(Streampath+"/config/generalTemperature");
    if(generalTemperature!=""){
      lastCommand=generalTemperature;
    }
    String magneticSensor=getChildValue(Streampath+"/config/magneticSensor");
    if(magneticSensor!=""){
      windowInterval=atol(magneticSensor.c_str());
    }
    String presenceSensor=getChildValue(Streampath+"/config/presenceSensor");
    if(presenceSensor!=""){
      motionInterval=atol(presenceSensor.c_str());
    }
}

/************************************************************************************loop*************************************************/
/************************************************************************************loop*************************************************/
/************************************************************************************loop*************************************************/
/************************************************************************************loop*************************************************/
/************************************************************************************loop*************************************************/



void loop() {
  if(acStateON){
    if ((((unsigned long)((unsigned long)millis() - lastMotionDetectionTime) > (windowInterval*60000))) && acWorking){
    String myCodePath = "/acRef/"+refClim+"/hexCodes/Off";
    String hexCodeString = getChildValue(myCodePath);
    if(hexCodeString.length()>18){
      String sstr=hexCodeString+"z";
      String str=sstr.substring(2);
      int len=str.length();
      uint8_t myCode[len/2];
      int j=0;
      for(int i=0;i<len-2;i+=2){
        myCode[j]=charToUINT8(str[i])*16+charToUINT8(str[i+1]);
        j++;
      }
      sendStateCode(protocol,myCode);
    }
    else{
      uint64_t myCode=convertCodeToData(hexCodeString);
      sendDataCode(protocol,myCode);
    }
      Serial.println("AC off");
      acWorking=false;   
    }
    if ((((unsigned long)((unsigned long)millis() - lastWindowDetectionTime) >= (windowInterval*60000))) && windowOpen && acWorking){
    String myCodePath = "/acRef/"+refClim+"/hexCodes/Off";
    String hexCodeString = getChildValue(myCodePath);
    if(hexCodeString.length()>18){
      String sstr=hexCodeString+"z";
      String str=sstr.substring(2);
      int len=str.length();
      uint8_t myCode[len/2];
      int j=0;
      for(int i=0;i<len-2;i+=2){
        myCode[j]=charToUINT8(str[i])*16+charToUINT8(str[i+1]);
        j++;
      }
      sendStateCode(protocol,myCode);
    }
    else{
      uint64_t myCode=convertCodeToData(hexCodeString);
      sendDataCode(protocol,myCode);
    }
      Serial.println("AC off");
      acWorking=false;   
    }
    if(turnOnAC){
    String myCodePath =  "/acRef/"+refClim+"/hexCodes/"+lastCommand;
    String hexCodeString = getChildValue(myCodePath);
    if(hexCodeString.length()>18){
      String sstr=hexCodeString+"z";
      String str=sstr.substring(2);
      int len=str.length();
      uint8_t myCode[len/2];
      int j=0;
      for(int i=0;i<len-2;i+=2){
        myCode[j]=charToUINT8(str[i])*16+charToUINT8(str[i+1]);
        j++;
      }
      sendStateCode(protocol,myCode);
    }
    else{
      uint64_t myCode=convertCodeToData(hexCodeString);
      sendDataCode(protocol,myCode);
    }
      Serial.println("AC on");
      acWorking=true;
      turnOnAC=false;
    }
  }
  if(getTemper){
    sendTemp();
    getTemper=false;
  }
}




/************************************************************************************functions*************************************************/
/************************************************************************************functions*************************************************/
/************************************************************************************functions*************************************************/
/************************************************************************************functions*************************************************/
/************************************************************************************functions*************************************************/

/*****************************************************************************************************sendTemp************************************/
void sendTemp(){
  tempSensor.requestTemperatures();
  float temperatureC = tempSensor.getTempCByIndex(0);
  int cTemp= round(temperatureC);
  String sTemp= String(cTemp);
  Firebase.set(firebaseData, Streampath+"/getTemp/temp",sTemp);
  Firebase.set(firebaseData, Streampath+"/getTemp/state","unclicked");
}

/*****************************************************************************************************conversionFunctions************************************/

unsigned long stringToHex(char* s){
  char * pEnd;
  unsigned long code =strtol(s,&pEnd,16);
  return code;
}

/*****************************************************************************************************getChildValue************************************/

String getChildValue(String path){
  if(Firebase.get(firebaseData, path))
  {
    //Success
    if(firebaseData.dataType() == "string"){
      return (firebaseData.stringData());
    }
  }
  else{
    //Failed?, get the error reason from firebaseData

    Serial.print("Error in get, ");
    Serial.println(firebaseData.errorReason());
    return("");
  }
}
/*****************************************************************************************************convertCodeToData************************************/
uint64_t convertCodeToData(String str){
  int len=str.length();
  uint64_t m=0;
  int p=0;
  for(int i=len-1;i>=0;i--){
    int base=charToInt(str[i]);
    m+=base*pow(16, p);
    p++;
  }
  return m;
}
int charToInt(char c){
  int n;
      if(c == '0')
        n=0;
      else if(c == '1')
        n=1;
      else if(c == '2')
        n=2;
      else if(c == '3')
        n=3;
      else if(c == '4')
        n=4;
      else if(c == '5')
        n=5;
      else if(c == '6')
        n=6;
      else if(c == '7')
        n=7;
      else if(c == '8')
        n=8;
      else if(c == '9')
        n=9;
      else if(c == 'A')
        n=10;
      else if(c == 'B')
        n=11;
      else if(c == 'C')
        n=12;
      else if(c == 'D')
        n=13;
      else if(c == 'E')
        n=14;
      else if(c == 'F')
        n=15;
      else
        n=0;
  return n;
}

uint8_t charToUINT8(char c){
  uint8_t uc;
      if(c == '0')
        uc=0;
      else if(c == '1')
        uc=1;
      else if(c == '2')
        uc=2;
      else if(c == '3')
        uc=3;
      else if(c == '4')
        uc=4;
      else if(c == '5')
        uc=5;
      else if(c == '6')
        uc=6;
      else if(c == '7')
        uc=7;
      else if(c == '8')
        uc=8;
      else if(c == '9')
        uc=9;
      else if(c == 'A')
        uc=10;
      else if(c == 'B')
        uc=11;
      else if(c == 'C')
        uc=12;
      else if(c == 'D')
        uc=13;
      else if(c == 'E')
        uc=14;
      else if(c == 'F')
        uc=15;
      else
        uc=0;
  return uc;
}
