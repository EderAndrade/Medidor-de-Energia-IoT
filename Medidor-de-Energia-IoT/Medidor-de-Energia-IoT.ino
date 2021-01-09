
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <HardwareSerial.h>
#include <PZEM004T.h>         // Where to find this library: https://github.com/olehs/PZEM004T

/*
 *  ESP32 HARDWARE DETAILS ********************************************** 
 *  
 - To stablish communicaiont between ESP32 and PZEM004T module, ESP32 uses 
 UART2 peripheral and the pins are: IO-16(RX2) and IO-17(TX2);
 
 - To control solid state relay, ESP32 uses IO-22(D22).   

*************************************************************************/

/*
 *  DEFINES MUST BE HERE! ********************************************** 
 */
#define CORE_0    (unsigned char)0 
#define CORE_1    (unsigned char)1

#define WIFISSID  "insert your SSID here"  
#define PASSWORD  "insert your password here"   

#define IO_RELAY  (uint8_t)22
#define IO_LED    (uint8_t)2

/***********************************************************************/

/*
 *  FUNCTION PROTOTYPES MUST BE HERE! *********************************** 
 */
void prvSetupHardware (void);
void vPrintStr        (const    char *pcString);
void vPrintStrNum     (const    char *pcString, uint32_t uiValue);
void vPrintStrFloat   (const    char *pcString, float fValue);
void vPrintHex        (uint32_t uiValue);
void vHttpTask        (void     *pvParameters);
void vPzemTask        (void     *pvParameters);
/***********************************************************************/

/*
 *  GLOBAL VARIABLES MUST BE HERE! ************************************** 
 */
/* Wi-Fi --------------------------------------------------------------*/
WiFiClient      client;
WiFiMulti       wifiMulti;

/* FreeRTOS -----------------------------------------------------------*/
portMUX_TYPE    myMutex           = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t   xPzemQueue        = NULL;

/* HTTP ---------------------------------------------------------------*/
String          apiKey            = "insert your TagoIO device token here"; 
const char*     server            = "api.tago.io";

/* PZEM004T module-----------------------------------------------------*/
HardwareSerial  PzemSerial2(2);    
PZEM004T        Pzem004t(&PzemSerial2);
IPAddress       Ip(192,168,1,1);
/***********************************************************************/

/*
 *  STRUCTS MUST BE HERE! *********************************************** 
 */
typedef struct
{
  uint8_t  ucChipRev        ;// ESP32 chip revision
  uint32_t uiCpuFreq        ;// CPU frequency value
  uint32_t uiFreeHeapSize   ;// Free heap memory size
  uint64_t ulMac            ;// MAC address of ESP32
    
}SysStatus_t;

SysStatus_t stSysStatus = 
{
  .ucChipRev        = 0,
  .uiCpuFreq        = 0,
  .uiFreeHeapSize   = 0,
  .ulMac            = 0
};

typedef struct
{
  bool    bRelay    ; // State of relay: ON (1) or OFF (0)
  uint8_t fPercent  ; // Percentual of load (máx. 2A = 100%)
  float   fVoltage  ; // Voltage of line
  float   fCurrent  ; // Current consumed by load
  float   fEnergy   ; // Energy consumed by load
  float   fPower    ; // Power consumed
  
}PzemData_t;

PzemData_t stPzem = 
{
  .bRelay     = 0,
  .fPercent   = 0,
  .fVoltage   = 0,
  .fCurrent   = 0,
  .fEnergy    = 0,
  .fPower     = 0
   
};
/***********************************************************************/

/**
  * @brief  : Print strings
  * @param  : const char*
  * @retval : void
  */
void vPrintStr(const char *pcString)
{
  portENTER_CRITICAL(&myMutex);
  {
    Serial.print((char*)pcString);
    Serial.flush();
  }
  portEXIT_CRITICAL(&myMutex);
}

/**
  * @brief  : Print strings and int numbers
  * @param  : const char* and uint32_t
  * @retval : void
  */
void vPrintStrNum(const char *pcString, uint32_t uiValue)
{
  portENTER_CRITICAL(&myMutex);
  {
    char buffer[64] = {0}; 
    sprintf(buffer, "%s %lu\r", pcString, uiValue);
    Serial.println((char*)buffer);
  }
  portEXIT_CRITICAL(&myMutex);
}

