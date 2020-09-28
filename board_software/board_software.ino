/*
This is firmware that runs on an ESP8266 connected to a geiger counter kit. To view the data logged onboard, connect to the wifi set in the top of this script.
Then open a web browser to "192.168.4.1".
 */

//-------------------USER SETTINGS-------------------
//SSID and Password to your ESP Access Point
const char* ssid = "ESPWebServer";
const char* password = "RADIATION";

const int inputPin = 5; //This is just the pin my hardware is connected to

//Bin count combines with bin duration to end up setting how long our running buffer will store data.
//Note that we add an extra bin - we want to be able to keep track of recent counts, without counting the currently active bin.
const int bin_count = 13;
//Increasing bin duration will increase reliability of measurements while decreasing time resolution (small bins are more affected by the particular moment of radiation events)
const int bin_duration = 5;
//-------------------END USER SETTINGS----------------

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

//File system, allows logging to spiffs
#include "FS.h"

int bins[bin_count];
const int all_bins_duration = (bin_count-1) * bin_duration;

unsigned long startTime;
unsigned long current_second = 0;
byte current_bin = 0;
//Total number of counts that have happened since startup
long full_counts = 0;
//Counts that have triggered an interrupt that have not yet been processed by the main processing function
volatile byte queued_counts = 0;

ESP8266WebServer server(80); //Server on port 80
int counts_in_bins(){
  int total_counts = 0;
  for(byte i = 0; i<bin_count; i++)
  {
    //Don't include the current bin when adding the total
    if (i != current_bin)
      total_counts += bins[i];
  }
  return total_counts;
}
unsigned long runtime(){
  return millis() - startTime;
}
void handleRoot() {
  String message = "";
  //Debug stuff
  //message += "Current bin: " + String(current_bin) + "\n"; 
  //message += ("Bin " + String(i) + ": " + String(bins[i]) + "\n"); 
  
  message += ("Counts in past " + String(all_bins_duration)+ " seconds: " + String(counts_in_bins()) + "\n");
  message += ("Total counts since startup: " + String(full_counts) + "\n");
  message += ("Minutes since startup: " + String(runtime()/60000) + "\n");
  message += ("Average CPM since startup: " + String(full_counts / ((float)runtime()/60000)) + "\n");
  server.send(200, "text/plain", message);
}
void dumpCSV(){
  Serial.println("CSV requested");
  File download = SPIFFS.open("/temp.csv", "r");
  if (download) {
    
    server.sendHeader("Content-Type", "text/text");
    server.sendHeader("Content-Disposition", "attachment; filename=/temp.csv");
    server.sendHeader("Connection", "close");
    server.streamFile(download, "application/octet-stream");
    //For some reason this fails, I'd like to duplicate the data on serial but it doesn't work.
//    for(int i=0;i<download.size();i++) //Read upto complete file size
//      {
//        Serial.print((char)download.read());
//      }
    download.close();

  } else Serial.println("Download fail");
}
void userFormat(){
  server.send(200, "text/plain", "Starting Formatting");
  SPIFFS.format();
  server.send(200, "text/plain", "Format complete!");

}
 
void setup(void){
  Serial.begin(9600);
  if(SPIFFS.begin()){
    Serial.println("Opened fine");
  }
  if(!SPIFFS.exists("/formatComplete.txt")){
    Serial.println("SPIFFS formatting attempt");  
     if(SPIFFS.format())
     {
        Serial.println("format fine");
        File f = SPIFFS.open("/formatComplete.txt","w");
        if(!f){
          Serial.println("Failed to write format saver");
        }
        else{
          f.println("Format Complete");
        }
     }
     else{
      Serial.println("Format failed");
     }
  }
  else{
    Serial.println("SPIFFS exists");
  }
 
  File f = SPIFFS.open("/temp.csv","a");

  WiFi.mode(WIFI_AP);           //Only Access point
  WiFi.softAP(ssid, password);  //Start HOTspot removing password will disable security
 
  IPAddress myIP = WiFi.softAPIP(); //Get IP address
  Serial.print("HotSpt IP:");
  Serial.println(myIP);

  server.on("/", handleRoot);      //Which routine to handle at root location
  server.on("/csv",dumpCSV);
  server.on("/fmt",userFormat);
  server.begin();                  //Start server
  Serial.println("HTTP server started");
  
  pinMode(inputPin,INPUT);
  attachInterrupt(digitalPinToInterrupt(inputPin), handleInterrupt, FALLING); 
  startTime = millis();
}

void loop(void){
  while(queued_counts > 0){
    queued_counts--;
    full_counts++;
    bins[current_bin]++;
  }
  //If the time has now moved past what we previously held as the "current" second
  if((runtime() / 1000) > current_second)
  {
    //Update the current second, so that this function won't happen again until the next second.
    current_second = runtime() / 1000;
    //If we're at a multiple of the bin duration, it's time to move on to the next bin.
    if ((current_second % bin_duration) == 0)
    {
      //This moves to the next bin, and if we're at the bin count, the modulo takes us back to zero.
      current_bin = (current_bin + 1) % bin_count;
      //Now that we're in a new bin, zero it out to begin collecting data, ditching what was in the bin before.
      bins[current_bin] = 0;
    }
    //Every minute, log the counts in past minute into the spiffs.
    if ((current_second % 60) == 0)
    {
      File tempLog = SPIFFS.open("/temp.csv", "a"); // Write the time and the temperature to the csv file
      if (!tempLog) {
        Serial.println("file open failed");
      }
      else{
        Serial.println("Logging counts");
      }
      tempLog.print(current_second / 60);
      tempLog.print(',');
      tempLog.println(counts_in_bins());
      tempLog.close();
    }
  }
  server.handleClient();          //Handle client requests
  delay(100);
}
ICACHE_RAM_ATTR void handleInterrupt() {
    queued_counts++;
}
