#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <WiFi.h>              
#include <HTTPClient.h>       
#include <time.h>
#include <ArduinoJson.h>
#include <lvgl.h>              
#include "ui.h"                

// --- Cấu hình WiFi & API ---
const char* ssid     = "";         // Điền tên WiFi
const char* password = "";         // Điền mật khẩu WiFi
const long timeZone  = 7 * 3600;       
const String apiKey  = "";         // Điền API Key của OpenWeatherMap
const String city    = "Ho Chi Minh";  // Tên thành phố

// --- Cấu hình chân màn hình (Adafruit_ST7735, Software SPI) ---
#define TFT_RST 0 
#define TFT_DC  1
#define TFT_SCLK 3
#define TFT_CS  4
#define TFT_MOSI 2 // sda
#define TFT_LED  10

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// --- Cấu hình Màn hình cho LVGL ---
static const uint16_t screenWidth  = 128; // Kích thước ST7735
static const uint16_t screenHeight = 128;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[screenWidth * 10];

// Hàm đẩy màu từ LVGL ra màn hình (dùng API của Adafruit_ST7735)
void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.writePixels((uint16_t *)&color_p->full, w * h, true, false);
  tft.endWrite();

  lv_disp_flush_ready(disp_drv);
}

// --- Biến thời gian & Cập nhật ---
unsigned long lastWeatherUpdate = 0;
const unsigned long weatherInterval = 300000UL; // 5 phút cập nhật 1 lần
int lastMinute = -1;

