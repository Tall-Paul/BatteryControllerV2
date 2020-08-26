#include "time.h"
#include "wifi_location.h"
#include "EspMQTTClient.h"
#include "secrets.h"

//pins
#define ABIT0 26
#define ABIT1 25
#define ABIT2 17
#define VOLTS 2
#define RELAY_5V 23
#define RELAY_CHARGE 5
#define BATT1_PIN 2
#define BATT2_PIN 0
#define CAR_ACC 14
#define WAKE_PIN GPIO_NUM_14

#define MAX_TIME_TO_CHARGE 1800
#define LOW_BATTERY 12.6
#define VOLTAGE_CAR_RUNNING 13.0


//sleep setting
#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  3600ULL       /* Time ESP32 will go to sleep (in seconds) */

//wifi / mqtt


//misc
bool car_is_running = false;
double leisure_volts = 0.00;
double main_volts = 0.00;
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;
bool debug = false;
int loop_counter = 0;
int send_on_loop = 30;
int not_connected_timer = 0;
int charger_connected_timer = 0;
int start_charging_timer = 0;

void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : break;
  }
}

EspMQTTClient client(
  WIFI_SSID,
  WIFI_PASSWORD,
  MQTT_ENDPOINT,  // MQTT Broker server ip
  MQTT_USERNAME,   // Can be omitted if not needed
  MQTT_PASSWORD,   // Can be omitted if not needed
  "BatteryCharger",     // Client // that uniquely identify your device
  1883              // The MQTT port, default to 1883. this line can be omitted
);

String current_lat = "0.00";
String current_lon = "0.00";
String current_pos_type = "GPS";

void getCurrentLocation(){
  //current_lat = String(gps.location.lat(),6);
  //current_lon = String(gps.location.lng(),6);
  
  //if (current_lat == "0.000000" || current_lon == "0.000000"){
    location_t loc = getGeoFromWiFi();
    current_lat = String(loc.lat, 7);
    current_lon = String(loc.lon, 7);
    current_pos_type = "Wifi";   
  //}
}

String build_payload(){
  
  Serial.print("Building Payload");
  getCurrentLocation();
  String data_payload = "{\
    \"battery1\": "+String(main_volts,2)+",\
    \"battery2\": "+String(leisure_volts,2)+",\
    \"engine\": "+String(car_is_running)+",\
    \"charging\": "+String(get_charging())+",\
    \"latitude\": \""+current_lat+"\",\
    \"longitude\": \""+current_lon+"\",\
    \"provider\": \""+current_pos_type+"\",\
    \"time\": \""+getTimeString()+"\",\   
  }\
";
  Serial.println("Payload built");
  Serial.println(data_payload);
  return data_payload;
}

void publish_payload(){     
    if (client.isConnected()){
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);  
      String data_payload = build_payload();
      client.publish(channel_prefix+"/status/json",data_payload,true);         
    } else {
      Serial.println("Unable to publish data");
    }
}

void onConnectionEstablished()
{
    client.subscribe(channel_prefix+"/control/reboot", [](const String & payload) {
      if (payload == "1")
          ESP.restart();
    });

    client.subscribe(channel_prefix+"/control/debug", [](const String & payload) {
      if (payload == "1")
          debug = 1;
      else
          debug = 0;
    });

     
    loop_counter = 50;
    send_on_loop = 60;
    
}

void doChargingLogic(){
  if (get_charging() == 0){
      if (leisure_volts < LOW_BATTERY){ //we need to charge
        if (main_volts > VOLTAGE_CAR_RUNNING){ //car is running        
          start_charging();
        } 
      } 
    } else { //already charging
      check_charging_cycle();
    }
}

int get_charging(){
  return digitalRead(RELAY_CHARGE);
}

void start_charging(){
  if (start_charging_timer >= 10){
    charger_connected_timer = MAX_TIME_TO_CHARGE;
    set_charging(1);
  } else {
    if (leisure_volts < LOW_BATTERY)
      start_charging_timer++;
    if (leisure_volts > LOW_BATTERY && start_charging_timer > 0)
      start_charging_timer--;
  }
}

void stop_charging(){
  charger_connected_timer = 0;
  set_charging(0);
}


void check_charging_cycle(){
  if (get_charging == 0)
    return;
  if (main_volts <= VOLTAGE_CAR_RUNNING){
    stop_charging();
  }
    
  if (charger_connected_timer > 0){
    charger_connected_timer--;
    return;
  }
  //need to check current leisure voltage on next loop so stop charging  
  stop_charging();  
}

