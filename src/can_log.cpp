#include <FlexCAN_T4.h>
#include <SD.h>
#include "datatypes.h"
#include "config.h"
#include "rgb_led.h"
#include "time_manager.h"
#include "can_log.h"
#include "config_manager.h"
#include <EEPROM.h>

File SD_CAN_Logger::data_file;

SD_CAN_Logger::SD_CAN_Logger(Config_Manager* _config){
  config = _config;
}

void SD_CAN_Logger::flush_sd_file(){
  set_led_from_status(writing_sd);
  data_file.flush();
  delay(50);
  set_led_from_status(waiting_for_data);
}

void SD_CAN_Logger::set_time_since_log_start_in_buffer(char* sTmp){
  sprintf(sTmp, "%3.3f", (millis()-log_start_millis)/1000.0);
}

int SD_CAN_Logger::start_log(){
  log_enabled = false;
  data_file.close();
  data_file = SD.open(log_file_name, FILE_WRITE);
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
  return 1;
}

void SD_CAN_Logger::can_frame_to_str(const CAN_message_t &msg, char* sTmp){
  set_time_since_log_start_in_buffer(sTmp);
  sprintf(sTmp+strlen(sTmp), ",%d", (unsigned int)msg.bus);
  sprintf(sTmp+strlen(sTmp), ",%d", (unsigned int)msg.flags.extended);
  sprintf(sTmp+strlen(sTmp), ",%X", (unsigned int)msg.id);
  sprintf(sTmp+strlen(sTmp), ",%d", (unsigned int)msg.len);
  for (int i=0; i<msg.len; i++){
    sprintf(sTmp+strlen(sTmp), ",%0.2X", msg.buf[i]);
  }
  strcat(sTmp, "\r\n");
}

void SD_CAN_Logger::set_next_log_filename(){
  // char file_to_try[LOG_FILE_NAME_LENGTH];
  // strcpy(file_to_try, file);
  // EEPROM.get(0, file_number_to_try);
  // Serial.print("Trying file from eeprom: ");
  // Serial.println(file_number_to_try);
  if (file_number_to_try > 999) {
    sprintf(&log_file_name[LOG_FILE_NUM_POS-1], "%04d", file_number_to_try);  
  }
  else{
    sprintf(&log_file_name[LOG_FILE_NUM_POS], "%03d", file_number_to_try);
  }
  log_file_name[LOG_FILE_DOT_POS] = '.';
  file_number_to_try = 0;
  // todo: debug eeprom idea

  bool next_file_found = false;
  while (!next_file_found){
    if (!SD.exists(log_file_name)){
      next_file_found = true;
      EEPROM.put(0, file_number_to_try+1);
      return;
    }
    file_number_to_try ++;
    if (file_number_to_try > 999) {
      sprintf(&log_file_name[LOG_FILE_NUM_POS-1], "%04d", file_number_to_try);
    }
    else{
      sprintf(&log_file_name[LOG_FILE_NUM_POS], "%03d", file_number_to_try);
    }
    log_file_name[LOG_FILE_DOT_POS] = '.';
  }
}

void SD_CAN_Logger::get_log_filename(char* name){
    strcpy(name, log_file_name);
}

void SD_CAN_Logger::write_sd_line(char* line){
  // open the file.
  // if the file is available, write to it:
  if (!log_enabled)
    return;
  if (data_file) {
    data_file.print(line);

    if (data_file.size() > max_log_size){
      data_file.close();
      set_next_log_filename();
      start_log();
    }
  }
  else{
    // if the file isn't open, pop up an error:
    Serial.println("file not opened! opening and trying again");
    data_file = SD.open(log_file_name, FILE_WRITE);
    write_sd_line(line);
  }
}