// --- Cập nhật Icon Thời Tiết trên giao diện SquareLine ---
void updateWeatherIconUI(String weatherMain, String weatherIcon) {
  // Ẩn tất cả các hình ảnh thời tiết trước
  lv_obj_add_flag(ui_clearsky, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_cloudy, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_rain, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_thunderstorm, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_storm, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_night, LV_OBJ_FLAG_HIDDEN);

  // API trả về icon dạng "xxN" (N = night) hoặc "xxD" (D = day)
  bool isNight = weatherIcon.endsWith("n");
  bool isRain         = (weatherMain == "Rain" || weatherMain == "Drizzle");
  bool isThunderstorm = (weatherMain == "Thunderstorm");
  bool isStorm        = (weatherMain == "Squall" || weatherMain == "Tornado");

  if (isThunderstorm) {
    lv_obj_clear_flag(ui_thunderstorm, LV_OBJ_FLAG_HIDDEN);
  } else if (isStorm) {
    lv_obj_clear_flag(ui_storm, LV_OBJ_FLAG_HIDDEN);
  } else if (isRain) {
    lv_obj_clear_flag(ui_rain, LV_OBJ_FLAG_HIDDEN);
  } else if (isNight) {
    // Trời tối (quang hoặc có mây đều tính) và không mưa bão -> dùng hình night
    lv_obj_clear_flag(ui_night, LV_OBJ_FLAG_HIDDEN);
  } else if (weatherMain == "Clear") {
    lv_obj_clear_flag(ui_clearsky, LV_OBJ_FLAG_HIDDEN);
  } else if (weatherMain == "Clouds") {
    lv_obj_clear_flag(ui_cloudy, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(ui_cloudy, LV_OBJ_FLAG_HIDDEN); // Mặc định nếu không xác định được
  }
}

// Encode khoảng trắng và ký tự đặc biệt trong tên thành phố để URL hợp lệ
String urlEncode(const String &str) {
  String encoded = "";
  char c;
  char code0, code1;
  for (unsigned int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encoded += c;
    } else {
      code0 = (c >> 4) & 0xF;
      code1 = c & 0xF;
      code0 = (code0 < 10) ? ('0' + code0) : ('A' + code0 - 10);
      code1 = (code1 < 10) ? ('0' + code1) : ('A' + code1 - 10);
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}

String removeVietnameseTones(String str) {
  const char* utf8Chars[] = {
    "à","á","ạ","ả","ã","â","ầ","ấ","ậ","ẩ","ẫ","ă","ằ","ắ","ặ","ẳ","ẵ",
    "è","é","ẹ","ẻ","ẽ","ê","ề","ế","ệ","ể","ễ",
    "ì","í","ị","ỉ","ĩ",
    "ò","ó","ọ","ỏ","õ","ô","ồ","ố","ộ","ổ","ỗ","ơ","ờ","ớ","ợ","ở","ỡ",
    "ù","ú","ụ","ủ","ũ","ư","ừ","ứ","ự","ử","ữ",
    "ỳ","ý","ỵ","ỷ","ỹ","đ",
    "À","Á","Ạ","Ả","Ã","Â","Ầ","Ấ","Ậ","Ẩ","Ẫ","Ă","Ằ","Ắ","Ặ","Ẳ","Ẵ",
    "È","É","Ẹ","Ẻ","Ẽ","Ê","Ề","Ế","Ệ","Ể","Ễ",
    "Ì","Í","Ị","Ỉ","Ĩ",
    "Ò","Ó","Ọ","Ỏ","Õ","Ô","Ồ","Ố","Ộ","Ổ","Ỗ","Ơ","Ờ","Ớ","Ợ","Ở","Ỡ",
    "Ù","Ú","Ụ","Ủ","Ũ","Ư","Ừ","Ứ","Ự","Ử","Ữ",
    "Ỳ","Ý","Ỵ","Ỷ","Ỹ","Đ"
  };
  const char asciiChars[] = {
    'a','a','a','a','a','a','a','a','a','a','a','a','a','a','a','a','a',
    'e','e','e','e','e','e','e','e','e','e','e',
    'i','i','i','i','i',
    'o','o','o','o','o','o','o','o','o','o','o','o','o','o','o','o','o',
    'u','u','u','u','u','u','u','u','u','u','u',
    'y','y','y','y','y','d',
    'A','A','A','A','A','A','A','A','A','A','A','A','A','A','A','A','A',
    'E','E','E','E','E','E','E','E','E','E','E',
    'I','I','I','I','I',
    'O','O','O','O','O','O','O','O','O','O','O','O','O','O','O','O','O',
    'U','U','U','U','U','U','U','U','U','U','U',
    'Y','Y','Y','Y','Y','D'
  };
  int n = sizeof(utf8Chars) / sizeof(utf8Chars[0]);
  for (int i = 0; i < n; i++) {
    str.replace(utf8Chars[i], String(asciiChars[i]));
  }
  return str;
}

// --- Gọi API và Gán dữ liệu lên Label LVGL ---
void getWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Weather] Bo qua: WiFi chua ket noi");
    return;
  }

  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + urlEncode(city) +
               ",VN&appid=" + apiKey + "&units=metric&lang=en";

  Serial.println("[Weather] Goi API: " + url);

  http.begin(url);
  int httpCode = http.GET();

  Serial.print("[Weather] HTTP code: ");
  Serial.println(httpCode);

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      float temp  = doc["main"]["temp"] | 0.0;
      int hum     = doc["main"]["humidity"] | 0;
      float wind  = doc["wind"]["speed"] | 0.0;
      String cond = doc["weather"][0]["main"] | "";
      String desc = doc["weather"][0]["description"] | "";
      String icon = doc["weather"][0]["icon"] | "";

      desc = removeVietnameseTones(desc); 
      if (desc.length() > 0) desc[0] = toupper(desc[0]);
      Serial.println("[Weather] Cap nhat OK: " + cond + " / " + desc + " / " + String(temp) + "C");

      // Gán dữ liệu vào các nhãn (Label) bạn đã tạo trong SquareLine
      lv_label_set_text(ui_tempvalue, (String(temp, 0)).c_str());
      lv_label_set_text(ui_humdivalue, (String(hum) + "%").c_str());
      lv_label_set_text(ui_windvalue, (String(wind, 1)).c_str());
      lv_label_set_text(ui_weather, desc.c_str());
      lv_label_set_text(ui_mapvalue, city.c_str());

      // Cập nhật đổi hình ảnh
      updateWeatherIconUI(cond, icon);
    } else {
      Serial.print("[Weather] Loi parse JSON: ");
      Serial.println(error.c_str());
    }
  } else {
    Serial.println("[Weather] Loi goi API! Noi dung tra ve:");
    Serial.println(http.getString());
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(2000); // Đợi Serial Monitor ổn định

  Serial.println("--- BAT DAU SETUP ---");

  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);

  Serial.println("1. Khoi tao TFT...");
  // initR: loại tab của ST7735 128x128 tương ứng ST7735_GREENTAB128 bên TFT_eSPI
  tft.initR(INITR_144GREENTAB);
  tft.setRotation(2);
  tft.fillScreen(ST77XX_BLACK);
  Serial.println("   TFT OK");

  Serial.println("2. Khoi tao LVGL...");
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  Serial.println("3. Khoi tao giao dien SquareLine...");
  ui_init();

  lv_obj_invalidate(lv_scr_act());
  for (int i = 0; i < 20; i++) { lv_timer_handler(); delay(10); }
  delay(4000);

  // Ẩn hết icon thời tiết ngay từ đầu - tránh hiện chồng lên nhau
  // trong lúc chưa có dữ liệu API (mặc định SquareLine không tự ẩn)
  lv_obj_add_flag(ui_clearsky, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_cloudy, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_rain, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_thunderstorm, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_storm, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_night, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_cloudy, LV_OBJ_FLAG_HIDDEN); // hiện tạm 1 icon mặc định trong lúc chờ API

  Serial.println("4. Ket noi WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
      delay(300);
      Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");

  Serial.println("5. Dong bo gio qua NTP...");
  // Khai báo 3 server NTP dự phòng, ESP32 sẽ tự chọn server phản hồi trước
  configTime(timeZone, 0, "pool.ntp.org", "asia.pool.ntp.org", "time.nist.gov");

  struct tm timeinfo;
  int retry = 0;
  const int maxRetry = 15;
  while (!getLocalTime(&timeinfo, 1000) && retry < maxRetry) { // chờ tối đa 1s mỗi lần thử
    Serial.print(".");
    retry++;
  }
  if (retry < maxRetry) {
    Serial.println("\n[NTP] Dong bo THANH CONG");
  } else {
    Serial.println("\n[NTP] Dong bo THAT BAI sau nhieu lan thu - se thu lai trong loop()");
  }

  getWeather();
  lastWeatherUpdate = millis();

  Serial.println("--- SETUP HOAN TAT ---");
}

