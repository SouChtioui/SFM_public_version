#include "BaseOTA.h"
#include "FirebaseESP8266.h"
#include "FirebaseJson.h"
#include <ESP8266WiFi.h>
#include <Arduino.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include <IRtext.h>
#include <IRutils.h>
#include <string.h>

const uint16_t kRecvPin = 14;
const uint32_t kBaudRate = 115200;
const uint16_t kCaptureBufferSize = 1024;

#if DECODE_AC
const uint8_t kTimeout = 50;
#else   // DECODE_AC
const uint8_t kTimeout = 15;
#endif  // DECODE_AC
const uint16_t kMinUnknownSize = 12;

#define LEGACY_TIMING_INFO false

IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results;

/*wifi settings*/
#define FIREBASE_HOST "****"
#define FIREBASE_AUTH "****"
#define WIFI_SSID "****"
#define WIFI_PASSWORD "****"
//Define FirebaseESP8266 data object
FirebaseData firebaseData;
/*wifi settings*/

/************************************************************************************variables declaration*********************************/
/************************************************************************************variables declaration*********************************/
/************************************************************************************variables declaration*********************************/
/************************************************************************************variables declaration*********************************/
/************************************************************************************variables declaration*********************************/
FirebaseJson jsonReference;
FirebaseJson jsonAcRefCodes;
FirebaseJson jsonAcRefStates;

String path = "/Actuals";
String protocol = "";
String code = "";
String power = "";
String temperature = "";
String actualRef="";
String refClim="";

bool starting=false;
bool allowReceive=false;
bool exitReceive=false;

int maxTemp=0;
int minTemp=100;

/************************************************************************************functions declaration*********************************/
/************************************************************************************functions declaration*********************************/
/************************************************************************************functions declaration*********************************/
/************************************************************************************functions declaration*********************************/
/************************************************************************************functions declaration*********************************/
void addDataToJSON(void);
void recIR(void);
void extractParam(String parameter);
void receiveCodes(void);
void resetVariables(void);
String getFirebaseData(String myPath);
void updateMaxMinTemp(void);
/************************************************************************************Stream functions**************************************/
void streamCallback(StreamData data)
{
  Serial.println("EVENT PATH: " + data.streamPath() + data.dataPath());
  String dataValue=data.stringData();
  if(data.dataPath()=="/start"){
    if(dataValue=="on"){
      starting=true;
      Firebase.set(firebaseData, path+"/start","off");
    }
  }
  Serial.println();
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


/************************************************************************************Setup*************************************************/
/************************************************************************************Setup*************************************************/
/************************************************************************************Setup*************************************************/
/************************************************************************************Setup*************************************************/
/************************************************************************************Setup*************************************************/
void setup() {
  
  OTAwifi();  // start default wifi (previously saved on the ESP) for OTA
  #if defined(ESP8266)
    Serial.begin(kBaudRate, SERIAL_8N1, SERIAL_TX_ONLY);
  #else  // ESP8266
    Serial.begin(kBaudRate, SERIAL_8N1);
  #endif  // ESP8266
    while (!Serial)  // Wait for the serial connection to be establised.
      delay(50);
    Serial.printf("\n" D_STR_IRRECVDUMP_STARTUP "\n", kRecvPin);
    OTAinit();  // setup OTA handlers and show IP
  #if DECODE_HASH
    // Ignore messages with less than minimum on or off pulses.
    irrecv.setUnknownThreshold(kMinUnknownSize);
  #endif  // DECODE_HASH
    irrecv.enableIRIn();  // Start the receiver

  
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
  //firebaseData.setBSSLBufferSize(1024, 1024);
  firebaseData.setBSSLBufferSize(4096, 4096);
  //Set the size of HTTP response buffers in the case where we want to work with large data.
  //firebaseData.setResponseSize(1024);
  firebaseData.setResponseSize(4096);


  if (!Firebase.beginStream(firebaseData, path))
  {
    Serial.println("------------------------------------");
    Serial.println("Can't begin stream connection...");
    Serial.println("REASON: " + firebaseData.errorReason());
    Serial.println("------------------------------------");
    Serial.println();
  }
  Firebase.setStreamCallback(firebaseData, streamCallback, streamTimeoutCallback);
/*firebase part*/
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN,HIGH);
}

