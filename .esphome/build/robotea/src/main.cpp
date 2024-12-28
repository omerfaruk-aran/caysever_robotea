// Auto generated code by esphome
// ========== AUTO GENERATED INCLUDE BLOCK BEGIN ===========
#include "esphome.h"
using namespace esphome;
using std::isnan;
using std::min;
using std::max;
using namespace sensor;
using namespace switch_;
using namespace select;
using namespace text_sensor;
logger::Logger *logger_logger_id;
web_server_base::WebServerBase *web_server_base_webserverbase_id;
captive_portal::CaptivePortal *captive_portal_captiveportal_id;
wifi::WiFiComponent *wifi_wificomponent_id;
mdns::MDNSComponent *mdns_mdnscomponent_id;
esphome::ESPHomeOTAComponent *esphome_esphomeotacomponent_id;
safe_mode::SafeModeComponent *safe_mode_safemodecomponent_id;
web_server::WebServer *web_server_webserver_id;
const uint8_t ESPHOME_WEBSERVER_INDEX_HTML[174] PROGMEM = {60, 33, 68, 79, 67, 84, 89, 80, 69, 32, 104, 116, 109, 108, 62, 60, 104, 116, 109, 108, 62, 60, 104, 101, 97, 100, 62, 60, 109, 101, 116, 97, 32, 99, 104, 97, 114, 115, 101, 116, 61, 85, 84, 70, 45, 56, 62, 60, 108, 105, 110, 107, 32, 114, 101, 108, 61, 105, 99, 111, 110, 32, 104, 114, 101, 102, 61, 100, 97, 116, 97, 58, 62, 60, 47, 104, 101, 97, 100, 62, 60, 98, 111, 100, 121, 62, 60, 101, 115, 112, 45, 97, 112, 112, 62, 60, 47, 101, 115, 112, 45, 97, 112, 112, 62, 60, 115, 99, 114, 105, 112, 116, 32, 115, 114, 99, 61, 34, 104, 116, 116, 112, 115, 58, 47, 47, 111, 105, 46, 101, 115, 112, 104, 111, 109, 101, 46, 105, 111, 47, 118, 50, 47, 119, 119, 119, 46, 106, 115, 34, 62, 60, 47, 115, 99, 114, 105, 112, 116, 62, 60, 47, 98, 111, 100, 121, 62, 60, 47, 104, 116, 109, 108, 62};
const size_t ESPHOME_WEBSERVER_INDEX_HTML_SIZE = 174;
api::APIServer *api_apiserver_id;
using namespace api;
using namespace json;
preferences::IntervalSyncer *preferences_intervalsyncer_id;
caysever_robotea::CayseverRobotea *caysever;
adc::ADCSensor *adc_sensor;
esp32::ESP32InternalGPIOPin *esp32_esp32internalgpiopin_id;
resistance::ResistanceSensor *resistance_sensor;
ntc::NTC *ntc_temperature;
caysever_robotea::CayseverRoboteaSelect *caysever_robotea_cayseverroboteaselect_id;
caysever_robotea::CayseverRoboteaSwitch *caysever_robotea_cayseverroboteaswitch_id_2;
caysever_robotea::CayseverRoboteaSwitch *caysever_robotea_cayseverroboteaswitch_id;
text_sensor::TextSensor *text_sensor_textsensor_id;
#define yield() esphome::yield()
#define millis() esphome::millis()
#define micros() esphome::micros()
#define delay(x) esphome::delay(x)
#define delayMicroseconds(x) esphome::delayMicroseconds(x)
// ========== AUTO GENERATED INCLUDE BLOCK END ==========="