void set_charging(int state){
  if (state == 1){    
    digitalWrite(RELAY_CHARGE,HIGH);    
  } else {
    digitalWrite(RELAY_CHARGE,LOW);
  }
}



String getTimeString(){
  struct tm timeinfo;

  if(!getLocalTime(&timeinfo)){    
    return "";
  } else {
    char timeHour[3];
    char timeMinute[3];
    char dateMonth[3];
    char dateDay[3];
    char dateYear[5];
    strftime(timeHour,3, "%H", &timeinfo);
    strftime(timeMinute,3, "%M", &timeinfo);
    strftime(dateMonth,3, "%m", &timeinfo);
    strftime(dateDay,3, "%d", &timeinfo);
    strftime(dateYear,5, "%Y", &timeinfo);
    return String(dateYear)+"/"+String(dateMonth)+"/"+String(dateDay)+" "+String(timeHour)+":"+String(timeMinute);
  }
}

void go_to_sleep(){
  Serial.println("Going to sleep");
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_sleep_enable_ext0_wakeup(WAKE_PIN,1);
  esp_deep_sleep_start();
}


double multiRead(int pin){
  switch (pin){
    case 0:
      digitalWrite(ABIT0,LOW);
      digitalWrite(ABIT1,LOW);
      digitalWrite(ABIT2,LOW);
      break;
    case 1:
      digitalWrite(ABIT0,LOW);
      digitalWrite(ABIT1,HIGH);
      digitalWrite(ABIT2,LOW);
      break;
    case 2:
      digitalWrite(ABIT0,HIGH);
      digitalWrite(ABIT1,LOW);
      digitalWrite(ABIT2,LOW);
      break;
    case 3:
      digitalWrite(ABIT0,HIGH);
      digitalWrite(ABIT1,HIGH);
      digitalWrite(ABIT2,LOW);
      break;
    case 4:
      digitalWrite(ABIT0,LOW);
      digitalWrite(ABIT1,LOW);
      digitalWrite(ABIT2,HIGH);
      break;
    case 5:
      digitalWrite(ABIT0,HIGH);
      digitalWrite(ABIT1,LOW);
      digitalWrite(ABIT2,HIGH);
      break;
    case 6:
      digitalWrite(ABIT0,LOW);
      digitalWrite(ABIT1,HIGH);
      digitalWrite(ABIT2,HIGH);
      break;
    case 7:
      digitalWrite(ABIT0,HIGH);
      digitalWrite(ABIT1,HIGH);
      digitalWrite(ABIT2,HIGH);
      break;    
  }
  return analogRead(VOLTS);
}

double ReadVoltage(int pin){
  double pin_voltage;
  double rounded;
  pin_voltage = multiRead(pin);
  //Serial.println(pin_voltage);
  pin_voltage = pin_voltage * 0.015625; 
  rounded = round(pin_voltage * 10) / 10;
  if (rounded < 5)
    rounded = 0.00;
  return rounded;
} 

void read_sensors(){
  leisure_volts = ReadVoltage(BATT2_PIN);
  main_volts = ReadVoltage(BATT1_PIN);    
  if (main_volts > 0)
    main_volts = main_volts + 0.20;   
  car_is_running = digitalRead(CAR_ACC);
}


void setup() {
  pinMode(ABIT0, OUTPUT);
  pinMode(ABIT1, OUTPUT);
  pinMode(ABIT2,OUTPUT);
  pinMode(RELAY_5V,OUTPUT);
  pinMode(RELAY_CHARGE,OUTPUT);
  pinMode(VOLTS,INPUT);
  Serial.begin(9600);  
  //enable 5v
  digitalWrite(RELAY_5V,HIGH);
  print_wakeup_reason();
}

void loop() {
   Serial.println("======================");
  Serial.println(ESP.getFreeHeap());  
    Serial.println("Reading sensors");    
    read_sensors();
    if (car_is_running == true){
      //don't go back to sleep
      doChargingLogic(); 
      loop_counter++;
      if ((loop_counter >= send_on_loop && client.isConnected() == true) || debug == 1){
        Serial.println("Going to send...");
        publish_payload();
        loop_counter = 0;
      }
      client.loop();  
    } else {
      //need to send once then go back to sleep
      if (client.isConnected() == true){
        Serial.println("Going to send...");
        publish_payload();        
        delay(500);
        go_to_sleep();
      } else {
        if (not_connected_timer >= 30){
          go_to_sleep();
        } else {
          Serial.println("Not Connected yet...");
          Serial.println(digitalRead(CAR_ACC));
          not_connected_timer++;
        }
      }
      client.loop();  
    }
    delay(1000);
  
  
}