/************************************************************************************loop*************************************************/
/************************************************************************************loop*************************************************/
/************************************************************************************loop*************************************************/
/************************************************************************************loop*************************************************/
/************************************************************************************loop*************************************************/



void loop() {
  if(starting){
    //get BT reference
    actualRef=getFirebaseData("/Actuals/actualRef");
    delay(100);
    Serial.println("actualRef " + actualRef);
    Firebase.set(firebaseData, path+"/actualRef","");
    delay(100);
        
    //get AC reference
    refClim=getFirebaseData("/Actuals/refClim");
    delay(100);
    Serial.println("refClim " + refClim);
    Firebase.set(firebaseData, path+"/refClim","");
    delay(100);

    //test if AC reference already exists
    if(Firebase.get(firebaseData, "/acRef/"+refClim)){
      //Success
      if(firebaseData.dataType()!="null"){ // if AC ref exists copy its codes
        Serial.println("ref exists");
        copyChild("/acRef/"+refClim+"/Buttons","/Reference/"+actualRef+"/Buttons");
      }
      else {                               // if AC ref doesn't exist allow receiving codes
        Serial.println("ref doesn't exist");
        allowReceive=true;
      }
    }
    else{
      //Failed?, get the error reason from firebaseData
  
      Serial.print("Error in get, ");
      Serial.println(firebaseData.errorReason());
    }

    
    //Start receiving code
    if(allowReceive){
      allowReceive=false;
      receiveCodes();
    }
    resetVariables();
  }
}



/************************************************************************************functions*************************************************/
/************************************************************************************functions*************************************************/
/************************************************************************************functions*************************************************/
/************************************************************************************functions*************************************************/
/************************************************************************************functions*************************************************/

/******************************************************************************************resetVariables***********************************************/
void resetVariables(void){
protocol = "";
code = "";
power = "";
temperature = "";
actualRef="";
refClim="";
starting=false;
allowReceive=false;
exitReceive=false;
jsonAcRefCodes.clear();
jsonAcRefStates.clear();
jsonReference.clear();
maxTemp=0;
minTemp=100;
}

/******************************************************************************************receiveCodes***********************************************/
void receiveCodes(void){
  digitalWrite(LED_BUILTIN,LOW);
  while(!exitReceive){
    recIR();
    delay(100);
  }
  digitalWrite(LED_BUILTIN,HIGH);
  Firebase.set(firebaseData,"/Reference/"+actualRef,jsonReference);
  Firebase.set(firebaseData,"/acRef/"+refClim+"/Buttons",jsonAcRefStates);
  Firebase.set(firebaseData,"/acRef/"+refClim+"/hexCodes",jsonAcRefCodes);
  delay(100);
}


/******************************************************************************************recIR***********************************************/
void recIR(void)
{
  char* temp[100];
  // Check if the IR code has been received.
  if (irrecv.decode(&results)) {
    digitalWrite(LED_BUILTIN,HIGH);
    // Check if we got an IR message that was to big for our capture buffer.
    if (results.overflow)
      Serial.printf(D_WARN_BUFFERFULL "\n", kCaptureBufferSize);

    String desc = resultToHumanReadableBasic(&results);
    if (desc.length()){
      extractParam(desc); // extract protocol/code/powerState/temp
    }     
    // Display any extra A/C info if we have it.
    String description = IRAcUtils::resultAcToString(&results);
    if (description.length()){
      extractParam(description);
    }

    yield();  // Feed the WDT as the text output can take a while to print.
#if LEGACY_TIMING_INFO
    // Output legacy RAW timing info of the result.
    Serial.println(resultToTimingInfo(&results));
    yield();  // Feed the WDT (again)
#endif  // LEGACY_TIMING_INFO
    // Output the results as source code
    Serial.println();
    yield();             // Feed the WDT (again)

    addDataToJSON();     // add received data to json object
  }
  OTAloopHandler();
  delay(200);
  digitalWrite(LED_BUILTIN,LOW);
}



/*****************************************************************************************************extractParam************************************/