/**
  * @brief  : Print strings and float numbers
  * @param  : const char* and float
  * @retval : void
  */
void vPrintStrFloat(const char *pcString, float fValue)
{
  portENTER_CRITICAL(&myMutex);
  {
    char buffer[64] = {0}; 
    sprintf(buffer, "%s %4.2f\r", pcString, fValue);
    Serial.println((char*)buffer);
  }
  portEXIT_CRITICAL(&myMutex);
}

/**
  * @brief  : Print hexadecimal values
  * @param  : uint32_t
  * @retval : void
  */
void vPrintHex(uint32_t uiValue)
{
  portENTER_CRITICAL(&myMutex);
  {
    char buffer[64] = {0}; 
    sprintf(buffer, " %08X", uiValue);
    Serial.print((char*)buffer);
  }
  portEXIT_CRITICAL(&myMutex);
}

/**
  * @brief  : HTTP task. All about connection, JSON formating and post values.
  * @param  : void *
  * @retval : void
  */
void vHttpTask(void *pvParameters)
{
  // Local variable
  PzemData_t stPzemPost = {0};
  
  //
  vPrintStr("HTTP task has started...");
    
  for(;;)
  {            
    // Wi-Fi is connected?
    if((wifiMulti.run() == WL_CONNECTED))
    {
      // Connected to network!
      digitalWrite(IO_LED, HIGH);
      
      // Waiting for receiving queue from PZEM task
      if(xQueueReceive(xPzemQueue, &stPzemPost, (TickType_t)10) == pdPASS)
      {
        //Inicia um client TCP para o envio dos dados
        if(client.connect(server, 80)) 
        {
          vPrintStr("\n\n****************************************************\n");
          vPrintStr("*************** CONNECTED TO TAGOIO ****************\n");
          vPrintStr("****************************************************\n");
          String      strPost     = ""; 
          String      strPostData = "";

          /* JSON data formatation ******************************************************************************/
          strPostData  = "[\n{\n\"variable\": \"fVoltage\",\n\"value\": " + String(stPzemPost.fVoltage) +"\n},\n";
          strPostData += "{\n\"variable\": \"fCurrent\",\n\"value\": "    + String(stPzemPost.fCurrent) +"\n},\n";
          strPostData += "{\n\"variable\": \"fEnergy\",\n\"value\": "     + String(stPzemPost.fEnergy)  +"\n},\n";
          strPostData += "{\n\"variable\": \"fPower\",\n\"value\": "      + String(stPzemPost.fPower)   +"\n},\n";
          strPostData += "{\n\"variable\": \"bRelay\",\n\"value\": "      + String(stPzemPost.bRelay)   +"\n},\n";
          strPostData += "{\n\"variable\": \"Percent\",\n\"value\": "     + String(stPzemPost.fPercent) +"\n}\n]";
          /******************************************************************************************************/
          
          strPost = "POST /data HTTP/1.1\n";
          strPost += "Host: api.tago.io\n";
          strPost += "Device-Token: "+apiKey+"\n";
          strPost += "_ssl: false\n";
          strPost += "Content-Type: application/json\n";
          strPost += "Content-Length: "+String(strPostData.length())+"\n";
          strPost += "\n";
          strPost += strPostData;

          // Post header and content
          client.print(strPost);
   
          uint32_t uiTimeout = millis();
          while(client.available() == 0) 
          {
            if(millis() - uiTimeout > 5000) 
            {
              Serial.print(">>> Client Timeout !\n");
              client.stop();
              break;
            }
          } 

          while(client.available())
          {
            String strLine = client.readStringUntil('\r');
            Serial.print(strLine);
          } 
        }
        // Close TCP/IP connection
        client.stop();
      }
    }
    else
    {
      vPrintStr("\n\n****************************************************\n");
      vPrintStr("!!!!! Trying to reconnect to local network... !!!!!!");
      vPrintStr("\n****************************************************\n\n");
      // Disconnected to local network
      digitalWrite(IO_LED, LOW);
      wifiMulti.addAP(WIFISSID, PASSWORD); 
    }
    vTaskDelay(15000/portTICK_PERIOD_MS);
  }
}

