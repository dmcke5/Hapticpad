#include <Arduino.h>
#include <U8g2lib.h>
#include <SimpleFOC.h>
#include <Adafruit_TinyUSB.h>
#include <SPI.h>
#include <SD.h>
#include <FastLED.h>

const unsigned char SD_Card [] PROGMEM = {
	0xff, 0x01, 0x55, 0x01, 0x55, 0x01, 0xff, 0x01, 0x01, 0x03, 0x2d, 0x02, 0x45, 0x03, 0x49, 0x01, 
	0x2d, 0x03, 0x01, 0x02, 0xfd, 0x02, 0x01, 0x02, 0xff, 0x03
};

#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

#define WHEEL_CLICKY    0
#define WHEEL_TWIST     1
#define WHEEL_MOMENTUM  2

#define KEY_LEFT_CTRL 17

#define NUM_LEDS 20
#define DATA_PIN 15

CRGB leds[NUM_LEDS];

//User Configurable LED Settings
byte primaryColour[3] = {0,50,50}; //Red, Green, Blue
byte secondaryColour[3] = {0,0,0};
int ledBrightness = 100;
int ledSpeed = 50;

//LED Sequence Variables
float redScale;
float greenScale;
float blueScale;
int sequenceStep = 0;
int ledMode = 3;
unsigned long ledTimer;

//LED Halo Variables
int haloCount = 0; //Which is the brightest LED in the sequence

//LED Breath Variables
bool breathIncrease = true;

//LED Bands
bool evenNumber = false;
int loopCounter;

bool debug = false;

//SD Pins
const int _MISO = 4;
const int _MOSI = 3;
const int _CS = 5;
const int _SCK = 2;

bool sdDetected = false;
bool useYaml = false; // Added for YAML support

U8G2_SSD1309_128X64_NONAME0_1_4W_SW_SPI u8g2(U8G2_R0, 28, 22, 6, 7, 8);

BLDCMotor motor = BLDCMotor(7);
BLDCDriver6PWM driver = BLDCDriver6PWM(20, 21, 18,19, 16, 17);

//float target_velocity = 6;
Commander command = Commander(Serial);

Encoder encoder = Encoder(27, 26, 1024);
void doA(){encoder.handleA();}
void doB(){encoder.handleB();}

// angle set point variable
float target_angle = 0; //Radians. 1 Rad = 57.2958 Deg
float new_target_angle;

float target_velocity = 0;
float last_velocity = 0;
float testFactor;

float angle_step = radians(360/40);
float encoderAngle;
float lastEncoderAngle;
bool keyPressed = false;
bool wheelKeyPressed = false;

//PID Values

float Clicky_P;
float Clicky_I;
float Twist_P;
float Twist_I;
float Momentum_P;
float Momentum_I;
float motorP = 0.5;
float motorI = 0;
float motorD = 0;

long timer;
long aceltimer;
long mouseTimer;
float interval;
unsigned long wheelKeyTimer;
unsigned long keyTimer;

long debounceTimer;
bool decelDetected = false;
bool decelerating = false;

int wheelMode = 0;
int lastWheelMode = 0;
bool wheelModeChanged = true;

bool FOC_Ready = false;

#define buttonCount 8 //Number of buttons connected
byte buttonPins[buttonCount] = {9, 1, 0, 12, 11, 10, 13, 14}; //1,2,3,4,5,6
int lastButtonState[buttonCount];

//Eeprom Memory
int activeProfile = 0;
//int activePage = 1;

uint8_t icon1[15][2]; //Used to store Icon 1 Data
uint8_t icon2[15][2]; //Used to store Icon 2 Data
uint8_t icon3[15][2]; //Used to store Icon 3 Data
uint8_t icon4[15][2]; //Used to store Icon 4 Data
uint8_t icon5[15][2]; //Used to store Icon 5 Data
uint8_t icon6[15][2]; //Used to store Icon 6 Data

// ---- Profile data ----
#define maxProfileNameLength 32
#define maxProfiles 256
char profileName[maxProfileNameLength];
char profileNames[maxProfileNameLength][maxProfiles];
char buttonLabel[6][32];
unsigned long profileChangeTimer;
bool profilePlusStarted = false;
bool profileMinusStarted = false;
bool profileSelectMenu = false;

uint8_t wheelAction;
uint8_t macroAction[6][3];   // decimal values
uint16_t macroDelay[6][3];
float targetAngle;

