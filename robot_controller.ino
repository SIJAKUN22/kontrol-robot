#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <driver/ledc.h>

// Motor driver pins (ESP32 DevKit). Check your board's pinout before wiring.
const int ENA = 32;
const int IN1 = 33;
const int IN2 = 25;
const int ENB = 14;
const int IN3 = 26;
const int IN4 = 27;

const uint32_t PWM_FREQ = 10000;
const int PWM_MAX = 255;
const int PWM_DEFAULT = 150;
const bool REVERSE_MOTOR_A = false;
// The observed forward/reverse/turn behavior indicates the right motor is reversed.
const bool REVERSE_MOTOR_B = true;

// Connect ESP32 to your phone hotspot or home Wi-Fi (2.4 GHz).
const char *WIFI_SSID = "NAMA_HOTSPOT_ATAU_WIFI";
const char *WIFI_PASSWORD = "PASSWORD_WIFI";

// The browser repeats the movement command while a button is held.
// If Wi-Fi disappears, motors stop after this interval.
const uint32_t MOTION_TIMEOUT_MS = 700;

enum Gerakan { BERHENTI, MAJU, MUNDUR, KIRI, KANAN };

Gerakan gerakanSekarang = BERHENTI;
int speedPWM = PWM_DEFAULT;
bool emergencyLatched = false;
uint32_t lastMotionCommandMs = 0;

WebServer server(80);

bool setupMotorPWM() {
  ledc_timer_config_t timer = {};
  timer.speed_mode = LEDC_HIGH_SPEED_MODE;
  timer.duty_resolution = LEDC_TIMER_8_BIT;
  timer.timer_num = LEDC_TIMER_0;
  timer.freq_hz = PWM_FREQ;
  timer.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&timer) != ESP_OK) return false;

  ledc_channel_config_t left = {};
  left.gpio_num = ENA;
  left.speed_mode = LEDC_HIGH_SPEED_MODE;
  left.channel = LEDC_CHANNEL_0;
  left.intr_type = LEDC_INTR_DISABLE;
  left.timer_sel = LEDC_TIMER_0;
  left.duty = 0;
  left.hpoint = 0;
  if (ledc_channel_config(&left) != ESP_OK) return false;

  ledc_channel_config_t right = {};
  right.gpio_num = ENB;
  right.speed_mode = LEDC_HIGH_SPEED_MODE;
  right.channel = LEDC_CHANNEL_1;
  right.intr_type = LEDC_INTR_DISABLE;
  right.timer_sel = LEDC_TIMER_0;
  right.duty = 0;
  right.hpoint = 0;
  return ledc_channel_config(&right) == ESP_OK;
}

void setMotorPWM(ledc_channel_t channel, int duty) {
  duty = constrain(duty, 0, PWM_MAX);
  ledc_set_duty(LEDC_HIGH_SPEED_MODE, channel, duty);
  ledc_update_duty(LEDC_HIGH_SPEED_MODE, channel);
}

void aturMotor(int pin1, int pin2, ledc_channel_t pwmChannel, int pwm) {
  pwm = constrain(pwm, -PWM_MAX, PWM_MAX);

  if (pwm > 0) {
    digitalWrite(pin1, HIGH);
    digitalWrite(pin2, LOW);
  } else if (pwm < 0) {
    digitalWrite(pin1, LOW);
    digitalWrite(pin2, HIGH);
  } else {
    digitalWrite(pin1, LOW);
    digitalWrite(pin2, LOW);
  }

  setMotorPWM(pwmChannel, abs(pwm));
}

void aturRoda(int pwmKiri, int pwmKanan) {
  if (REVERSE_MOTOR_A) pwmKiri = -pwmKiri;
  if (REVERSE_MOTOR_B) pwmKanan = -pwmKanan;

  aturMotor(IN1, IN2, LEDC_CHANNEL_0, pwmKiri);
  aturMotor(IN3, IN4, LEDC_CHANNEL_1, pwmKanan);
}

void terapkanGerakan() {
  switch (gerakanSekarang) {
    case MAJU:
      aturRoda(speedPWM, speedPWM);
      lastMotionCommandMs = millis();
      break;
    case MUNDUR:
      aturRoda(-speedPWM, -speedPWM);
      lastMotionCommandMs = millis();
      break;
    case KIRI:
      aturRoda(-speedPWM, speedPWM);
      lastMotionCommandMs = millis();
      break;
    case KANAN:
      aturRoda(speedPWM, -speedPWM);
      lastMotionCommandMs = millis();
      break;
    case BERHENTI:
    default:
      aturRoda(0, 0);
      break;
  }
}

