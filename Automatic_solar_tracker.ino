#define BLYNK_PRINT Serial

// Widget html 
#define BLYNK_TEMPLATE_ID "TMPL6ynGxhMrs"
#define BLYNK_TEMPLATE_NAME "Automatic Solar Tracker"
#define BLYNK_AUTH_TOKEN "FZZJK26JIlGMjoHF-3xyIf6kzTCOf2ID"

// Widget 
// #define BLYNK_TEMPLATE_ID "TMPL6XxDrxOBS"
// #define BLYNK_TEMPLATE_NAME "Automatic Solar Tracker V2"
// #define BLYNK_AUTH_TOKEN "g3XAdvS8E4RuzIdf_MCYlJI1RALf2TmD"

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <Servo.h>
#include "DHT.h"

// ===== PIN =====
#define DHTPIN D5
#define DHTTYPE DHT22
#define LDR_UP_PIN D2
#define LDR_DOWN_PIN D4
#define SERVO_PIN D1

// ===== WIFI =====
char ssid[] = "OPPO Reno8 T 5G";
char pass[] = "qxua9479";

Servo myServo;
DHT dht(DHTPIN, DHTTYPE);
BlynkTimer timer;

// ===== VARIABLES =====
int currentAngle = 0;
int userAngle = 90;
int mode = 0;            // 0 = AUTO, 1 = MANUAL

int ldr_up = HIGH;
int ldr_down = HIGH;

float lastTemperature = NAN;
const float TEMP_OFFSET = 0.0;
const float TEMP_LIMIT = 30.0;
bool tempAlarmSent = false;

int lastRawUp = -1;
int lastRawDown = -1;
int stableCount = 0;
int stableLdrUp = HIGH;
int stableLdrDown = HIGH;

unsigned long lastServoMove = 0;
const unsigned long SERVO_MOVE_INTERVAL = 60;

// ======================================================
// V5 : MODE (Mode_Selection)  0=AUTO 1=MANUAL
// ======================================================
BLYNK_WRITE(V5)   // Mode_Selection
{
  int newMode = constrain(param.asInt(), 0, 1);

  // ทำงานเฉพาะตอน Mode เปลี่ยนจริง (เพราะมี poll ซ้ำทุกวินาที)
  if (newMode == mode)
    return;

  mode = newMode;

  Serial.print(">>> MODE: ");
  Serial.println(mode == 0 ? "AUTO" : "MANUAL");

  if (mode == 1)
  {
    // เข้า Manual: เริ่มจากตำแหน่งปัจจุบัน (ไม่กระโดด)
    userAngle = currentAngle;
    Blynk.virtualWrite(V3, userAngle);
  }
}

// ดึงค่า Mode จาก Blynk ซ้ำ กันข้อความหลุด
void pollCloud()
{
  if (!Blynk.connected())
    return;

  // ดึงเฉพาะ Mode (V5) เท่านั้น
  // ไม่ดึง V3 ซ้ำ เพราะค่าเก่าใน Cloud จะดึง servo กลับเอง
  Blynk.syncVirtual(V5);
}

// ======================================================
// V3 : MANUAL ANGLE (Servo_Control)
// ======================================================
BLYNK_WRITE(V3)   // Servo_Control
{
  userAngle = constrain(param.asInt(), 0, 180);

  Serial.print("[MANUAL] V3 = ");
  Serial.println(userAngle);

  lastServoMove = 0;
}

BLYNK_CONNECTED()
{
  Serial.println(">>> BLYNK CONNECTED");
  Blynk.syncVirtual(V5);   // Mode
  Blynk.syncVirtual(V3);   // Manual angle
}

// ======================================================
// READ LDR (เรียกเร็วเพื่อความนิ่ง ไม่ส่ง Blynk ที่นี่)
// ======================================================
void readLDR()
{
  int rawUp = digitalRead(LDR_UP_PIN);
  int rawDown = digitalRead(LDR_DOWN_PIN);

  ldr_up = rawUp;
  ldr_down = rawDown;

  if (rawUp == lastRawUp && rawDown == lastRawDown)
  {
    stableCount++;
  }
  else
  {
    stableCount = 0;
    lastRawUp = rawUp;
    lastRawDown = rawDown;
  }

  if (stableCount >= 2)
  {
    stableLdrUp = rawUp;
    stableLdrDown = rawDown;
  }
}