// ---- Global ----
uint16_t totalProfiles = 0;

File root;

uint8_t const desc_keyboard_report[] =
{
  TUD_HID_REPORT_DESC_KEYBOARD()
};

uint8_t const desc_mouse_report[] =
{
  TUD_HID_REPORT_DESC_MOUSE()
};

Adafruit_USBD_HID usb_keyboard(desc_keyboard_report, sizeof(desc_keyboard_report), HID_ITF_PROTOCOL_KEYBOARD, 2, false);
Adafruit_USBD_HID usb_mouse(desc_mouse_report, sizeof(desc_mouse_report), HID_ITF_PROTOCOL_MOUSE, 2, false);

void setup() { //Core 0
  driver.voltage_power_supply = 5;
  driver.voltage_limit = 5;
  driver.init();

  encoder.init();
  encoder.enableInterrupts(doA, doB);
  motor.linkSensor(&encoder);

  driver.voltage_power_supply = 5;
  driver.init();

  motor.linkDriver(&driver);
  motor.voltage_sensor_align = 3;

  motor.PID_velocity.D = 0;

  motor.voltage_limit = 3;

  motor.PID_velocity.output_ramp = 1000;
  motor.LPF_velocity.Tf = 0.025f;//0.01f;
  motor.P_angle.P = 20;
  motor.velocity_limit = 4;

  motor.init();
  motor.initFOC();

  Serial.begin(115200);
  if(debug){
    while(!Serial){}
  }
  Serial.println("Motor ready!");
  FOC_Ready = true;
}

void setup1(){ //core 1
  usb_keyboard.begin();
  usb_mouse.begin();

  FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, NUM_LEDS);

  SPI.setRX(_MISO);
  SPI.setTX(_MOSI);
  SPI.setSCK(_SCK);

  if(debug){
    while(!Serial){}
  }

  initialiseSD();

  calculateColourMultiplier();

  u8g2.begin();

  for(int i = 0; i < buttonCount; i++){
    pinMode(buttonPins[i], INPUT_PULLUP);
  }

  while(!FOC_Ready){delay(10);}
}

void buttonRead(){ //Read button inputs and set state arrays.
  for (int i = 0; i < buttonCount; i++){
    int input = !digitalRead(buttonPins[i]);
    if (input != lastButtonState[i]){
      lastButtonState[i] = input;
    }
  }
  if(sdDetected && !profileSelectMenu){
    for(int i = 0; i < 6; i++){
      if(lastButtonState[i]){
        macroOutput(i);
        keyPressed = true;
        keyTimer = millis();
      }
    }
    if(lastButtonState[6]){ //Next Profile
      if(!profilePlusStarted){
        profilePlusStarted = true;
        profileChangeTimer = millis();
      }
      if(profilePlusStarted && profileChangeTimer + 100 < millis()){
        profileSelectMenu = true;
        profileChangeTimer = millis();
      }
    } else {
      if(profilePlusStarted){
        if(activeProfile < totalProfiles - 1){
          activeProfile++;
          if(useYaml) loadProfileYaml("/config.yaml", activeProfile); else loadProfile("/config.xml", activeProfile);
          storeLastProfile();
          Serial.print("Stored last profile = ");
          Serial.println(activeProfile);
          loadButtonIcons();
          delay(100);
        }
      }
    }
    if(lastButtonState[7]){ //Previous Profile
      if(!profileMinusStarted){
        profileMinusStarted = true;
        profileChangeTimer = millis();
      } else if(profileMinusStarted && profileChangeTimer + 100 < millis()){
        profileSelectMenu = true;
        profileChangeTimer = millis();
      }
    } else {
      if(profileMinusStarted){
        if(activeProfile > 0 ){
          activeProfile--;
          if(useYaml) loadProfileYaml("/config.yaml", activeProfile); else loadProfile("/config.xml", activeProfile);
          storeLastProfile();
          Serial.print("Stored last profile = ");
          Serial.println(activeProfile);
          loadButtonIcons();
          delay(100);
        }
      }
    }
    if(!lastButtonState[7] && !lastButtonState[6]){
      profileMinusStarted = false;
      profilePlusStarted = false;
    }
      
    if(keyPressed && keyTimer + 50 < millis()){
      usb_keyboard.keyboardRelease(0);
      keyPressed = false;
    }
  } else if(!sdDetected && !profileSelectMenu){
    if(lastButtonState[4]){
      initialiseSD();
      delay(100);
    }
  } else {
    if(profileChangeTimer + 500 < millis()){
      if(lastButtonState[7] || lastButtonState[6]){
        profileSelectMenu = false;
        profileMinusStarted = false;
        profilePlusStarted = false;
        if(useYaml) loadProfileYaml("/config.yaml", activeProfile); else loadProfile("/config.xml", activeProfile);
        storeLastProfile();
        Serial.print("Stored last profile = ");
        Serial.println(activeProfile);
        loadButtonIcons();
        delay(200);
      }
    }
  }
}

