#include "config.h"
#include "datatypes.cpp"
#include "Arduino.h"
#include <FlexCAN_T4.h>
#include <ArduinoJson.h>
FlexCAN_T4<CAN3, RX_SIZE_256, TX_SIZE_16> Can3; //orig RX_SIZE_256 TX_SIZE_64



// see TimeTeensy3 example for how to use time library

// use https://github.com/tonton81/FlexCAN_T4 library for receiving CAN data

// look into: https://github.com/greiman/SdFat-beta/blob/master/examples/ExFatLogger/ExFatLogger.ino
CANBus_Config can_config_1; // initilize three canbus configurations for teensy 4.1
CANBus_Config can_config_2;
CANBus_Config can_config_3;

// ******* Setup timers
#include "TeensyTimerTool.h"
using namespace TeensyTimerTool;
// This does not work when timelib.h is also included !
//// timer for LED blinking
Timer t1; // generate a timer from the pool (Pool: 2xGPT, 16xTMR(QUAD), 20xTCK)
Timer t2; // generate a timer from the pool (Pool: 2xGPT, 16xTMR(QUAD), 20xTCK)

// ***** setup sd card
// setup sd card datalogging
#include <SD.h>
// #include "SdFat.h"
#include <SPI.h>
// SdFat sd_fat;


SdFile sd_file;
char log_file_name[LOG_FILE_NAME_LENGTH] = DEFAULT_LOG_FILE_NAME;
const int chipSelect = BUILTIN_SDCARD;
uint16_t log_number = 0;


//***** setup time 
#include <TimeLib.h>
#include <time.h>
time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}

// ** begin functions
void blink_builtin_led()
{
    digitalWriteFast(LED_BUILTIN, !digitalReadFast(LED_BUILTIN));    
}

void set_current_time_in_buffer(char* str_addr){
  // format as ISO datetime like 2021-01-10T20:14:09Z
  time_t seconds = getTeensy3Time();
  strftime(str_addr, 21, "%Y-%m-%dT%H:%M:%SZ", gmtime(&seconds));
}

void serial_print_current_time(){
  char time_buf[21];
  set_current_time_in_buffer(time_buf);
  Serial.println(time_buf);
}

// void fatDateTime(uint16_t *date, uint16_t *time) {
//   *date = FAT_DATE(localTime.year, localTime.month,  localTime.date);
//   *time = FAT_TIME(localTime.hours,localTime.minutes,localTime.seconds);
// }

#define TIME_HEADER  "T"   // Header tag for serial time sync message

unsigned long processSyncMessage() {
  unsigned long pctime = 0L;
  const unsigned long DEFAULT_TIME = 1610331025; // Jan 10 2021 

  if(Serial.find(TIME_HEADER)) {
     pctime = Serial.parseInt();
     return pctime;
     if( pctime < DEFAULT_TIME) { // check the value is a valid time (greater than Jan 1 2013)
       pctime = 0L; // return 0 to indicate that the time is not valid
     }
  }
  return pctime;
}

void check_serial_time(){
  if (Serial.available()) {
    time_t t = processSyncMessage();
    if (t != 0) {
      Teensy3Clock.set(t); // set the RTC
      Serial.print("Set new serial time: ");
      serial_print_current_time();
    }
  }
}

int start_log(){
  File dataFile = SD.open(log_file_name, FILE_WRITE);
  // if the file is available, write to it:
  if (dataFile) {
  // digital clock display of the time
    dataFile.println(HEADER_CSV);
  }
  else{
    Serial.println("file not opened!");
    return 0;
  }
  dataFile.close();
  return 1;
}

void set_default_config(CANBus_Config* config, uint8_t port){
  Serial.print("Setting default config for bus: ");
  Serial.println(port);
  config->port=port;
  sprintf(config->bus_name, "CAN%d", port);
  config->baudrate=DEFAULT_BAUD_RATE;
  config->id_filter_mask=0;
  config->id_filter_value=0;
  config->log_ext=true;
  config->log_std=true;
}

void print_bus_config(CANBus_Config* config){
  Serial.print("Bus config <bus number: ");
  Serial.print(config->port, DEC);
  Serial.print(", bus name: ");
  Serial.print(config->bus_name);
  Serial.print(", baudrate: ");
  Serial.print(config->baudrate);
  Serial.print(", log_std: ");
  Serial.print(config->log_std);
  Serial.print(", log_ext: ");
  Serial.print(config->log_ext);
  Serial.print(", id_filter_mask");
  Serial.print(config->id_filter_mask);
  Serial.print(", id_filter_value");
  Serial.print(config->id_filter_value);
  Serial.println(">");
}

void set_config_from_jsonobject(JsonObject json_obj, CANBus_Config* config){
  config->baudrate = json_obj["baudrate"] | DEFAULT_BAUD_RATE;
  strlcpy(config->bus_name, json_obj["bus_name"] | "BUS1", sizeof(config->bus_name));
  config->port = 1;
  config->log_ext = json_obj["log_extended_frames"] | true;
  config->log_std = json_obj["log_standard_frames"] | true;
  config->id_filter_mask = json_obj["id_filter_mask"] | 0;
  config->id_filter_value = json_obj["id_filter_value"] | 0;
  ;

}

