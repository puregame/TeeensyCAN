#include <FlexCAN_T4.h>
#include <SdFat.h>
#include "datatypes.h"
#include "config.h"
#include "rgb_led.h"
#include "time_manager.h"
#include "can_log.h"
#include "config_manager.h"
#include "helpers.h"
#include <EEPROM.h>

extern SdFs sd;
extern SD_CAN_Logger sd_logger;

FsFile SD_CAN_Logger::data_file;

SD_CAN_Logger::SD_CAN_Logger(Config_Manager* _config){
  config = _config;

  // get the latest first and next log file numbers
  EEPROM.get(EEPROM_LOCATION_FIRST_LOG_NUM, first_log_file_number);
  EEPROM.get(EEPROM_LOCATION_NEXT_LOG_NUM, next_log_file_number);
  if (next_log_file_number > MAX_LOG_NUMBER | first_log_file_number > MAX_LOG_NUMBER){
    next_log_file_number = 1;
    first_log_file_number = 0;
    EEPROM.put(EEPROM_LOCATION_FIRST_LOG_NUM, next_log_file_number);
    EEPROM.put(EEPROM_LOCATION_FIRST_LOG_NUM, first_log_file_number);
  }
  #ifdef DEBUG
    Serial.print("EEPROM first log file number: ");
    Serial.println(first_log_file_number);
    Serial.print("EEPROM next log file number: ");
    Serial.println(next_log_file_number);
  #endif
}

void SD_CAN_Logger::flush_sd_file(){
  // check if write buffer is not empty, if it has content then write to the file then flush
  if (strlen(sd_logger.write_buffer) > 0){
    data_file.print(sd_logger.write_buffer);
    sd_logger.write_buffer[0]='\0'; // clear the write buffer
  }

  set_led_from_status(writing_sd);
  data_file.flush();
  delay(40);
  set_led_from_status(waiting_for_data);
}

void SD_CAN_Logger::reopen_file(){
  #ifdef DEBUG
    Serial.println("Reopening CAN log data file.");
  #endif
  uint64_t pos = data_file.position();
  data_file.close();
  data_file.open(log_file_name, FILE_WRITE);
  data_file.seek(pos);
}

void SD_CAN_Logger::set_time_since_log_start_in_buffer(char* sTmp){
  sprintf(sTmp, "%3.3f", (millis()-log_start_millis)/1000.0);
}

int SD_CAN_Logger::start_log(){
  log_enabled = false;
  data_file.close();
  data_file = sd.open(log_file_name, FILE_WRITE);
  // if the file is available, write to it:
  if (data_file) {
    // print header file in the log
    data_file.print("{\"unit_type\": \"");
    data_file.print(config->unit_type);
    data_file.print("\", \"unit_number\": \"");
    data_file.print(config->unit_number);
    data_file.print("\", \"can_1\": ");
    single_can_log_config_str[0] = '\0';
    config->bus_config_to_str(0, single_can_log_config_str);
    data_file.print(single_can_log_config_str);
    single_can_log_config_str[0] = '\0';
    data_file.print(", \"can_2\": ");
    config->bus_config_to_str(1, single_can_log_config_str);
    data_file.print(single_can_log_config_str);
    single_can_log_config_str[0] = '\0';
    data_file.print(", \"can_3\": ");
    config->bus_config_to_str(2, single_can_log_config_str);
    data_file.print(single_can_log_config_str);
    single_can_log_config_str[0] = '\0';
    data_file.print(", \"log_start_time\": \"");
    char s_tmp[30];
    set_current_time_in_buffer(s_tmp);
    data_file.print(s_tmp);
    data_file.println("\"}");
    log_start_millis = millis();
    data_file.println(HEADER_CSV);
  }
  else{
    Serial.println("file not opened!");
    return 0;
  }
  data_file.flush();
  log_enabled=true;
  if (first_log_file_number == 0){
    // if this is the first time we started a log then set first log accordingly
    first_log_file_number = 1;
    EEPROM.put(EEPROM_LOCATION_FIRST_LOG_NUM, first_log_file_number);
  }
  return 1;
}

void SD_CAN_Logger::can_frame_to_str(const CAN_message_t &msg, char* sTmp){
  set_time_since_log_start_in_buffer(sTmp);
  sprintf(sTmp+strlen(sTmp), ",%d", (unsigned int)msg.bus);
  sprintf(sTmp+strlen(sTmp), ",%d", (unsigned int)msg.flags.extended);
  sprintf(sTmp+strlen(sTmp), ",%X", (unsigned int)msg.id);
  sprintf(sTmp+strlen(sTmp), ",%d", (unsigned int)msg.len);
  byte len = min(msg.len, 8);
  for (int i=0; i<len; i++){
    sprintf(sTmp+strlen(sTmp), ",%0.2X", msg.buf[i]);
  }
  strcat(sTmp, "\r\n");
}

int SD_CAN_Logger::get_current_log_count(){
  // return count of log files including log that we are currently logging to
  return first_log_file_number - next_log_file_number + 1;
}