void gerakMaju() { gerakanSekarang = MAJU; terapkanGerakan(); }
void gerakMundur() { gerakanSekarang = MUNDUR; terapkanGerakan(); }
void belokKiri() { gerakanSekarang = KIRI; terapkanGerakan(); }
void belokKanan() { gerakanSekarang = KANAN; terapkanGerakan(); }
void berhenti() { gerakanSekarang = BERHENTI; terapkanGerakan(); }

void aturSpeedPWM(int nilai, bool reportToSerial = true) {
  speedPWM = constrain(nilai, 0, PWM_MAX);
  terapkanGerakan();
  if (reportToSerial) {
    Serial.print("PWM sekarang: ");
    Serial.println(speedPWM);
  }
}

void tampilkanBantuan() {
  Serial.println();
  Serial.println("Serial: W maju, S mundur, A kiri, D kanan, X berhenti");
  Serial.println("+ / - ubah PWM 10, V180 atur PWM, E! emergency stop, E0 lepas");
  Serial.println("? tampilkan bantuan");
  Serial.println();
}

bool isMotionCommand(const String &command) {
  return command == "W" || command == "S" || command == "A" || command == "D";
}

bool isAllowedCommand(const String &command) {
  if (command == "W" || command == "S" || command == "A" || command == "D" ||
      command == "X" || command == "+" || command == "-" || command == "E!" ||
      command == "E0") {
    return true;
  }
  if (!command.startsWith("V") || command.length() < 2) return false;
  for (size_t i = 1; i < command.length(); i++) {
    if (!isDigit(command[i])) return false;
  }
  return true;
}

void prosesPerintah(String perintah, bool reportToSerial = true) {
  perintah.trim();
  perintah.toUpperCase();

  if (perintah == "E!") {
    emergencyLatched = true;
    berhenti();
    if (reportToSerial) Serial.println("STOP DARURAT AKTIF");
    return;
  }
  if (perintah == "E0") {
    emergencyLatched = false;
    if (reportToSerial) Serial.println("STOP DARURAT DILEPAS");
    return;
  }
  if (emergencyLatched && isMotionCommand(perintah)) {
    if (reportToSerial) Serial.println("Gerakan ditolak: stop darurat masih aktif.");
    return;
  }

  if (perintah == "W") {
    gerakMaju();
    if (reportToSerial) Serial.println("MAJU");
  } else if (perintah == "S") {
    gerakMundur();
    if (reportToSerial) Serial.println("MUNDUR");
  } else if (perintah == "A") {
    belokKiri();
    if (reportToSerial) Serial.println("KIRI");
  } else if (perintah == "D") {
    belokKanan();
    if (reportToSerial) Serial.println("KANAN");
  } else if (perintah == "X") {
    berhenti();
    if (reportToSerial) Serial.println("BERHENTI");
  } else if (perintah == "+") {
    aturSpeedPWM(speedPWM + 10, reportToSerial);
  } else if (perintah == "-") {
    aturSpeedPWM(speedPWM - 10, reportToSerial);
  } else if (perintah.startsWith("V")) {
    String angka = perintah.substring(1);
    if (angka.length() == 0) {
      if (reportToSerial) Serial.println("Format speed: V0 sampai V255");
      return;
    }
    for (size_t i = 0; i < angka.length(); i++) {
      if (!isDigit(angka[i])) {
        if (reportToSerial) Serial.println("Format speed: V0 sampai V255");
        return;
      }
    }
    aturSpeedPWM(angka.toInt(), reportToSerial);
  } else if (perintah == "?") {
    tampilkanBantuan();
  } else {
    if (reportToSerial) Serial.println("Perintah tidak dikenal. Ketik ? untuk bantuan.");
  }
}

void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Access-Control-Allow-Private-Network", "true");
  server.sendHeader("Cache-Control", "no-store");
}

void handleOptions() {
  addCorsHeaders();
  server.send(204);
}

void sendJson(int statusCode, const String &body) {
  addCorsHeaders();
  server.send(statusCode, "application/json", body);
}