void initialiseSD(){
  SD.end(true); //Attempt to close any previous SD sessions

  if (!SD.begin(_CS)) {
    Serial.println("initialization failed!");
    return;
  } else {
    sdDetected = true;
    Serial.println("SD Initialised!");
  }

  // Detection logic for YAML vs XML
  if (SD.exists("/config.yaml")) {
    useYaml = true;
    totalProfiles = countProfilesYaml("/config.yaml");
    loadSettingsYaml("/config.yaml");
  } else {
    useYaml = false;
    totalProfiles = countProfiles("/config.xml");
    loadSettings("/config.xml");
  }

  activeProfile = readLastProfile();
  Serial.print("Active Profile = ");
  Serial.println(activeProfile);

  if(useYaml) loadProfileYaml("/config.yaml", activeProfile); else loadProfile("/config.xml", activeProfile);
  loadButtonIcons();
}

void loop() {
  motor.loopFOC();

  if(lastWheelMode != wheelMode){
    wheelModeChanged = true;
  }
  lastWheelMode = wheelMode;

  if(!profileSelectMenu){
    if(wheelMode == 0){
      notchyWheel();
    } else if(wheelMode == 1){
      twistScroll();
    } else if(wheelMode == 2){
      freeSpinning();
    }
  } else {
    notchyWheel();
  }
}

void macroOutput(int button){
  uint8_t keycode[6] = { 0 };
  int modifier = 0;
        
  for(int i = 0; i < 3; i++){
    delay(macroDelay[button][i]);
    if(macroAction[button][i] != 0){
      keycode[0] = convertKeycode(macroAction[button][i]);
      if(checkModifiers(macroAction[button][i]) != 0){
        modifier = checkModifiers(macroAction[button][i]);
        Serial.println("Modifier Detected");
      } else {
        usb_keyboard.keyboardReport(0, modifier, keycode);
      }
    }
  }
}

void loop1() {
  u8g2.firstPage();
  buttonRead();
  if(ledTimer + ledSpeed < millis()){
    if(ledMode == 0){
      haloLED();
    } else if(ledMode == 1){
      breathLED();
    } else if(ledMode == 2){
      ledBand();
    } else if(ledMode == 3){
      rainbowLED();
    } else if(ledMode == 4){
      solidLED();
    } else {
      offLED();
    }
    ledTimer = millis();
  }
  
  do {
    if(sdDetected){
      if(!profileSelectMenu){
        drawGrid();
        drawActiveProfile();
      } else {
        drawProfileMenu();
      }

      encoderAngle = encoder.getAngle();

      if(wheelMode == 2){
        if(abs(lastEncoderAngle - encoderAngle) > 0.1){
          wheelActionCheck();
          usb_mouse.mouseScroll(0, (lastEncoderAngle - encoderAngle) * 10, 0);
          lastEncoderAngle = encoderAngle;
        } else {
          cancelWheelAction();
        }
      }
    } else {
      u8g2.drawXBMP(59, 10, 10, 13, SD_Card);
      u8g2.setFont(u8g2_font_5x7_tf);
      u8g2.drawStr(17, 36, "No SD Card Detected!");
      u8g2.drawStr(53, 60, "Retry");
    }

  } while ( u8g2.nextPage() );
}

bool storeLastProfile(){
  SD.remove("/lastProfile");

  File file = SD.open("/lastProfile", FILE_WRITE);
  if (file) {
    file.print(activeProfile);
    file.close();
    return true;
  } else {
    return false;
  }
}

int readLastProfile(){
  File file = SD.open("/lastProfile");

  if (!file){
    return 0;
  }

  return (uint8_t)file.parseInt();
}