void SD_CAN_Logger::set_next_log_filename(){
  // search for and set the next log file name (log_file_name) into this object
  // returns: none

  // check if we should be overwriting old files
  if (overwriting_old_files){
    // delete first log file and increment
    if (next_log_file_number == first_log_file_number){
      sprintf_num_to_logfile_name(next_log_file_number, log_file_name);
      sd.remove(log_file_name);
      next_log_file_number++;
      first_log_file_number++;
      EEPROM.put(EEPROM_LOCATION_NEXT_LOG_NUM, next_log_file_number);
      EEPROM.put(EEPROM_LOCATION_FIRST_LOG_NUM, first_log_file_number);
      return; // file has been removed, it can now be used
    }
  }

  // if first log file does not exist then assume SD card has been cleared and start logging at log 1
  sprintf_num_to_logfile_name(first_log_file_number, log_file_name);
  if (!sd.exists(log_file_name)){
    next_log_file_number = 1;
    first_log_file_number = 0;
    sprintf_num_to_logfile_name(next_log_file_number, log_file_name);
    EEPROM.put(EEPROM_LOCATION_NEXT_LOG_NUM, next_log_file_number);
    EEPROM.put(EEPROM_LOCATION_FIRST_LOG_NUM, first_log_file_number);
    return;
  }
  
  if (next_log_file_number > MAX_LOG_NUMBER){
    //wrap number around when it gets to MAX_LOG_NUMBER
    next_log_file_number = 1;
  }
  // first test existing file
  sprintf_num_to_logfile_name(next_log_file_number, log_file_name);

  if (!sd.exists(log_file_name)){ 
    // next log file that we tried does not exist then use this number, increment the next one and use this file name
    next_log_file_number++;
    EEPROM.put(EEPROM_LOCATION_NEXT_LOG_NUM, next_log_file_number);
    return; // return with this log_file_name as the one to use
  }
  else{
    // next log file that we tried does exist, what to do?
    // if next log file is 0 then look though all logs?
    uint16_t log_file_to_try = next_log_file_number;
    log_file_to_try++;
    do {
      if (log_file_to_try > MAX_LOG_NUMBER){
        log_file_to_try = 1;
      }
      sprintf_num_to_logfile_name(log_file_to_try, log_file_name);
      if (!sd.exists(log_file_name)){
        // this file does not exist, use it
        next_log_file_number = log_file_to_try;
        EEPROM.put(EEPROM_LOCATION_NEXT_LOG_NUM, next_log_file_number);
        return;
      }
      log_file_to_try++;
    }
    while(log_file_to_try != first_log_file_number); // try all file numbers until we get back to first log file

    // if we get here then there are MAX_LOG_NUMBER log files, none available.
    Serial.println("All log files up to first log file are used, checking if we should overwrite from config");
    if (config->overwrite_logs){
      // delete the oldest log, use that log.
      // Short circuit this algorithm to overwrite next time a new log is needed
      // assume next_log_file_number is the one to delete
      sprintf_num_to_logfile_name(first_log_file_number, log_file_name);
      sd.remove(log_file_name);
      first_log_file_number ++;
      next_log_file_number = first_log_file_number;
      EEPROM.put(EEPROM_LOCATION_NEXT_LOG_NUM, next_log_file_number);
      EEPROM.put(EEPROM_LOCATION_FIRST_LOG_NUM, first_log_file_number);
      overwriting_old_files = true;
      return; // file has been removed, it can now be used
    }
    else{
      // Stop logging
      Serial.println("Stopping all logging, will not overwrite logs");
      log_enabled=false;
    }
  }
}

void SD_CAN_Logger::check_sd_free_space(){
  // checks free space available on SD card, 
    // if free space is not at least 2*max_log_size and log overwrite is enabled 
      // then delete oldest files until 2* max_log_size is available on card
  // todo: implement
  #ifdef DEBUG
    Serial.println("Checking free space on SD Card");
  #endif
}

void SD_CAN_Logger::get_log_filename(char* name){
    strcpy(name, log_file_name);
}

void SD_CAN_Logger::print_end_log_line(){
  data_file.println(EOF_CAN_LOGFILE);
}

void SD_CAN_Logger::restart_logging(){
  data_file.close();
  set_next_log_filename();
  start_log();
}

void SD_CAN_Logger::write_sd_line(char* line){
  // open the file.
  // if the file is available, write to it:
  if (!log_enabled)
    return;
  if (no_write_file){
    if (strlen(write_buffer) + strlen(line) > SD_WRITE_BUFFER_LEN){
      Serial.println("ERROR: sd file buffer overrun!");
      return;
    }
    strcat(write_buffer, line);
  }
  else{
    if (data_file) {
      if (strlen(write_buffer) > 0){
        data_file.print(write_buffer);
        write_buffer[0]='\0'; // clear the write buffer
      }
      data_file.print(line);

      if (data_file.size() > max_log_size){
        restart_logging();
      }
    }
    else{
      // if the file isn't open, pop up an error:
      #ifdef DEBUG
        Serial.println("file not opened! opening and trying again");
      #endif
      data_file = sd.open(log_file_name, FILE_WRITE);
      write_sd_line(line);
    }
  }
}