// ======================================================
// CONTROL SERVO
// ======================================================
void controlServo()
{
  if (millis() - lastServoMove < SERVO_MOVE_INTERVAL)
    return;

  int oldAngle = currentAngle;

  if (mode == 0)
  {
    if (stableLdrUp == LOW && stableLdrDown == HIGH)
      currentAngle += 2;
    else if (stableLdrUp == HIGH && stableLdrDown == LOW)
      currentAngle -= 2;
  }
  else
  {
    if (currentAngle < userAngle)
      currentAngle = min(currentAngle + 3, userAngle);
    else if (currentAngle > userAngle)
      currentAngle = max(currentAngle - 3, userAngle);
  }

  currentAngle = constrain(currentAngle, 0, 180);

  if (currentAngle != oldAngle)
  {
    myServo.write(currentAngle);
    lastServoMove = millis();
  }
}

// ======================================================
// READ DHT22 (ทุก 2.5 วินาที)
// ======================================================
void readDHT()
{
  float raw = dht.readTemperature();

  if (isnan(raw))
  {
    Serial.println("[DHT22] Read failed");
    return;
  }

  float t = raw + TEMP_OFFSET;

  if (t < -40.0 || t > 80.0)
  {
    Serial.print("[DHT22] Invalid = ");
    Serial.println(t);
    return;
  }

  lastTemperature = t;

  Serial.print("[DHT22] Temp = ");
  Serial.print(lastTemperature, 1);
  Serial.println(" C");

  if (lastTemperature >= TEMP_LIMIT && !tempAlarmSent)
  {
    Blynk.logEvent("high_temp", String("อุณหภูมิรอบแผงโซลาร์เซลล์สูงเกินกำหนด = ") + String(lastTemperature, 1) + " C");
    tempAlarmSent = true;
  }

  if (lastTemperature < TEMP_LIMIT - 2)
    tempAlarmSent = false;
}

// ======================================================
// ส่งค่าขึ้น Blynk รวมที่เดียว ทุก 1 วินาที
// ======================================================
void sendToBlynk()
{
  if (!Blynk.connected())
    return;

  if (!isnan(lastTemperature))
    Blynk.virtualWrite(V2, lastTemperature);   // Temperature

  Blynk.virtualWrite(V0, ldr_up);              // LDR_Up
  Blynk.virtualWrite(V1, ldr_down);            // LDR_Down
  Blynk.virtualWrite(V4, currentAngle);        // Servo_Position
}

void showStatus()
{
  Serial.print("[STATUS] Mode=");
  Serial.print(mode == 0 ? "AUTO" : "MANUAL");
  Serial.print(" LDR=");
  Serial.print(ldr_up);
  Serial.print(",");
  Serial.print(ldr_down);
  Serial.print(" Servo=");
  Serial.print(currentAngle);
  Serial.print(" Target=");
  Serial.print(userAngle);
  Serial.print(" Temp=");
  Serial.println(lastTemperature, 1);
}

// ======================================================
void setup()
{
  Serial.begin(9600);
  delay(300);

  Serial.println();
  Serial.println("=== AUTOMATIC SOLAR TRACKER ===");

  myServo.attach(SERVO_PIN);
  currentAngle = 0;
  userAngle = 90;
  myServo.write(currentAngle);

  dht.begin();

  pinMode(LDR_UP_PIN, INPUT_PULLUP);
  pinMode(LDR_DOWN_PIN, INPUT_PULLUP);

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  timer.setInterval(50L, controlServo);
  timer.setInterval(200L, readLDR);
  timer.setInterval(2500L, readDHT);
  timer.setInterval(1500L, sendToBlynk);
  timer.setInterval(1000L, pollCloud);
  timer.setInterval(2000L, showStatus);
}

void loop()
{
  Blynk.run();
  timer.run();
}