const char *motionName() {
  switch (gerakanSekarang) {
    case MAJU: return "FORWARD";
    case MUNDUR: return "BACKWARD";
    case KIRI: return "LEFT";
    case KANAN: return "RIGHT";
    case BERHENTI:
    default: return "STOP";
  }
}

void handleStatus() {
  String body = "{\"ok\":true,\"motion\":\"";
  body += motionName();
  body += "\",\"pwm\":" + String(speedPWM);
  body += ",\"pwmDefault\":" + String(PWM_DEFAULT);
  body += ",\"pwmMax\":" + String(PWM_MAX);
  body += ",\"estop\":" + String(emergencyLatched ? "true" : "false");
  body += ",\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
  body += ",\"wifiRSSI\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  body += ",\"uptimeMs\":" + String(millis()) + "}";
  sendJson(200, body);
}

void handleCommand() {
  if (!server.hasArg("c")) {
    sendJson(400, "{\"ok\":false,\"error\":\"Parameter c diperlukan\"}");
    return;
  }

  String command = server.arg("c");
  command.trim();
  command.toUpperCase();
  if (!isAllowedCommand(command)) {
    sendJson(400, "{\"ok\":false,\"error\":\"Perintah tidak dikenal\"}");
    return;
  }
  const bool wasLatched = emergencyLatched;
  prosesPerintah(command, false);

  if (!wasLatched && emergencyLatched && command != "E!") {
    sendJson(423, "{\"ok\":false,\"error\":\"Stop darurat aktif\"}");
    return;
  }
  if (emergencyLatched && isMotionCommand(command)) {
    sendJson(423, "{\"ok\":false,\"error\":\"Stop darurat aktif\"}");
    return;
  }

  String body = "{\"ok\":true,\"motion\":\"";
  body += motionName();
  body += "\",\"pwm\":" + String(speedPWM);
  body += ",\"pwmDefault\":" + String(PWM_DEFAULT);
  body += ",\"pwmMax\":" + String(PWM_MAX);
  body += ",\"estop\":" + String(emergencyLatched ? "true" : "false") + "}";
  sendJson(200, body);
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    addCorsHeaders();
    server.send(200, "text/plain; charset=utf-8",
      "Robot ESP32 API aktif. Jalankan robot-ui.html dari laptop; lihat CARA_MENJALANKAN.md.");
  });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/status", HTTP_OPTIONS, handleOptions);
  server.on("/api/cmd", HTTP_GET, handleCommand);
  server.on("/api/cmd", HTTP_OPTIONS, handleOptions);
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      handleOptions();
      return;
    }
    sendJson(404, "{\"ok\":false,\"error\":\"Endpoint tidak ditemukan\"}");
  });
  server.begin();
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);

  if (!setupMotorPWM()) {
    Serial.println("Gagal mengatur PWM LEDC. Periksa pin motor dan konfigurasi board.");
    while (true) delay(1000);
  }
  setMotorPWM(LEDC_CHANNEL_0, 0);
  setMotorPWM(LEDC_CHANNEL_1, 0);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Menyambungkan ke Wi-Fi: ");
  Serial.println(WIFI_SSID);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  setupWebServer();
  berhenti();
  Serial.println("Kontrol motor siap.");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi tersambung. Alamat ESP32: http://");
    Serial.println(WiFi.localIP());
    Serial.print("Sinyal Wi-Fi: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("Wi-Fi belum tersambung. Periksa SSID/password; ESP32 akan mencoba lagi.");
  }
  Serial.print("PWM awal: ");
  Serial.println(speedPWM);
  tampilkanBantuan();
}

void loop() {
  server.handleClient();

  static uint32_t lastReconnectAttemptMs = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - lastReconnectAttemptMs >= 10000) {
    lastReconnectAttemptMs = millis();
    Serial.println("Wi-Fi putus; mencoba menyambung lagi...");
    WiFi.reconnect();
  }

  static bool previouslyConnected = false;
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (wifiConnected && !previouslyConnected) {
    Serial.print("Wi-Fi tersambung kembali. Alamat ESP32: http://");
    Serial.println(WiFi.localIP());
  }
  previouslyConnected = wifiConnected;

  if (gerakanSekarang != BERHENTI && millis() - lastMotionCommandMs > MOTION_TIMEOUT_MS) {
    berhenti();
    Serial.println("Berhenti otomatis: perintah gerak timeout.");
  }

  if (Serial.available()) {
    const String perintah = Serial.readStringUntil('\n');
    prosesPerintah(perintah);
  }
}