void extractParam(String parameter){
  String str = parameter; 
  int str_len = str.length() + 1; 
  char char_array[str_len];
  str.toCharArray(char_array, str_len);
  
  char *ptr = NULL;
  ptr = strtok(char_array, ",\n");  // takes a list of delimiters
  while(ptr != NULL)
  {
      parameter = ptr;
      if(parameter.indexOf("Protocol  :")>-1){
        protocol=parameter;
        protocol=protocol.substring(12);
        Serial.println("Protocol is " + protocol);
      }
      else if(parameter.indexOf("Code      :")>-1){
        code=parameter;
        code=code.substring(12);
        code=code.substring(0,code.indexOf(" "));
        Serial.println("Code is " + code);
      }
      else if(parameter.indexOf("Power:")>-1){
        power=parameter;
        if(power.indexOf("On")>-1){
          power="On";
        }
        else if(power.indexOf("Off")>-1){
          power="Off";
        }
        Serial.println("Power is " + power);
      }
      else if(parameter.indexOf("Temp:")>-1){
        String temp=parameter;
        if(temp.indexOf("C")>-1){
          temp=temp.substring(temp.indexOf("C")-2,temp.indexOf("C"));
          temperature=temp;
          Serial.println("Temp is " + temperature);
          updateMaxMinTemp();
        }
      }
      ptr = strtok(NULL, ",");  // takes a list of delimiters
  }
}

/*****************************************************************************************************addDataToJSON*************************/
void addDataToJSON(void){
  String prot=protocol;
  prot.toLowerCase();
  if((prot!="unknown") && (power=="On")){
    jsonAcRefCodes.set("/"+temperature, code);
    jsonAcRefStates.set("/"+temperature+"/state", "unclicked");
    jsonReference.set("/Buttons/"+temperature+"/state", "unclicked");
  }
  else if((prot!="unknown") && (power=="Off")){
    jsonAcRefCodes.set("/"+power, code);
    jsonAcRefStates.set("/"+power+"/state", "unclicked");
    jsonReference.set("/Buttons/"+power, "unclicked");
    
    //Setting other parameter
    jsonAcRefStates.set("/Protocol",protocol);
    jsonAcRefStates.set("/refClim",refClim);
    jsonAcRefStates.set("/maxTemp",maxTemp);
    jsonAcRefStates.set("/minTemp",minTemp);
    jsonAcRefStates.set("/getTemp/state","unclicked");
    jsonAcRefStates.set("/getTemp/temp","24");
    jsonAcRefStates.set("/config/generalTemperature","24");
    jsonAcRefStates.set("/config/magneticSensor","10");
    jsonAcRefStates.set("/config/presenceSensor","10");

    jsonReference.set("/Buttons/Protocol",protocol);
    jsonReference.set("/Buttons/refClim",refClim);
    jsonReference.set("/Buttons/maxTemp",maxTemp);
    jsonReference.set("/Buttons/minTemp",minTemp);
    jsonReference.set("/Buttons/getTemp/state","unclicked");
    jsonReference.set("/Buttons/getTemp/temp","24");
    jsonReference.set("/Buttons/config/generalTemperature","24");
    jsonReference.set("/Buttons/config/magneticSensor","10");
    jsonReference.set("/Buttons/presenceSensor","10");

    exitReceive=true;
  }
    String jsonStr;
    Serial.println("-------------------------");

    jsonAcRefCodes.toString(jsonStr, true);
    Serial.println(jsonStr);

    Serial.println("-------------------------");

}

/*****************************************************************************************************copyChild********************************/
void copyChild(String pathFrom, String pathTo){
  if(Firebase.get(firebaseData, pathFrom)){
    //Success
    FirebaseJson &json = firebaseData.jsonObject();
    Firebase.set(firebaseData,pathTo,json); 
  }
  else{
    //Failed?, get the error reason from firebaseData
    Serial.print("Error, ");
    Serial.println(firebaseData.errorReason());
  }
}

/*****************************************************************************************************getFirebaseData********************************/
String getFirebaseData(String myPath){
  String myData="";
  if (Firebase.get(firebaseData,myPath))
  {
    if (firebaseData.dataType() == "string")
      myData=firebaseData.stringData();
  }
  else
  {
    Serial.println("FAILED");
    Serial.println("REASON: " + firebaseData.errorReason());
    Serial.println("------------------------------------");
    Serial.println();
  }
  return myData;
}

/*****************************************************************************************************updateMaxMinTemp********************************/
void updateMaxMinTemp(void){
  int myTemp=temperature.toInt();
  if(myTemp>maxTemp){
    maxTemp=myTemp;
  }
  if(myTemp<minTemp){
    minTemp=myTemp;
  }
  
}
