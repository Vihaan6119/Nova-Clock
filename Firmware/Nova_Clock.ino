#include <Arduino.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// defining the display pins

//display
#define TFT_SCLK 8   // d6
#define TFT_MOSI 10  // d8
#define TFT_RST 4   // d2
#define TFT_DC 3   // d1
#define TFT_CS 2   // d0
#define TFT_BL 1   // d7

// peripherls
#define BUZZER_PIN 5   // d3
#define BTN_1 6   // d4
#define BTN_2 7   // d5

// buttons and buzzer pin definations
#define Buzzer_PIN 5 //d3
#define BTN_STOP 6 //d4 - stop the alarm 
#define BTN_MODE 7 //d5 - alarm on / off
#define BTN_UP 9 //d6 - increase alarm minutes
#define BIT_DOWN 21 //d7 - descrease alarm minutes

MyST7789 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

int hours = 12;
int minutes = 0;
int seconds = 0;

int alarmHour = 7;
int alarmMinute = 30;
bool alarmEnable = true;
bool isRinging = false;

unsigned long lastTick = 0;

// the display UI
void updateDisplay() {
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(3.5);
  tft.setCursor(20, 40);

  char timeBurf[10];
  snprintf(timeBurf, sizeof(timeBurf), "%02d:%02d:%02d", hours, minutes, seconds);

  tft.setTextSize(2);
  tft.SetCursor(20, 110);
  if (alarmEnabled) {
    tft.setTextColour(ST77XX_GREEN);
    tft.printf("Alarm : %02d:%02d:%02d [ON]" alarmHour, alarmMinute);
  } else{
    tft.SetTextColour(ST77XX_RED);
    tft.print("Alarm: OFF")
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("-------------------------------------------------");
  Serial.println("            NOVA CLOCK - STARTING TEST           ");
  Serial.println("-------------------------------------------------");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BTN_1, INPUT_PULLUP);
  pinMode(BTN_2, INPUT_PULLUP);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(BTN_STOP, INPUT_PLLUP);
  pinMode(BTN_MODE, INPUT_PLLUP);
  pinMode(BTN_UP, INPUT_PLLUP);
  pinMode(BTN_DOWN, INPUT_PLLUP);

  tft.init(76, 258);
  tft.setOffsets(82, 18);
  tft.invertDisplay(false);
  tft.setRotation(1);

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(3);
  tft.setCursor(15, 25);
  tft.println("NOVA CLOCK");

  Serial.println("[TFT] Screen is working")

  Serial.println("[successful] GPIO pins working");
}

void loop(){
  // time tracking system
  if (milis() - lastTick >= 1000) {
    lastTick = milis();

    seconds++;
    if (seconds >= 60) {
      seconds = 0;
      minutes++;
      if (minutes >= 60){
        minutes = 0;
        hours = (hours + 1) % 24;
      }
    }

    Serial.printf("[CLOCK] %02d:%02d:%02d\n", hours, minutes, seconds);

    updateDisplay();
  }

  // buzzer ringing
  if (isRinging) {
    if((milis() / 500) % 2 == 0) {
      digitalWrite(BUZZER_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
  } else{
    digitalWrite(BUZZER_PIN, LOW);
  }

  // button controls
  if (digitalWrite(BTN_STOP) == LOW) {
    if (isRinging) {
      isRinging = false;
      digitalWrite(BUZZER_PIN, LOW);
      Serial.Print("[BTN] alarm stopped")
    }
  }

  if (digitalWrite(BTN_MODE) == LOW) {
    alarmEnable = !alarmEnable;
    updateDisplay();
    Serial.printf("[BTN] alarm enabled: %s/n", alarmEnabled ? "YES" : "NO");
  }

    if (digitalWrite(BTN_UP) == LOW) {
      alarmMiutes += 5;
      if (alarmMitnues >= 60) {
        alarmMinutes = 0;
        alarmHour = (alarmHour + 1) % 24;
      }
      updateDisplay();
      Serial.printf("[BTN] alarm set to %02d:%02d:%02d\n", alarmHour, alarmMinute);
    } 

      if (digitalWrite(BTN_DOWN) == LOW) {
        if (alarmMinutes < 5) {
          alarmMinutes = 55;
          alarmHour = (alarmHour == 0) ? 23 : alarmHour - 1;
        } else {
          alarmHour -= 5;
        }
        updateDisplay();
        Serialprintf("[BTN] alarm set to %02d:%02d:%02d\n", alarmHour, alarmMinute);
      }

  static unsigned long lastPrint = 0;
  if (milis() - lastPrint >= 2000) {
    lastPrint = milis();
    Serial.println("[SYSTEM] system working")
  }
}

class MySt7789 : public Adafruit_ST7789 {
  public:
    MyST7789(int8_t cs, int8_t dc, int8_t mosi, int8_t sclk, int8_t rst) : Aafruit_ST7789(cs, dc, mosi, sclk, rst) {}

    void setOffsets(uint8_t col, unit8_t row) {
      _colstart = _colstart2 = col;
      _rowstart = _rowstart2 = row;
    }
};