void setup() {
  // ========== AUTO GENERATED CODE BEGIN ===========
  // async_tcp:
  //   {}
  // esphome:
  //   name: robotea
  //   friendly_name: RoboTea
  //   build_path: build\robotea
  //   area: ''
  //   platformio_options: {}
  //   includes: []
  //   libraries: []
  //   name_add_mac_suffix: false
  //   min_version: 2024.10.2
  App.pre_setup("robotea", "RoboTea", "", "", __DATE__ ", " __TIME__, false);
  // sensor:
  // switch:
  // select:
  // text_sensor:
  // logger:
  //   level: INFO
  //   logs:
  //     component: INFO
  //     caysever_robotea: INFO
  //   id: logger_logger_id
  //   baud_rate: 115200
  //   tx_buffer_size: 512
  //   deassert_rts_dtr: false
  //   hardware_uart: UART0
  logger_logger_id = new logger::Logger(115200, 512);
  logger_logger_id->set_uart_selection(logger::UART_SELECTION_UART0);
  logger_logger_id->pre_setup();
  logger_logger_id->set_log_level("component", ESPHOME_LOG_LEVEL_INFO);
  logger_logger_id->set_log_level("caysever_robotea", ESPHOME_LOG_LEVEL_INFO);
  logger_logger_id->set_component_source("logger");
  App.register_component(logger_logger_id);
  // web_server_base:
  //   id: web_server_base_webserverbase_id
  web_server_base_webserverbase_id = new web_server_base::WebServerBase();
  web_server_base_webserverbase_id->set_component_source("web_server_base");
  App.register_component(web_server_base_webserverbase_id);
  // captive_portal:
  //   id: captive_portal_captiveportal_id
  //   web_server_base_id: web_server_base_webserverbase_id
  captive_portal_captiveportal_id = new captive_portal::CaptivePortal(web_server_base_webserverbase_id);
  captive_portal_captiveportal_id->set_component_source("captive_portal");
  App.register_component(captive_portal_captiveportal_id);
  // wifi:
  //   use_address: 10.0.0.205
  //   networks:
  //   - ssid: ARANSMARTHOME_IoT
  //     password: !secret 'IoT_wifi_password'
  //     id: wifi_wifiap_id
  //     priority: 0.0
  //   ap:
  //     ssid: RoboTea
  //     password: !secret 'ap_password'
  //     id: wifi_wifiap_id_2
  //     ap_timeout: 1min
  //   id: wifi_wificomponent_id
  //   domain: .local
  //   reboot_timeout: 15min
  //   power_save_mode: LIGHT
  //   fast_connect: false
  //   passive_scan: false
  //   enable_on_boot: true
  wifi_wificomponent_id = new wifi::WiFiComponent();
  wifi_wificomponent_id->set_use_address("10.0.0.205");
  {
  wifi::WiFiAP wifi_wifiap_id = wifi::WiFiAP();
  wifi_wifiap_id.set_ssid("ARANSMARTHOME_IoT");
  wifi_wifiap_id.set_password("suHjy7-sypbuq-bicruv");
  wifi_wifiap_id.set_priority(0.0f);
  wifi_wificomponent_id->add_sta(wifi_wifiap_id);
  }
  {
  wifi::WiFiAP wifi_wifiap_id_2 = wifi::WiFiAP();
  wifi_wifiap_id_2.set_ssid("RoboTea");
  wifi_wifiap_id_2.set_password("25278146q-+");
  wifi_wificomponent_id->set_ap(wifi_wifiap_id_2);
  }
  wifi_wificomponent_id->set_ap_timeout(60000);
  wifi_wificomponent_id->set_reboot_timeout(900000);
  wifi_wificomponent_id->set_power_save_mode(wifi::WIFI_POWER_SAVE_LIGHT);
  wifi_wificomponent_id->set_fast_connect(false);
  wifi_wificomponent_id->set_passive_scan(false);
  wifi_wificomponent_id->set_enable_on_boot(true);
  wifi_wificomponent_id->set_component_source("wifi");
  App.register_component(wifi_wificomponent_id);
  // mdns:
  //   id: mdns_mdnscomponent_id
  //   disabled: false
  //   services: []
  mdns_mdnscomponent_id = new mdns::MDNSComponent();
  mdns_mdnscomponent_id->set_component_source("mdns");
  App.register_component(mdns_mdnscomponent_id);
  // ota:
  // ota.esphome:
  //   platform: esphome
  //   password: !secret 'ota_password'
  //   id: esphome_esphomeotacomponent_id
  //   version: 2
  //   port: 3232
  esphome_esphomeotacomponent_id = new esphome::ESPHomeOTAComponent();
  esphome_esphomeotacomponent_id->set_port(3232);
  esphome_esphomeotacomponent_id->set_auth_password("d5fab7240219a4afd69a9ffb9d80d694");
  esphome_esphomeotacomponent_id->set_component_source("esphome.ota");
  App.register_component(esphome_esphomeotacomponent_id);
  // safe_mode:
  //   id: safe_mode_safemodecomponent_id
  //   boot_is_good_after: 1min
  //   disabled: false
  //   num_attempts: 10
  //   reboot_timeout: 5min
  safe_mode_safemodecomponent_id = new safe_mode::SafeModeComponent();
  safe_mode_safemodecomponent_id->set_component_source("safe_mode");
  App.register_component(safe_mode_safemodecomponent_id);
  if (safe_mode_safemodecomponent_id->should_enter_safe_mode(10, 300000, 60000)) return;
  // web_server:
  //   port: 80
  //   id: web_server_webserver_id
  //   version: 2
  //   enable_private_network_access: true
  //   web_server_base_id: web_server_base_webserverbase_id
  //   include_internal: false
  //   ota: true
  //   log: true
  //   css_url: ''
  //   js_url: https:oi.esphome.io/v2/www.js
  web_server_webserver_id = new web_server::WebServer(web_server_base_webserverbase_id);
  web_server_webserver_id->set_component_source("web_server");
  App.register_component(web_server_webserver_id);
  web_server_base_webserverbase_id->set_port(80);
  web_server_webserver_id->set_allow_ota(true);
  web_server_webserver_id->set_expose_log(true);
  web_server_webserver_id->set_include_internal(false);
  // api:
  //   encryption:
  //     key: !secret 'api_encryption'
  //   id: api_apiserver_id
  //   port: 6053
  //   password: ''
  //   reboot_timeout: 15min
  api_apiserver_id = new api::APIServer();
  api_apiserver_id->set_component_source("api");
  App.register_component(api_apiserver_id);
  api_apiserver_id->set_port(6053);
  api_apiserver_id->set_password("");
  api_apiserver_id->set_reboot_timeout(900000);
  api_apiserver_id->set_noise_psk({126, 141, 23, 160, 189, 26, 139, 213, 55, 70, 145, 254, 253, 152, 115, 195, 15, 214, 44, 186, 20, 134, 174, 98, 184, 168, 14, 141, 183, 95, 100, 146});
  // json:
  //   {}
  // esp32:
  //   board: esp32doit-devkit-v1
  //   framework:
  //     version: 2.0.5
  //     advanced:
  //       ignore_efuse_custom_mac: false
  //     source: ~3.20005.0
  //     platform_version: platformio/espressif32@5.4.0
  //     type: arduino
  //   flash_size: 4MB
  //   variant: ESP32
  // preferences:
  //   id: preferences_intervalsyncer_id
  //   flash_write_interval: 60s
  preferences_intervalsyncer_id = new preferences::IntervalSyncer();
  preferences_intervalsyncer_id->set_write_interval(60000);
  preferences_intervalsyncer_id->set_component_source("preferences");
  App.register_component(preferences_intervalsyncer_id);
  // external_components:
  //   - source:
  //       path: C:/Users/H1/ESPHOME/esphome_caysever_robotea/components
  //       type: local
  //     components:
  //     - caysever_robotea
  //     refresh: 0s
  // caysever_robotea:
  //   id: caysever
  //   sensor: ntc_temperature
  //   active_mode_sensor:
  //     name: Aktif Mod
  //     disabled_by_default: false
  //     id: text_sensor_textsensor_id
  //   mama_suyu_switch:
  //     name: Mama Suyu
  //     disabled_by_default: false
  //     restore_mode: ALWAYS_OFF
  //     id: caysever_robotea_cayseverroboteaswitch_id
  //   su_kaynatma_switch:
  //     name: Su Kaynatma
  //     disabled_by_default: false
  //     restore_mode: ALWAYS_OFF
  //     id: caysever_robotea_cayseverroboteaswitch_id_2
  //   cay_demleme:
  //     name: Çay Demleme
  //     disabled_by_default: false
  //     id: caysever_robotea_cayseverroboteaselect_id
  caysever = new caysever_robotea::CayseverRobotea();
  // sensor.adc:
  //   platform: adc
  //   id: adc_sensor
  //   pin:
  //     number: 35
  //     mode:
  //       input: true
  //       output: false
  //       open_drain: false
  //       pullup: false
  //       pulldown: false
  //     id: esp32_esp32internalgpiopin_id
  //     inverted: false
  //     ignore_pin_validation_error: false
  //     ignore_strapping_warning: false
  //     drive_strength: 20.0
  //   attenuation: 12db
  //   update_interval: 2s
  //   disabled_by_default: false
  //   force_update: false
  //   unit_of_measurement: V
  //   accuracy_decimals: 2
  //   device_class: voltage
  //   state_class: measurement
  //   raw: false
  //   samples: 1
  //   name: adc_sensor
  //   internal: true
  adc_sensor = new adc::ADCSensor();
  adc_sensor->set_update_interval(2000);
  adc_sensor->set_component_source("adc.sensor");
  App.register_component(adc_sensor);
  App.register_sensor(adc_sensor);
  adc_sensor->set_name("adc_sensor");
  adc_sensor->set_object_id("adc_sensor");
  adc_sensor->set_disabled_by_default(false);
  adc_sensor->set_internal(true);
  adc_sensor->set_device_class("voltage");
  adc_sensor->set_state_class(sensor::STATE_CLASS_MEASUREMENT);
  adc_sensor->set_unit_of_measurement("V");
  adc_sensor->set_accuracy_decimals(2);
  adc_sensor->set_force_update(false);
  esp32_esp32internalgpiopin_id = new esp32::ESP32InternalGPIOPin();
  esp32_esp32internalgpiopin_id->set_pin(::GPIO_NUM_35);
  esp32_esp32internalgpiopin_id->set_inverted(false);
  esp32_esp32internalgpiopin_id->set_drive_strength(::GPIO_DRIVE_CAP_2);
  esp32_esp32internalgpiopin_id->set_flags(gpio::Flags::FLAG_INPUT);
  adc_sensor->set_pin(esp32_esp32internalgpiopin_id);
  adc_sensor->set_output_raw(false);
  adc_sensor->set_sample_count(1);
  adc_sensor->set_attenuation(adc::ADC_ATTEN_DB_12_COMPAT);
  adc_sensor->set_channel1(::ADC1_CHANNEL_7);
  // sensor.resistance:
  //   platform: resistance
  //   sensor: adc_sensor
  //   configuration: DOWNSTREAM
  //   resistor: 10000.0
  //   id: resistance_sensor
  //   disabled_by_default: false
  //   force_update: false
  //   unit_of_measurement: Ω
  //   icon: mdi:flash
  //   accuracy_decimals: 1
  //   state_class: measurement
  //   reference_voltage: 3.3
  //   name: resistance_sensor
  //   internal: true
  resistance_sensor = new resistance::ResistanceSensor();
  App.register_sensor(resistance_sensor);
  resistance_sensor->set_name("resistance_sensor");
  resistance_sensor->set_object_id("resistance_sensor");
  resistance_sensor->set_disabled_by_default(false);
  resistance_sensor->set_internal(true);
  resistance_sensor->set_icon("mdi:flash");
  resistance_sensor->set_state_class(sensor::STATE_CLASS_MEASUREMENT);
  resistance_sensor->set_unit_of_measurement("\316\251");
  resistance_sensor->set_accuracy_decimals(1);
  resistance_sensor->set_force_update(false);
  resistance_sensor->set_component_source("resistance.sensor");
  App.register_component(resistance_sensor);
  resistance_sensor->set_sensor(adc_sensor);
  resistance_sensor->set_configuration(resistance::DOWNSTREAM);
  resistance_sensor->set_resistor(10000.0f);
  resistance_sensor->set_reference_voltage(3.3f);
  // sensor.ntc:
  //   platform: ntc
  //   sensor: resistance_sensor
  //   name: NTC Sıcaklık
  //   calibration:
  //     a: 0.0007124681299720999
  //     b: 0.00025316455696202533
  //     c: 0
  //   id: ntc_temperature
  //   disabled_by_default: false
  //   force_update: false
  //   unit_of_measurement: °C
  //   accuracy_decimals: 1
  //   device_class: temperature
  //   state_class: measurement
  ntc_temperature = new ntc::NTC();
  App.register_sensor(ntc_temperature);
  ntc_temperature->set_name("NTC S\304\261cakl\304\261k");
  ntc_temperature->set_object_id("ntc_s_cakl_k");
  ntc_temperature->set_disabled_by_default(false);
  ntc_temperature->set_device_class("temperature");
  ntc_temperature->set_state_class(sensor::STATE_CLASS_MEASUREMENT);
  ntc_temperature->set_unit_of_measurement("\302\260C");
  ntc_temperature->set_accuracy_decimals(1);
  ntc_temperature->set_force_update(false);
  ntc_temperature->set_component_source("ntc.sensor");
  App.register_component(ntc_temperature);
  ntc_temperature->set_sensor(resistance_sensor);
  ntc_temperature->set_a(0.0007124681299720999f);
  ntc_temperature->set_b(0.00025316455696202533f);
  ntc_temperature->set_c(0);
  // socket:
  //   implementation: bsd_sockets
  // md5:
  // network:
  //   enable_ipv6: false
  //   min_ipv6_addr_count: 0
  caysever->set_ntc_sensor(ntc_temperature);
  caysever_robotea_cayseverroboteaselect_id = new caysever_robotea::CayseverRoboteaSelect();
  App.register_select(caysever_robotea_cayseverroboteaselect_id);
  caysever_robotea_cayseverroboteaselect_id->set_name("\303\207ay Demleme");
  caysever_robotea_cayseverroboteaselect_id->set_object_id("_ay_demleme");
  caysever_robotea_cayseverroboteaselect_id->set_disabled_by_default(false);
  caysever_robotea_cayseverroboteaselect_id->traits.set_options({"1/4", "2/4", "3/4", "MAX", "OFF"});
  caysever_robotea_cayseverroboteaselect_id->set_component_source("caysever_robotea");
  App.register_component(caysever_robotea_cayseverroboteaselect_id);
  caysever->set_cay_demleme_select(caysever_robotea_cayseverroboteaselect_id);
  caysever_robotea_cayseverroboteaswitch_id_2 = new caysever_robotea::CayseverRoboteaSwitch();
  caysever_robotea_cayseverroboteaswitch_id_2->set_component_source("caysever_robotea");
  App.register_component(caysever_robotea_cayseverroboteaswitch_id_2);
  App.register_switch(caysever_robotea_cayseverroboteaswitch_id_2);
  caysever_robotea_cayseverroboteaswitch_id_2->set_name("Su Kaynatma");
  caysever_robotea_cayseverroboteaswitch_id_2->set_object_id("su_kaynatma");
  caysever_robotea_cayseverroboteaswitch_id_2->set_disabled_by_default(false);
  caysever_robotea_cayseverroboteaswitch_id_2->set_restore_mode(switch_::SWITCH_ALWAYS_OFF);
  caysever->set_su_kaynatma_switch(caysever_robotea_cayseverroboteaswitch_id_2);
  caysever_robotea_cayseverroboteaswitch_id = new caysever_robotea::CayseverRoboteaSwitch();
  caysever_robotea_cayseverroboteaswitch_id->set_component_source("caysever_robotea");
  App.register_component(caysever_robotea_cayseverroboteaswitch_id);
  App.register_switch(caysever_robotea_cayseverroboteaswitch_id);
  caysever_robotea_cayseverroboteaswitch_id->set_name("Mama Suyu");
  caysever_robotea_cayseverroboteaswitch_id->set_object_id("mama_suyu");
  caysever_robotea_cayseverroboteaswitch_id->set_disabled_by_default(false);
  caysever_robotea_cayseverroboteaswitch_id->set_restore_mode(switch_::SWITCH_ALWAYS_OFF);
  caysever->set_mama_suyu_switch(caysever_robotea_cayseverroboteaswitch_id);
  text_sensor_textsensor_id = new text_sensor::TextSensor();
  App.register_text_sensor(text_sensor_textsensor_id);
  text_sensor_textsensor_id->set_name("Aktif Mod");
  text_sensor_textsensor_id->set_object_id("aktif_mod");
  text_sensor_textsensor_id->set_disabled_by_default(false);
  caysever->set_mode_sensor(text_sensor_textsensor_id);
  caysever->set_component_source("caysever_robotea");
  App.register_component(caysever);
  // =========== AUTO GENERATED CODE END ============
  App.setup();
}

void loop() {
  App.loop();
}