int read_config_file() {
  // to do: implement reading config file
  // baud, ack, log_std, log_ext,
  Serial.println("reading Config file");
  File config_file = SD.open(CONFIG_FILE_NAME, FILE_READ);
  StaticJsonDocument<512> config_doc;
  DeserializationError error = deserializeJson(config_doc, config_file);
  config_file.close();
  JsonObject config_root = config_doc.as<JsonObject>();
  if (error){
    Serial.println(F("Failed to read file, using default configuration"));
    set_default_config(&can_config_1, 1);
    set_default_config(&can_config_2, 2);
    set_default_config(&can_config_3, 3);
    return 1;
  }
  JsonObject temp_object;

  if (config_root.containsKey("can1")){ // check if it is not zero or none, if so then process
    temp_object = config_root["can1"];
    set_config_from_jsonobject(temp_object, &can_config_1);
  }
  else 
    set_default_config(&can_config_1, 1);
  if (config_root.containsKey("can2")){
    temp_object = config_root["can2"];
    set_config_from_jsonobject(temp_object, &can_config_2);
  }
  else
    set_default_config(&can_config_2, 2);
  if (config_root.containsKey("can3")){
    temp_object = config_root["can3"];
    set_config_from_jsonobject(temp_object, &can_config_3);
  }
  else
    set_default_config(&can_config_3, 3);
  return 1;
}

void can_frame_to_str(const CAN_message_t &msg, char* sTmp){
  set_current_time_in_buffer(sTmp);
  sprintf(sTmp+strlen(sTmp), ",%X", (unsigned int)msg.id);
  sprintf(sTmp+strlen(sTmp), ",%d", (unsigned int)msg.flags.extended);
  sprintf(sTmp+strlen(sTmp), ",%d", (unsigned int)msg.bus);
  sprintf(sTmp+strlen(sTmp), ",%d", (unsigned int)msg.len);
  for (int i=0; i<msg.len; i++){
    sprintf(sTmp+strlen(sTmp), ",%0.2X", msg.buf[i]);
  }
  strcat(sTmp, "\r\n");
}

void write_sd_line(char* line){
  // open the file.
  File dataFile = SD.open(log_file_name, FILE_WRITE);
  // if the file is available, write to it:
  if (dataFile) {
  // digital clock display of the time
    dataFile.print(line);
  }
  else{
    Serial.println("file not opened!");
  }
  dataFile.close();
  // if the file isn't open, pop up an error:
}

void can_callback(const CAN_message_t &msg) {
  Serial.println("doing CAN callback");
  //todo: determine how CAN messages are received

  // filter by extended and standard
  //filter by ID filter
    // id filtering in akpc CAN_Logger uses if ((rxmsg.EID & iFilterMask) != (iFilterValue & iFilterMask)) continue;
  char temp_str[128];
  can_frame_to_str(msg, temp_str);
  Serial.print("Got CAN message: ");
  Serial.print(temp_str);
  write_sd_line(temp_str);
}


void set_next_log_filename(char* in_file){
  uint16_t file_number_to_try = 0;
  // char file_to_try[LOG_FILE_NAME_LENGTH];
  // strcpy(file_to_try, file);
  sprintf(&in_file[LOG_FILE_NUM_POS], "%03d", file_number_to_try);
  in_file[LOG_FILE_DOT_POS] = '.';

  bool next_file_found = false;
  while (!next_file_found){
    if (!SD.exists(in_file)){
      next_file_found = true;
      return;
    }
    file_number_to_try ++;
    sprintf(&in_file[LOG_FILE_NUM_POS], "%03d", file_number_to_try);
    in_file[LOG_FILE_DOT_POS] = '.';
  }
}

void setup() {
  Serial.begin(115200); 
  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
  }
  // if (!sd_fat.begin(chipSelect, SPI_HALF_SPEED)) sd.initErrorHalt();

  #ifdef DEBUG
    delay(1000); // delay 1 seconds to allow computer to open serial connection
  #endif
  pinMode(LED_BUILTIN, OUTPUT);

  if (!read_config_file()) Serial.println("Config File read error!");
  print_bus_config(&can_config_1);
  print_bus_config(&can_config_2);
  print_bus_config(&can_config_3);

  // setup CANBus
  Can3.begin();
  Can3.setBaudRate(can_config_1.baudrate);
  Can3.enableFIFO();
  Can3.enableFIFOInterrupt();
  Can3.onReceive(can_callback);

  
  Serial.println("Started");
  setSyncProvider(getTeensy3Time);
  
  set_next_log_filename(log_file_name);
  Serial.print("Logging on file:");
  Serial.println(log_file_name);
  start_log();
    
  if (timeStatus()!= timeSet) {
    Serial.println("Unable to sync with the RTC");
  } else {
    Serial.println("Sync with RTC complete");
  }

  //Setup Periodic blink timer 
//  pinMode(LED_BUILTIN,OUTPUT);  
  t1.beginPeriodic(blink_builtin_led, 100'000); // 100ms blink every 100ms
//
  // t2.beginPeriodic(write_sd_data, 2000'000); // write to sd card every 2s

  Serial.print("Current Time: ");
  serial_print_current_time();
}

unsigned long target_time = 0L ;
#define ONE_SECOND_PERIOD 1*1000
#define TEN_SECOND_PERIOD 10*1000

void loop ()
{
  Can3.events();
  if (millis () - target_time >= TEN_SECOND_PERIOD)
  {
    target_time += TEN_SECOND_PERIOD ;   // change scheduled time exactly, no slippage will happen
  //    write_sd_data();
  //    Serial.print("Current Time: ");
  //    print_current_time();
  //    blink_builtin_led();
  }

  check_serial_time();
}