void loop() {
  // Chạy engine của LVGL
  lv_timer_handler();
  delay(5);

  // Cập nhật thời gian 
  struct tm ti;
  bool timeOk = getLocalTime(&ti, 5);

  static unsigned long lastDebugPrint = 0;
  if (millis() - lastDebugPrint > 10000) { // in debug mỗi 10 giây để không spam Serial
    lastDebugPrint = millis();
    Serial.print("[Time-debug] timeOk=");
    Serial.print(timeOk ? "true" : "false");
    if (timeOk) {
      Serial.print("  -> ");
      Serial.print(ti.tm_hour);
      Serial.print(":");
      Serial.print(ti.tm_min);
    }
    Serial.print("  lastMinute=");
    Serial.println(lastMinute);
  }

  if (timeOk && ti.tm_min != lastMinute) {
    lastMinute = ti.tm_min;
    Serial.println("[Time] -> Dang cap nhat nhan gio/ngay/thu...");

    // Gán Giờ/Phút
    char timeBuffer[6];
    sprintf(timeBuffer, "%02d:%02d", ti.tm_hour, ti.tm_min);
    lv_label_set_text(ui_timevalue1, timeBuffer);

    // ui_dayvalue: "ngày + tháng viết tắt" (mặc định trong UI là "18 JUN")
    char dateBuffer[12];
    strftime(dateBuffer, sizeof(dateBuffer), "%d %b", &ti); // vd: "04 Jul"
    for (int i = 0; dateBuffer[i]; i++) dateBuffer[i] = toupper(dateBuffer[i]);
    lv_label_set_text(ui_dayvalue, dateBuffer);

    // ui_monthvalue: thực chất là tên Thứ trong tuần (mặc định trong UI là "Thu")
    char weekdayBuffer[12];
    strftime(weekdayBuffer, sizeof(weekdayBuffer), "%a", &ti); // vd: "Sat"
    lv_label_set_text(ui_monthvalue, weekdayBuffer);

    Serial.println("[Time] Gia tri da gan: time=" + String(timeBuffer) +
                    " day=" + String(dateBuffer) + " weekday=" + String(weekdayBuffer));

    // Ép LVGL vẽ lại toàn màn hình NGAY LẬP TỨC (đồng bộ, không chờ chu kỳ sau)
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(lv_disp_get_default());
  }

  // Cập nhật thời tiết mỗi 5 phút
  if (millis() - lastWeatherUpdate >= weatherInterval) {
    lastWeatherUpdate = millis();
    getWeather();
  }
}