/**
  * @brief  : PZEM004T task. All about read values and cheking communicaiont with module.
  * @param  : void *
  * @retval : void
  */
void vPzemTask(void *pvParameters)
{
  //
  PzemData_t *PzemLocal = (PzemData_t*)pvParameters;
  //
  vPrintStr("PZEM004T task has started...");
    
  // Local variable just for "TEST"!
  uint32_t  uiTemp    = 0;
  float     fPzemRet  = 0;

  if(Pzem004t.setAddress(Ip))
  {
    vPrintStr("\n\n****************************************************\n"); 
    vPrintStr("*************** PZEM004T MODULE OK *****************\n");
    vPrintStr("****************************************************\n\n"); 
    digitalWrite(IO_RELAY, HIGH);  
    PzemLocal->bRelay = true;
  }
  
  // Loop
  for( ;; )
  {    
    if(Pzem004t.setAddress(Ip))
    {     
      // Read all value needed
      fPzemRet = Pzem004t.current(Ip);
      if(fPzemRet >= 0.000)
      {
        PzemLocal->fCurrent = fPzemRet;
      }      
      
      fPzemRet = 0;
      fPzemRet = Pzem004t.current(Ip);
      if(fPzemRet >= 0.000)
      {
        PzemLocal->fPercent = ((fPzemRet / 2) * 100);
      }
      
      fPzemRet = 0;
      fPzemRet = Pzem004t.voltage(Ip);     
      if(fPzemRet >= 0.000)
      {
        PzemLocal->fVoltage = fPzemRet;
      }
      
      fPzemRet = 0;
      fPzemRet = Pzem004t.energy(Ip);      
      if(fPzemRet >= 0.000)
      {
        PzemLocal->fEnergy = fPzemRet;
      }
      
      fPzemRet = 0;
      fPzemRet = Pzem004t.power(Ip);
      if(fPzemRet >= 0.000)
      {
        PzemLocal->fPower  = fPzemRet;
      }     
      
      if( (PzemLocal->fCurrent >= 0.00) &&
          (PzemLocal->fPercent >= 0.00) &&
          (PzemLocal->fVoltage >= 0.00) &&
          (PzemLocal->fEnergy  >= 0.00) &&
          (PzemLocal->fPower   >= 0.00)) 
        {        
          // Sending the queue to the HTTP task
          xQueueSendToBack(xPzemQueue, PzemLocal, 10);
        }
      
      // Print all values posted at TagoIO website
      vPrintStr     ("\n\n****************************************************\n"); 
      vPrintStr     ("********* VALUES READ FROM PZEM004T MODULE *********\n");
      vPrintStr     ("****************************************************\n");
      vPrintStrFloat("Voltage  (V): ", PzemLocal->fVoltage);
      vPrintStrFloat("Current  (A): ", PzemLocal->fCurrent);
      vPrintStrFloat("Power    (W): ", PzemLocal->fPower);
      vPrintStrFloat("Energy (W.h): ", PzemLocal->fEnergy);
      vPrintStrFloat("Percent  (%): ", PzemLocal->fPercent);        
      vPrintStrNum  ("Relay       : ", PzemLocal->bRelay);
      vPrintStr     ("****************************************************\n\n");
    }
    else
    {
      for( ;; )
      {
        vPrintStr("\n\n****************************************************\n"); 
        vPrintStr("*************** PZEM004T ERROR !!! *****************\n");
        vPrintStr("****************************************************\n\n"); 
        vTaskDelay(1000/portTICK_PERIOD_MS);
        if(Pzem004t.setAddress(Ip))
        {
          break;
        }
      }    
    }    
    // Delay
    vTaskDelay(100/portTICK_PERIOD_MS);
  }
}

/**
  * @brief  : Setting up UART, GPIOs, Wi-Fi connection, System Infos, etc.
  * @param  : void 
  * @retval : void
  */
