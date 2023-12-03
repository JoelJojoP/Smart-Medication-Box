// Teensy 4.1 code for Smart Medication Box

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <TimeLib.h>
#include <SD.h>
#include <SPI.h>

#define TIME_HEADER "T"

// Pins for various components
#define enc_clk 24
#define enc_dt 25
#define menu_button 32
#define select_button 30
#define buzzer_pin 39
int Rack_LED[4] = {33, 34, 35, 36};

// SD Card chip select - BUILTIN_SDCARD to use the built-in SD card slot of Teensy 4.1
const int chipSelect = BUILTIN_SDCARD;

// File object for SD card
File myFile;

// LCD object - 0x27 is the default I2C address of the I2C LCD module
LiquidCrystal_I2C lcd(0x27, 16, 2); // set the LCD address to 0x27 for a 16 chars and 2 line display

// Variables for various state controls
volatile int state = 0;
volatile int substate = 0;
volatile int rack = 0;
volatile int time_slot = 0;
volatile int reset_opt = 0;
volatile int edit_mode = 0;
int alert[4] = {0, 0, 0, 0};

String isEvening = "AM";
int h, m, prev_h, prev_m;
String r_buff;
int write_change = 0;

// Arrays to store the time for each rack
int slot_hour[4][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
int slot_minute[4][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
String slot_script[4][4] = {{"AM", "AM", "AM", "AM"}, {"AM", "AM", "AM", "AM"}, {"AM", "AM", "AM", "AM"}, {"AM", "AM", "AM", "AM"}};

// Functions to get time from Teensy's inbuilt RTC
time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}

unsigned long processSyncMessage()
{
  unsigned long pctime = 0L;
  const unsigned long DEFAULT_TIME = 1357041600;

  if (Serial.find(TIME_HEADER))
  {
    pctime = Serial.parseInt();
    return pctime;
    if (pctime < DEFAULT_TIME)
    {
      pctime = 0L;
    }
  }
  return pctime;
}

// Function to print digits on LCD
void printDigits(int digits)
{
  if (digits < 10)
    lcd.print("0");
  lcd.print(digits);
}

// State control functions
void change_option()
{
  if (digitalRead(enc_dt) == 0)
  {
    if (state == 1 && substate == 0)
    {
      rack--;
      if (rack <= -1)
        rack = 3;
    }
    else if (substate == 1)
    {
      time_slot--;
      if (time_slot <= -1)
        time_slot = 3;
    }
    else if (substate == 2)
    {
      if (edit_mode == 0)
      {
        slot_hour[rack][time_slot]--;
        if (slot_hour[rack][time_slot] <= -1)
          slot_hour[rack][time_slot] = 12;
      }
      else if (edit_mode == 1)
      {
        slot_minute[rack][time_slot]--;
        if (slot_minute[rack][time_slot] <= -1)
          slot_minute[rack][time_slot] = 59;
      }
      else if (edit_mode == 2)
      {
        slot_script[rack][time_slot] = (slot_script[rack][time_slot] == "AM") ? "PM" : "AM";
      }
    }
  }
  else
  {
    if (state == 1 && substate == 0)
    {
      rack++;
      if (rack >= 4)
        rack = 0;
    }
    else if (substate == 1)
    {
      time_slot++;
      if (time_slot >= 4)
        time_slot = 0;
    }
    else if (substate == 2)
    {
      if (edit_mode == 0)
      {
        slot_hour[rack][time_slot]++;
        if (slot_hour[rack][time_slot] >= 13)
          slot_hour[rack][time_slot] = 0;
      }
      else if (edit_mode == 1)
      {
        slot_minute[rack][time_slot]++;
        if (slot_minute[rack][time_slot] >= 60)
          slot_minute[rack][time_slot] = 0;
      }
      else if (edit_mode == 2)
      {
        slot_script[rack][time_slot] = (slot_script[rack][time_slot] == "AM") ? "PM" : "AM";
      }
    }
  }
}

void change_state()
{
  if (digitalRead(menu_button) == 0)
  {
    int f = 0;
    for (int i = 0; i < 4; i++)
    {
      if (alert[i] != 0)
      {
        alert[i] = 0;
        f = 1;
      }
    }
    if (f == 0)
    {
      substate = 0;
      state++;
      if (state == 2)
        state = 0;
    }
  }
}

void select_opt()
{
  if (digitalRead(select_button) == 0 && state == 1 && substate == 0)
  {
    substate = 1;
  }
  else if (digitalRead(select_button) == 0 && substate == 1)
  {
    substate = 2;
  }
  else if (digitalRead(select_button) == 0 && substate == 2)
  {
    edit_mode++;
    if (edit_mode == 3)
    {
      substate = 1;
      edit_mode = 0;
      write_change = 1;
    }
  }
}

// Function to sound the alarm
void alarm()
{
  for (int i = 0; i < 4; i++)
  {
    if (alert[i] >= 1 && alert[i] <= 200)
    {
      digitalWrite(Rack_LED[i], alert[i] % 2);
      digitalWrite(buzzer_pin, alert[i] % 2);
      alert[i]++;
    }
    else
    {
      alert[i] = 0;
      digitalWrite(Rack_LED[i], LOW);
      digitalWrite(buzzer_pin, LOW);
    }
  }
}

void setup()
{
  Serial.begin(9600);
  pinMode(menu_button, INPUT_PULLUP);
  pinMode(select_button, INPUT_PULLUP);
  pinMode(enc_clk, INPUT);
  pinMode(enc_dt, INPUT_PULLUP);
  pinMode(buzzer_pin, OUTPUT);
  for (int i = 0; i < 4; i++)
  {
    pinMode(Rack_LED[i], OUTPUT);
    digitalWrite(Rack_LED[i], LOW);
  }
  while (!SD.begin(chipSelect))
  {
    digitalWrite(LED_BUILTIN, HIGH);
  }
  setSyncProvider(getTeensy3Time);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(2, 0);
  lcd.print("Smart Med Box");
  myFile = SD.open("setting.txt");
  int i = 0, j = 0;

  // Reading the time slots from the SD card
  while (myFile.available())
  {
    char c = myFile.read();
    if (c == ',')
    {
      if (r_buff != "")
      {
        if (i < 4)
          slot_hour[i][j] = r_buff.toInt();
        else if (i < 8)
          slot_minute[i - 4][j] = r_buff.toInt();
        else if (i < 12)
          slot_script[i - 8][j] = r_buff;
        j++;
        if (j == 4)
        {
          i++;
          j = 0;
        }
      }
      r_buff = "";
    }
    else
      r_buff = r_buff + c;
  }
  delay(2000);
  lcd.clear();
  attachInterrupt(digitalPinToInterrupt(enc_clk), change_option, FALLING);
  attachInterrupt(digitalPinToInterrupt(menu_button), change_state, FALLING);
  attachInterrupt(digitalPinToInterrupt(select_button), select_opt, FALLING);
}

void loop()
{
  // Set time to system time when uploading code to Teensy
  if (Serial.available())
  {
    time_t t = processSyncMessage();
    if (t != 0)
    {
      Teensy3Clock.set(t);
      setTime(t);
    }
  }

  // Convert time from 24 hour format to 12 hour format
  isEvening = "AM";
  h = hour();
  if (h > 12)
  {
    h = h - 12;
    isEvening = "PM";
  }
  else if (h == 0)
    h = 12;
  else if (h == 12)
    isEvening = "PM";
  m = minute();
  lcd.clear();

  // Display everything on the LCD screen
  if (substate == 1)
  {
    lcd.setCursor(1, 0);
    printDigits(slot_hour[rack][0]);
    lcd.print(":");
    printDigits(slot_minute[rack][0]);
    lcd.print(slot_script[rack][0]);
    lcd.setCursor(1, 1);
    printDigits(slot_hour[rack][1]);
    lcd.print(":");
    printDigits(slot_minute[rack][1]);
    lcd.print(slot_script[rack][1]);
    lcd.setCursor(9, 0);
    printDigits(slot_hour[rack][2]);
    lcd.print(":");
    printDigits(slot_minute[rack][2]);
    lcd.print(slot_script[rack][2]);
    lcd.setCursor(9, 1);
    printDigits(slot_hour[rack][3]);
    lcd.print(":");
    printDigits(slot_minute[rack][3]);
    lcd.print(slot_script[rack][3]);
    if (time_slot == 0)
    {
      lcd.setCursor(0, 0);
      lcd.print(">");
    }
    else if (time_slot == 1)
    {
      lcd.setCursor(0, 1);
      lcd.print(">");
    }
    else if (time_slot == 2)
    {
      lcd.setCursor(8, 0);
      lcd.print(">");
    }
    else if (time_slot == 3)
    {
      lcd.setCursor(8, 1);
      lcd.print(">");
    }
  }
  else if (substate == 2)
  {
    lcd.setCursor(5, 0);
    printDigits(slot_hour[rack][time_slot]);
    lcd.print(":");
    printDigits(slot_minute[rack][time_slot]);
    lcd.print(slot_script[rack][time_slot]);
    if (edit_mode == 0)
    {
      lcd.setCursor(5, 1);
      lcd.print("^^");
    }
    else if (edit_mode == 1)
    {
      lcd.setCursor(8, 1);
      lcd.print("^^");
    }
    else if (edit_mode == 2)
    {
      lcd.setCursor(10, 1);
      lcd.print("^^");
    }
  }
  else if (state == 0)
  {
    lcd.setCursor(2, 0);
    printDigits(h);
    lcd.print(":");
    printDigits(m);
    lcd.print(":");
    printDigits(second());
    lcd.print(" ");
    lcd.print(isEvening);
    lcd.setCursor(3, 1);
    lcd.print(day());
    lcd.print("/");
    lcd.print(month());
    lcd.print("/");
    lcd.print(year());
  }
  else if (state == 1)
  {
    lcd.setCursor(1, 0);
    lcd.print("Rack 1");
    lcd.setCursor(1, 1);
    lcd.print("Rack 2");
    lcd.setCursor(10, 0);
    lcd.print("Rack 3");
    lcd.setCursor(10, 1);
    lcd.print("Rack 4");
    if (rack == 0)
    {
      lcd.setCursor(0, 0);
      lcd.print(">");
    }
    else if (rack == 1)
    {
      lcd.setCursor(0, 1);
      lcd.print(">");
    }
    else if (rack == 2)
    {
      lcd.setCursor(9, 0);
      lcd.print(">");
    }
    else if (rack == 3)
    {
      lcd.setCursor(9, 1);
      lcd.print(">");
    }
  }

  // Write changes time slots to the SD card
  if (write_change == 1)
  {
    myFile = SD.open("setting.txt", FILE_WRITE_BEGIN);
    if (myFile)
    {
      for (int i = 0; i < 4; i++)
      {
        for (int j = 0; j < 4; j++)
        {
          myFile.print(",");
          myFile.print(slot_hour[i][j]);
        }
      }
      myFile.println(" ");
      for (int i = 0; i < 4; i++)
      {
        for (int j = 0; j < 4; j++)
        {
          myFile.print(",");
          myFile.print(slot_minute[i][j]);
        }
      }
      myFile.println(" ");
      for (int i = 0; i < 4; i++)
      {
        for (int j = 0; j < 4; j++)
        {
          myFile.print(",");
          myFile.print(slot_script[i][j]);
        }
      }
      myFile.println(",");
    }
    myFile.close();
    write_change = 0;
  }

  // Check if the time matches with the time slot and sound the alarm
  if (m != prev_m)
  {
    for (int i = 0; i < 4; i++)
    {
      for (int j = 0; j < 4; j++)
      {
        if (slot_hour[i][j] == h && slot_script[i][j] == isEvening && slot_minute[i][j] == m)
        {
          alert[i] = 1;
        }
      }
    }
  }

  prev_m = m;
  prev_h = h;
  alarm();
  delay(300);
}