void prvSetupHardware(void)
{  
  //
  uint32_t uiTimeout = 0;
  
  /* UART **********************************************************/
  Serial.begin(115200); 
  delay(1000);
  /*****************************************************************/  

  /* GPIO **********************************************************/ 
  pinMode     (IO_RELAY , OUTPUT);
  pinMode     (IO_LED   , OUTPUT);
  digitalWrite(IO_RELAY , LOW);
  digitalWrite(IO_LED   , LOW);
  /*****************************************************************/     

  /* Wi-Fi *********************************************************/
  // Connect to the local network
  wifiMulti.addAP(WIFISSID, PASSWORD); 
  // Delay 50ms
  delay(50);
  
  // Get current millis value
  uiTimeout = millis();
  
  // Check if is connected to local network
  while(wifiMulti.run() != WL_CONNECTED)
  {
    // Print point indicating waiting...
    vPrintStr(".");
    
    // Check timeout
    if(millis() - uiTimeout > 15000)
    {
      vPrintStr("\n\n****************************************************\n");
      vPrintStr("!!!!!!!!!!!!!! Restarting ESP32 module !!!!!!!!!!!!!\n");
      vPrintStr("****************************************************\n\n");  
      ESP.restart();
    }
    delay(500);
  }
  // Indicating that Wi-Fi is connected!
  digitalWrite(IO_LED, HIGH);
  /*****************************************************************/  

  /* Getting ESP32 system informations *****************************/
  stSysStatus.ucChipRev         = ESP.getChipRevision();
  stSysStatus.uiCpuFreq         = ESP.getCpuFreqMHz();
  stSysStatus.uiFreeHeapSize    = ESP.getFreeHeap();
  stSysStatus.ulMac             = ESP.getEfuseMac();
  /*****************************************************************/

  vPrintStr   ("\n\nXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n");
  vPrintStr   ("******* GAUGE - IoT ENERGY MEASURE EQUIPMENT *******\n");
  vPrintStr   ("************* Eder Tadeu Silva Andrade *************\n");
  vPrintStr   ("************ ESP32 + FreeRTOS + TagoIO *************\n");
  vPrintStr   ("****************************************************\n");
  vPrintStr   ("----------------------------------------------------\n");
  vPrintStr   ("****************************************************\n");
  vPrintStr   ("************* ESP32 SYSTEM INFORMATION *************\n");
  vPrintStr   ("****************************************************\n");
  vPrintStrNum("Chip revision -----------:", stSysStatus.ucChipRev);
  vPrintStrNum("CPU frequency ------(MHz):", stSysStatus.uiCpuFreq);
  vPrintStrNum("Free Heap Size -------(B):", stSysStatus.uiFreeHeapSize);
  vPrintStr   ("MAC address -------------:");
  vPrintHex   ((uint16_t)(stSysStatus.ulMac >> 32));
  vPrintHex   (stSysStatus.ulMac);
  vPrintStr   ("\nWi-Fi -------------------: OK");
  vPrintStr   ("\nIP ----------------------: ");
  Serial.println(WiFi.localIP());   
  vPrintStr   ("****************************************************\n");
  vPrintStr   ("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n\n");

  // Creating a queue to notigy HTTP task about the PZEM values read
  xPzemQueue = xQueueCreate(2, sizeof(PzemData_t));
  
  // Test if was possible to alloca a queue
  if(xPzemQueue == NULL)
  {
    vPrintStr("Impossible to allocate a Queue for PZEM task!!!\n");
  }  
}

/**
  * @brief  : Setting up hardware and creating tasks.
  * @param  : void 
  * @retval : void
  */
void setup(void) 
{
  prvSetupHardware(); 
                          
  xTaskCreatePinnedToCore(vPzemTask  , 
                          "PZEM task", 
                          (configMINIMAL_STACK_SIZE + 1024), 
                          &stPzem, 
                          2, 
                          NULL, 
                          CORE_1);
                             
  xTaskCreatePinnedToCore(vHttpTask , 
                          "HTTP task"  , 
                          (configMINIMAL_STACK_SIZE + 2048), 
                          
                          NULL, 
                          3, 
                          NULL, 
                          CORE_1);   
}

void loop(void) 
{
  vTaskDelay(10/portTICK_PERIOD_MS);
}
