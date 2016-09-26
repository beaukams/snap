//
// SNAP -s acoustic recorder
//
// Loggerhead Instruments
// 2016
// David Mann
// 
// Modified from PJRC audio code
// http://www.pjrc.com/store/teensy3_audio.html
//

//#include <SerialFlash.h>
#include <Audio.h>  //this also includes SD.h from lines 89 & 90
#include <Wire.h>
#include <SPI.h>
//#include <SdFat.h>
#include "amx32.h"
#include <Snooze.h>  //using https://github.com/duff2013/Snooze; uncomment line 62 #define USE_HIBERNATE
#include <TimeLib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <TimerOne.h>

#define CPU_RESTART_ADDR (uint32_t *)0xE000ED0C
#define CPU_RESTART_VAL 0x5FA0004
#define CPU_RESTART (*CPU_RESTART_ADDR = CPU_RESTART_VAL);

#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);
#define BOTTOM 55

// set this to the hardware serial port you wish to use
#define HWSERIAL Serial1

static boolean printDiags = 0;  // 1: serial print diagnostics; 0: no diagnostics
static uint8_t myID[8];

unsigned long baud = 115200;

#define SECONDS_IN_MINUTE 60
#define SECONDS_IN_HOUR 3600
#define SECONDS_IN_DAY 86400
#define SECONDS_IN_YEAR 31536000
#define SECONDS_IN_LEAP 31622400

#define MODE_NORMAL 0
#define MODE_DIEL 1

// GUItool: begin automatically generated code
AudioInputI2S            i2s2;           //xy=105,63
AudioRecordQueue         queue1;         //xy=281,63
AudioConnection          patchCord1(i2s2, 0, queue1, 0);
AudioControlSGTL5000     sgtl5000_1;     //xy=265,212
// GUItool: end automatically generated code

const int myInput = AUDIO_INPUT_LINEIN;

// Pin Assignments
const int CAM_POW = 1;
const int hydroPowPin = 2;

// AMX
const int UP = 4;
const int DOWN = 3;  // new board pin
//const int DOWN = 5; // old board down pin
const int SELECT = 8;
const int displayPow = 20;
const int ledGreen = 16;
const int ledRed = 17;
const int BURN1 = 5;
const int SDSW = 0;
const int ledWhite = 21;
const int usbSense = 6;
const int vSense = 21; 
//const int vSense = A14;  // moved to Pin 21 for X1

// Pins used by audio shield
// https://www.pjrc.com/store/teensy3_audio.html
// MEMCS 6
// MOSI 7
// BCLK 9
// SDCS 10
// MCLK 11
// MISO 12
// RX 13
// SCLK 14
// VOL 15
// SDA 18
// SCL 19
// TX 22
// LRCLK 23

// Remember which mode we're doing
int mode = 0;  // 0=stopped, 1=recording, 2=playing
time_t startTime;
time_t stopTime;
time_t t;
time_t burnTime;
byte startHour, startMinute, endHour, endMinute; //used in Diel mode

boolean imuFlag = 0;
boolean rgbFlag = 0;
byte pressure_sensor = 0; //0=none, 1=MS5802, 2=Keller PA7LD
boolean audioFlag = 1;
boolean CAMON = 0;
boolean camFlag = 0;
boolean briteFlag = 0; // bright LED
boolean LEDSON=1;
boolean introperiod=1;  //flag for introductory period; used for keeping LED on for a little while
byte fileType = 0; //0=wav, 1=amx

float sensor_srate = 1.0;
float imu_srate = 100.0;
float audio_srate = 44100.0;

float audioIntervalSec = 256.0 / audio_srate; //buffer interval in seconds
unsigned int audioIntervalCount = 0;

int recMode = MODE_NORMAL;
long rec_dur = 10;
long rec_int = 30;
int wakeahead = 5;  //wake from snooze to give hydrophone and camera time to power up
int snooze_hour;
int snooze_minute;
int snooze_second;
int buf_count;
long nbufs_per_file;
boolean settingsChanged = 0;

long file_count;
char filename[20];
char dirname[7];
int folderMonth;
//SnoozeBlock snooze_config;
SnoozeAlarm alarm;
SnoozeAudio snooze_audio;
SnoozeBlock config_teensy32(snooze_audio, alarm);

// The file where data is recorded
File frec;

typedef struct {
    char    rId[4];
    unsigned int rLen;
    char    wId[4];
    char    fId[4];
    unsigned int    fLen;
    unsigned short nFormatTag;
    unsigned short nChannels;
    unsigned int nSamplesPerSec;
    unsigned int nAvgBytesPerSec;
    unsigned short nBlockAlign;
    unsigned short  nBitsPerSamples;
    char    dId[4];
    unsigned int    dLen;
} HdrStruct;

HdrStruct wav_hdr;
unsigned int rms;
float hydroCal = -164;

// Header for amx files
DF_HEAD dfh;
SID_SPEC sidSpec[SID_MAX];
SID_REC sidRec[SID_MAX];
SENSOR sensor[SENSOR_MAX]; //structure to hold sensor specifications. e.g. MPU9250, MS5803, PA47LD, ISL29125

unsigned char prev_dtr = 0;

// IMU
int FIFOpts;
#define BUFFERSIZE 140 // used this length because it is divisible by 20 bytes (e.g. A*3,M*3,G*3,T) and 14 (w/out mag)
byte imuBuffer[BUFFERSIZE]; // buffer used to store IMU sensor data before writes in bytes
int16_t accel_x;
int16_t accel_y;
int16_t accel_z;
int16_t magnetom_x;
int16_t magnetom_y;
int16_t magnetom_z;
int16_t gyro_x;
int16_t gyro_y;
int16_t gyro_z;
float gyro_temp;

// RGB
int16_t islRed;
int16_t islBlue;
int16_t islGreen;

// Pressure/Temp
byte Tbuff[3];
byte Pbuff[3];
volatile float depth, temperature;
boolean togglePress; //flag to toggle conversion of pressure and temperature

//Pressure and temp calibration coefficients
uint16_t PSENS; //pressure sensitivity
uint16_t POFF;  //Pressure offset
uint16_t TCSENS; //Temp coefficient of pressure sensitivity
uint16_t TCOFF; //Temp coefficient of pressure offset
uint16_t TREF;  //Ref temperature
uint16_t TEMPSENS; //Temperature sensitivity coefficient

// Pressure, Temp double buffer
#define PTBUFFERSIZE 40
float PTbuffer[PTBUFFERSIZE];
byte time2writePT = 0; 
int ptCounter = 0;
volatile byte bufferposPT=0;
byte halfbufPT = PTBUFFERSIZE/2;
boolean firstwrittenPT;

// RGB buffer
#define RGBBUFFERSIZE 120
byte RGBbuffer[RGBBUFFERSIZE];
byte time2writeRGB=0; 
int RGBCounter = 0;
volatile byte bufferposRGB=0;
byte halfbufRGB = RGBBUFFERSIZE/2;
boolean firstwrittenRGB;

void setup() {
  dfh.Version = 1000;
  dfh.UserID = 5555;

  read_myID();
  
  Serial.begin(baud);
  delay(500);
  Wire.begin();

  pinMode(CAM_POW, OUTPUT);
  pinMode(hydroPowPin, OUTPUT);
  pinMode(displayPow, OUTPUT);
  pinMode(ledGreen, OUTPUT);
  pinMode(ledRed, OUTPUT);
  pinMode(BURN1, OUTPUT);
  pinMode(ledWhite, OUTPUT);
  pinMode(SDSW, OUTPUT);
  pinMode(vSense, INPUT);
  analogReference(DEFAULT);


  digitalWrite(SDSW, HIGH); //low SD connected to microcontroller; HIGH SD connected to external pins
  digitalWrite(CAM_POW,  LOW);
  digitalWrite(hydroPowPin, LOW);
  digitalWrite(displayPow, HIGH);
  digitalWrite(ledGreen, LOW);
  digitalWrite(ledRed, LOW);
  digitalWrite(BURN1, LOW);
  digitalWrite(ledWhite, LOW);


  pinMode(usbSense, OUTPUT);
  digitalWrite(usbSense, LOW); // make sure no pull-up
  pinMode(usbSense, INPUT);
  
  //setup display and controls
  pinMode(UP, INPUT);
  pinMode(DOWN, INPUT);
  pinMode(SELECT, INPUT);
  digitalWrite(UP, HIGH);
  digitalWrite(DOWN, HIGH);
  digitalWrite(SELECT, HIGH);

  delay(500);    

  setSyncProvider(getTeensy3Time); //use Teensy RTC to keep time
  t = getTeensy3Time();
  if (t < 1451606400) Teensy3Clock.set(1451606400);
  startTime = getTeensy3Time();
  stopTime = startTime + rec_dur;

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  //initialize display
  delay(100);
  cDisplay();
  display.println("Loggerhead");
//  Serial.println("Loggerhead");
//  display.println("USB <->");
  display.display();
  // Check for external USB connection to microSD
// while(digitalRead(usbSense)){
//    delay(500);
//  }

  // Power down USB if not using Serial monitor
//  if (printDiags==0){
//      usbDisable();
//  }
  
  pinMode(usbSense, OUTPUT);  //not using any more, set to OUTPUT
  digitalWrite(usbSense, LOW); 

  cDisplay();
  display.println("Loggerhead");
  display.display();
  
  digitalWrite(SDSW, LOW); //no USB connected, switch to microcontroller read SD card
  delay(200);
  // Initialize the SD card
  SPI.setMOSI(7);
  SPI.setSCK(14);
  if (!(SD.begin(10))) {
    // stop here if no SD card, but print a message
    Serial.println("Unable to access the SD card");
    
    while (1) {
      cDisplay();
      display.println("SD error. Restart.");
      displayClock(getTeensy3Time(), BOTTOM);
      display.display();
      delay(1000);
      
    }
  }
  //SdFile::dateTimeCallback(file_date_time);
   
  //if (!LoadScript())  // if no script file, go to manual settings
  manualSettings();

  setupDataStructures();

  // disable buttons; not using any more
  digitalWrite(UP, LOW);
  digitalWrite(DOWN, LOW);
  digitalWrite(SELECT, LOW);
  pinMode(UP, OUTPUT);
  pinMode(DOWN, OUTPUT);
  pinMode(SELECT, OUTPUT);

  int ecode;
  kellerInit();  
  ecode = mpuInit(1);
  islInit(); // RGB light sensor
  pressInit();
  
  cDisplay();
  
  t = getTeensy3Time();
  if (startTime < t)
  {  
    startTime -= startTime % 300;  //modulo to nearest 5 minutes
    startTime += 300; //move forward
    stopTime = startTime + rec_dur;  // this will be set on start of recording
  }
 // if (recMode==MODE_DIEL) checkDielTime();  
  
  nbufs_per_file = (long) (rec_dur * audio_srate / 256.0);
  long ss = rec_int - wakeahead;
  if (ss<0) ss=0;
  snooze_hour = floor(ss/3600);
  ss -= snooze_hour * 3600;
  snooze_minute = floor(ss/60);
  ss -= snooze_minute * 60;
  snooze_second = ss;
  Serial.print("Snooze HH MM SS ");
  Serial.print(snooze_hour);
  Serial.print(snooze_minute);
  Serial.println(snooze_second);

  Serial.print("rec dur ");
  Serial.println(rec_dur);
  Serial.print("rec int ");
  Serial.println(rec_int);
  Serial.print("Current Time: ");
  printTime(t);
  Serial.print("Start Time: ");
  printTime(startTime);
  
  // Sleep here if won't start for 60 s
  long time_to_first_rec = startTime - t;
  Serial.print("Time to first record ");
  Serial.println(time_to_first_rec);

  // Audio connections require memory, and the record queue
  // uses this memory to buffer incoming audio.
  AudioMemory(100);
  AudioInit(); // this calls Wire.begin() in control_sgtl5000.cpp
  
  digitalWrite(hydroPowPin, HIGH);
  if (camFlag) cam_wake();
  mode = 0;

  // create first folder to hold data
  folderMonth = -1;  //set to -1 so when first file made will create directory
  
  if (fileType) Timer1.initialize(100000); // initialize with 100 ms period
}

//
// MAIN LOOP
//

int recLoopCount;  //for debugging when does not start record
  
void loop() {
  // Standby mode
  if(mode == 0)
  {
      t = getTeensy3Time();
      cDisplay();
      display.println("Next Start");
      displayClock(startTime, 20);
      displayClock(t, BOTTOM);
      display.display();
      
      if(t >= burnTime){
        digitalWrite(BURN1, HIGH);
      }
      if(t >= startTime){      // time to start?
        Serial.println("Record Start.");
        
        stopTime = startTime + rec_dur;
        startTime = stopTime + rec_int;
      //  if (recMode==MODE_DIEL) checkDielTime();

        Serial.print("Current Time: ");
        printTime(getTeensy3Time());
        Serial.print("Stop Time: ");
        printTime(stopTime);
        Serial.print("Next Start:");
        printTime(startTime);

//        cDisplay();
//        display.println("Rec");
//        display.setTextSize(1);
//        display.print("Stop Time: ");
//        displayClock(stopTime, 30);
//        display.display();

        mode = 1;

        startRecording();
        if (camFlag)  cam_start();
      }
  }


  // Record mode
  if (mode == 1) {
    continueRecording();  // download data  
    
     // update clock while recording
      recLoopCount++;
      if(recLoopCount>50){
        recLoopCount = 0;
        t = getTeensy3Time();
        cDisplay();
        if(rec_int > 0) {
          display.println("Rec");
          displayClock(stopTime, 20);
        }
        else{
          display.println("Rec Contin");
          display.setTextSize(1);
          display.println(filename);
        }
        displayClock(t, BOTTOM);
        display.display();
      }
   
    
    // write Pressure & Temperature to file
    if(time2writePT==1)
    {
      if(LEDSON | introperiod) digitalWrite(ledRed,HIGH);
      if(frec.write((uint8_t *)&sidRec[1],sizeof(SID_REC))==-1) resetFunc();
      if(frec.write((uint8_t *)&PTbuffer[0], halfbufPT * 4)==-1) resetFunc(); 
      time2writePT = 0;
      if(LEDSON | introperiod) digitalWrite(ledRed,LOW);
    }
    if(time2writePT==2)
    {
      if(LEDSON | introperiod) digitalWrite(ledRed,HIGH);
      if(frec.write((uint8_t *)&sidRec[1],sizeof(SID_REC))==-1) resetFunc();
      if(frec.write((uint8_t *)&PTbuffer[halfbufPT], halfbufPT * 4)==-1) resetFunc();     
      time2writePT = 0;
      if(LEDSON | introperiod) digitalWrite(ledRed,LOW);
    }   
  
    // write RGB values to file
    if(time2writeRGB==1)
    {
      if(LEDSON | introperiod) digitalWrite(ledRed,HIGH);
      if(frec.write((uint8_t *)&sidRec[2],sizeof(SID_REC))==-1) resetFunc();
      if(frec.write((uint8_t *)&RGBbuffer[0], halfbufRGB)==-1) resetFunc(); 
      time2writeRGB = 0;
      if(LEDSON | introperiod) digitalWrite(ledRed,LOW);
    }
    if(time2writeRGB==2)
    {
      if(LEDSON | introperiod) digitalWrite(ledRed,HIGH);
      if(frec.write((uint8_t *)&sidRec[2],sizeof(SID_REC))==-1) resetFunc();
      if(frec.write((uint8_t *)&RGBbuffer[halfbufRGB], halfbufRGB)==-1) resetFunc();     
      time2writeRGB = 0;
      if(LEDSON | introperiod) digitalWrite(ledRed,LOW);
    } 
      
    if(buf_count >= nbufs_per_file){       // time to stop?
      if(rec_int == 0){
        frec.close();
        FileInit();  // make a new file
        buf_count = 0;
      }
      else{
      
                stopRecording();
          
                long ss = startTime - getTeensy3Time() - wakeahead;
                if (ss<0) ss=0;
                snooze_hour = floor(ss/3600);
                ss -= snooze_hour * 3600;
                snooze_minute = floor(ss/60);
                ss -= snooze_minute * 60;
                snooze_second = ss;
          
                if( snooze_hour + snooze_minute + snooze_second >=10){
                    digitalWrite(hydroPowPin, LOW); //hydrophone off
                    mpuInit(0);  //gyro to sleep
                    islSleep(); // RGB light sensor
                    audio_power_down();
                    if (camFlag) cam_off();
                    cDisplay();
                    display.display();
                    delay(100);
                    display.ssd1306_command(SSD1306_DISPLAYOFF); 
                    if(printDiags){
                      Serial.print("Snooze HH MM SS ");
                      Serial.print(snooze_hour);
                      Serial.print(snooze_minute);
                      Serial.println(snooze_second);
                    }
                    
                    delay(100);
          
                   // AudioNoInterrupts();
          
                    
                    //snooze_config.setAlarm(snooze_hour, snooze_minute, snooze_second);
                    //delay(100);
                    //Snooze.sleep( snooze_config );
                    //Snooze.deepSleep(snooze_config);
                    //Snooze.hibernate( snooze_config);
          
                    alarm.setAlarm(snooze_hour, snooze_minute, snooze_second);
                    Snooze.sleep(config_teensy32);
          
                    
                    /// ... Sleeping ....
                    
                    // Waking up
                   // if (printDiags==0) usbDisable();
                    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  //initialize display
                    digitalWrite(hydroPowPin, HIGH); // hydrophone on
           
                  //  audio_enable();
                  //  AudioInterrupts();
                    
                    audio_power_up();
                    if (camFlag)  cam_wake();
                    islInit(); // RGB light sensor
                    mpuInit(1);  //start gyro
                    //sdInit();  //reinit SD because voltage can drop in hibernate
                 }
          
                    
                mode = 0;
      }
    }
  }
}

void startRecording() {
  Serial.println("startRecording");
  FileInit();
  resetGyroFIFO();
  if (fileType) Timer1.attachInterrupt(sampleSensors);
  buf_count = 0;
  queue1.begin();
  Serial.println("Queue Begin");
}

void continueRecording() {
  if (queue1.available() >= 2) {
    byte buffer[512];
    // Fetch 2 blocks from the audio library and copy
    // into a 512 byte buffer.  The Arduino SD library
    // is most efficient when full 512 byte sector size
    // writes are used.
    //digitalWrite(ledGreen, HIGH);
    memcpy(buffer, queue1.readBuffer(), 256);
    queue1.freeBuffer();
    memcpy(buffer+256, queue1.readBuffer(), 256);
    queue1.freeBuffer();
    if (fileType==0){
      frec.write(buffer, 512); //audio to .wav file
    }
    else{
      frec.write((uint8_t *)&sidRec[0],sizeof(SID_REC)); //audio to .amx file
      frec.write(buffer, 512); 
    }
      
    buf_count += 1;
    audioIntervalCount += 1;

    if (fileType & imuFlag){
      if (pollImu()){
        if(frec.write((uint8_t *)&sidRec[3],sizeof(SID_REC))==-1) resetFunc();
        if(frec.write((uint8_t *)&imuBuffer[0], BUFFERSIZE)==-1) resetFunc();  
      }
    }
    
    if(printDiags){
      Serial.print(".");
   }
   
   // digitalWrite(ledGreen, LOW);

    // we are updating these here because reading within interrupt causes board to seize
    float audioDuration = audioIntervalCount * audioIntervalSec;
    if (fileType & (audioDuration > (1.0 /sensor_srate))){
      audioIntervalCount = 0;
      if (rgbFlag) islRead(); 

      // MS5803 pressure and temperature
      if (pressure_sensor==1){
        if(togglePress){
          readPress();
          updateTemp();
          togglePress = 0;
        }
        else{
          readTemp();
          updatePress();
          togglePress = 1;
        }
        calcPressTemp();
      }
  
      // Keller PA7LD pressure and temperature
      if (pressure_sensor==2){
        kellerRead();
        kellerConvert();  // start conversion for next reading
      }
    }
  }
}

void stopRecording() {
  Serial.println("stopRecording");
  int maxblocks = AudioMemoryUsageMax();
  Serial.print("Audio Memory Max");
  Serial.println(maxblocks);
  byte buffer[512];
  queue1.end();
  //queue1.clear();

  AudioMemoryUsageMaxReset();
  if (fileType) Timer1.detachInterrupt();

  // to do: add flush for PTbuffer and Gyro
  
  //frec.timestamp(T_WRITE,(uint16_t) year(t),month(t),day(t),hour(t),minute(t),second);
  frec.close();
  delay(100);
  //calcRMS();
  //Serial.println(rms);
}

// increment PTbuffer position by 1 sample. This does not check for overflow, because collected at a slow rate
void incrementPTbufpos(){
  bufferposPT++;
   if(bufferposPT==PTBUFFERSIZE)
   {
     bufferposPT=0;
     time2writePT=2;  // set flag to write second half
     firstwrittenPT=0; 
   }
 
  if((bufferposPT>=halfbufPT) & !firstwrittenPT)  //at end of first buffer
  {
    time2writePT=1; 
    firstwrittenPT=1;  //flag to prevent first half from being written more than once; reset when reach end of double buffer
  }
}

void incrementRGBbufpos(unsigned short val){
  RGBbuffer[bufferposRGB] = (uint8_t) val;
  bufferposRGB++;
  RGBbuffer[bufferposRGB] = (uint8_t) val>>8;
  bufferposRGB++;
  
   if(bufferposRGB==RGBBUFFERSIZE)
   {
     bufferposRGB = 0;
     time2writeRGB= 2;  // set flag to write second half
     firstwrittenRGB = 0; 
   }
 
  if((bufferposRGB>=halfbufRGB) & !firstwrittenRGB)  //at end of first buffer
  {
    time2writeRGB = 1; 
    firstwrittenRGB = 1;  //flag to prevent first half from being written more than once; reset when reach end of double buffer
  }
}

void setupDataStructures(void){
  // setup sidSpec and sidSpec buffers...hard coded for now
  
  // audio
  strncpy(sensor[0].chipName, "SGTL5000", STR_MAX);
  sensor[0].nChan = 1;
  strncpy(sensor[0].name[0], "audio1", STR_MAX);
  strncpy(sensor[0].name[1], "audio2", STR_MAX);
  strncpy(sensor[0].name[2], "audio3", STR_MAX);
  strncpy(sensor[0].name[3], "audio4", STR_MAX);
  strncpy(sensor[0].units[0], "Pa", STR_MAX);
  strncpy(sensor[0].units[1], "Pa", STR_MAX);
  strncpy(sensor[0].units[2], "Pa", STR_MAX);
  strncpy(sensor[0].units[3], "Pa", STR_MAX);
  sensor[0].cal[0] = -180.0; // this needs to be set based on hydrophone sensitivity + chip gain
  sensor[0].cal[1] = -180.0;
  sensor[0].cal[2] = -180.0;
  sensor[0].cal[3] = -180.0;

  // Pressure/Temperature
  if(pressure_sensor == 1) {
    strncpy(sensor[1].chipName, "MS5803", STR_MAX);
    sensor[1].nChan = 2;
    strncpy(sensor[1].name[0], "pressure", STR_MAX);
    strncpy(sensor[1].name[1], "temp", STR_MAX);
    strncpy(sensor[1].units[0], "mBar", STR_MAX);
    strncpy(sensor[1].units[1], "degreesC", STR_MAX);
    sensor[1].cal[0] = 1.0;
    sensor[1].cal[1] = 1.0;
  }
  else{
    strncpy(sensor[1].chipName, "PA7LD", STR_MAX);
    sensor[1].nChan = 2;
    strncpy(sensor[1].name[0], "pressure", STR_MAX);
    strncpy(sensor[1].name[1], "temp", STR_MAX);
    strncpy(sensor[1].units[0], "mBar", STR_MAX);
    strncpy(sensor[1].units[1], "degreesC", STR_MAX);
    sensor[1].cal[0] = 1.0;
    sensor[1].cal[1] = 1.0;
  }

  
  // RGB light
  strncpy(sensor[2].chipName, "ISL29125", STR_MAX);
  sensor[2].nChan = 3;
  strncpy(sensor[2].name[0], "red", STR_MAX);
  strncpy(sensor[2].name[1], "green", STR_MAX);
  strncpy(sensor[2].name[2], "blue", STR_MAX);
  strncpy(sensor[2].units[0], "uWpercm2", STR_MAX);
  strncpy(sensor[2].units[1], "uWpercm2", STR_MAX);
  strncpy(sensor[2].units[2], "uWpercm2", STR_MAX);
  sensor[2].cal[0] = 20.0 / 65536.0;
  sensor[2].cal[1] = 18.0 / 65536.0;
  sensor[2].cal[2] = 30.0 / 65536.0;


  // IMU
  strncpy(sensor[3].chipName, "MPU9250", STR_MAX);
  sensor[3].nChan = 10;
  strncpy(sensor[3].name[0], "accelX", STR_MAX);
  strncpy(sensor[3].name[1], "accelY", STR_MAX);
  strncpy(sensor[3].name[2], "accelZ", STR_MAX);
  strncpy(sensor[3].name[3], "temp-21C", STR_MAX);
  strncpy(sensor[3].name[4], "gyroX", STR_MAX);
  strncpy(sensor[3].name[5], "gyroY", STR_MAX);
  strncpy(sensor[3].name[6], "gyroZ", STR_MAX);
  strncpy(sensor[3].name[7], "magX", STR_MAX);
  strncpy(sensor[3].name[8], "magY", STR_MAX);
  strncpy(sensor[3].name[9], "magZ", STR_MAX);
  strncpy(sensor[3].units[0], "g", STR_MAX);
  strncpy(sensor[3].units[1], "g", STR_MAX);
  strncpy(sensor[3].units[2], "g", STR_MAX);
  strncpy(sensor[3].units[3], "degreesC", STR_MAX);
  strncpy(sensor[3].units[4], "degPerS", STR_MAX);
  strncpy(sensor[3].units[5], "degPerS", STR_MAX);
  strncpy(sensor[3].units[6], "degPerS", STR_MAX);
  strncpy(sensor[3].units[7], "uT", STR_MAX);
  strncpy(sensor[3].units[8], "uT", STR_MAX);
  strncpy(sensor[3].units[9], "uT", STR_MAX);
  
  float accelFullRange = 16.0; //ACCEL_FS_SEL 2g(00), 4g(01), 8g(10), 16g(11)
  int gyroFullRange = 1000.0;  // FS_SEL 250deg/s (0), 500 (1), 1000(2), 2000 (3)
  int magFullRange = 4800.0;  // fixed
  
  sensor[3].cal[0] = accelFullRange / 32768.0;
  sensor[3].cal[1] = accelFullRange / 32768.0;
  sensor[3].cal[2] = accelFullRange / 32768.0;
  sensor[3].cal[3] = 1.0 / 337.87;
  sensor[3].cal[4] = gyroFullRange / 32768.0;
  sensor[3].cal[5] = gyroFullRange / 32768.0;
  sensor[3].cal[6] = gyroFullRange / 32768.0;
  sensor[3].cal[7] = magFullRange / 32768.0;
  sensor[3].cal[8] = magFullRange / 32768.0;
  sensor[3].cal[9] = magFullRange / 32768.0;
}

int addSid(int i, char* sid,  unsigned int sidType, unsigned long nSamples, SENSOR sensor, unsigned long dForm, float srate)
{
  unsigned long nBytes;
//  memcpy(&_sid, sid, 5);
//
//  memset(&sidSpec[i], 0, sizeof(SID_SPEC));
//        nBytes<<1;  //multiply by two because halfbuf
//
//  switch(dForm)
//  {
//    case DFORM_SHORT:
//      nBytes = nElements * 2;
//      break;            
//    case DFORM_LONG:
//      nBytes = nElements * 4;  //32 bit values
//      break;            
//    case DFORM_I24:
//      nBytes = nElements * 3;  //24 bit values
//      break;
//    case DFORM_FLOAT32:
//      nBytes = nElements * 4;
//      break;
//  }

  strncpy(sidSpec[i].SID, sid, STR_MAX);
  sidSpec[i].sidType = sidType;
  sidSpec[i].nSamples = nSamples;
  sidSpec[i].dForm = dForm;
  sidSpec[i].srate = srate;
  sidSpec[i].sensor = sensor;  
  
  if(frec.write((uint8_t *)&sidSpec[i], sizeof(SID_SPEC))==-1)  resetFunc();

  sidRec[i].nSID = i;
  sidRec[i].NU[0] = 100; //put in something easy to find when searching raw file
  sidRec[i].NU[1] = 200;
  sidRec[i].NU[2] = 300; 
}


/*
void sdInit(){
     if (!(SD.begin(10))) {
    // stop here if no SD card, but print a message
    Serial.println("Unable to access the SD card");
    
    while (1) {
      cDisplay();
      display.println("SD error. Restart.");
      displayClock(getTeensy3Time(), BOTTOM);
      display.display();
      delay(1000);
      
    }
  }
}
*/

void FileInit()
{
   t = getTeensy3Time();
   
   if (folderMonth != month(t)){
    if(printDiags) Serial.println("New Folder");
    folderMonth = month(t);
    sprintf(dirname, "%04d-%02d", year(t), folderMonth);
    SdFile::dateTimeCallback(file_date_time);
    SD.mkdir(dirname);
   }
   pinMode(vSense, INPUT);  // get ready to read voltage

   // only audio save as wav file, otherwise save as AMX file
   
   // open file 
   if(fileType==0)
      sprintf(filename,"%s/%02d%02d%02d%02d.wav", dirname, day(t), hour(t), minute(t), second(t));  //filename is DDHHMM
    else
      sprintf(filename,"%s/%02d%02d%02d%02d.amx", dirname, day(t), hour(t), minute(t), second(t));  //filename is DDHHMM

   // log file
   SdFile::dateTimeCallback(file_date_time);

   float voltage;
   voltage = 0;
   for(int n = 0; n<8; n++){
    voltage += (float) analogRead(vSense) / 1024.0;
    delay(2);
   }
   voltage = voltage / 8.0;
   
   if(File logFile = SD.open("LOG.CSV",  O_CREAT | O_APPEND | O_WRITE)){
      logFile.print(filename);
      logFile.print(',');
      for(int n=0; n<8; n++){
        logFile.print(myID[n]);
      }
      logFile.print(',');
      logFile.println(5.9 * voltage);  //fudging scaling based on actual measurements; shoud be max of 3.3V at 1023
      logFile.close();
   }
   else{
    if(printDiags) Serial.print("Log open fail.");
   }
    
   frec = SD.open(filename, O_WRITE | O_CREAT | O_EXCL);
   Serial.println(filename);
   delay(100);
   
   while (!frec){
    file_count += 1;
    if(fileType==0)
      sprintf(filename,"F%06d.wav",file_count); //if can't open just use count
      else
      sprintf(filename,"F%06d.amx",file_count); //if can't open just use count
    frec = SD.open(filename, O_WRITE | O_CREAT | O_EXCL);
    Serial.println(filename);
    delay(10);
   }

   if(fileType==0){
      //intialize .wav file header
      sprintf(wav_hdr.rId,"RIFF");
      wav_hdr.rLen=36;
      sprintf(wav_hdr.wId,"WAVE");
      sprintf(wav_hdr.fId,"fmt ");
      wav_hdr.fLen=0x10;
      wav_hdr.nFormatTag=1;
      wav_hdr.nChannels=1;
      wav_hdr.nSamplesPerSec=audio_srate;
      wav_hdr.nAvgBytesPerSec=audio_srate*2;
      wav_hdr.nBlockAlign=2;
      wav_hdr.nBitsPerSamples=16;
      sprintf(wav_hdr.dId,"data");
      wav_hdr.rLen = 36 + nbufs_per_file * 256 * 2;
      wav_hdr.dLen = nbufs_per_file * 256 * 2;
    
      frec.write((uint8_t *)&wav_hdr, 44);
   }

   //amx file header
   dfh.voltage = 7.5 * (float) analogRead(vSense) / 1024.0;
   pinMode(vSense, OUTPUT);  // done reading voltage
   if(fileType==1){
    // write DF_HEAD
    dfh.RecStartTime.sec = second();  
    dfh.RecStartTime.minute = minute();  
    dfh.RecStartTime.hour = hour();  
    dfh.RecStartTime.day = day();  
    dfh.RecStartTime.month = month();  
    dfh.RecStartTime.year = (int16_t) year();  
    dfh.RecStartTime.tzOffset = 0; //offset from GMT
    frec.write((uint8_t *) &dfh, sizeof(dfh));
    
    // write SID_SPEC depending on sensors chosen
    addSid(0, "AUDIO", RAW_SID, 256, sensor[0], DFORM_SHORT, audio_srate);
    if (pressure_sensor>0) addSid(1, "PT", RAW_SID, halfbufPT, sensor[1], DFORM_FLOAT32, sensor_srate);    
    if (rgbFlag) addSid(2, "light", RAW_SID, halfbufRGB / 2, sensor[2], DFORM_SHORT, sensor_srate);
    if (imuFlag) addSid(3, "IMU", RAW_SID, BUFFERSIZE / 2, sensor[3], DFORM_SHORT, imu_srate);
    addSid(4, "END", 0, 0, sensor[4], 0, 0);
  }

  Serial.print("Buffers: ");
  Serial.println(nbufs_per_file);
}

//This function returns the date and time for SD card file access and modify time. One needs to call in setup() to register this callback function: SdFile::dateTimeCallback(file_date_time);
void file_date_time(uint16_t* date, uint16_t* time) 
{
  t = getTeensy3Time();
  *date=FAT_DATE(year(t),month(t),day(t));
  *time=FAT_TIME(hour(t),minute(t),second(t));
}

void cam_wake() {
  digitalWrite(CAM_POW, HIGH);
  delay(2000); //power on camera (if off)
  digitalWrite(CAM_POW, LOW);      
  CAMON=1;   
}

void cam_start() {
  digitalWrite(CAM_POW, HIGH);
  delay(500);  // simulate Flywire button press
  digitalWrite(CAM_POW, LOW);  
  if (briteFlag) digitalWrite(ledWhite, HIGH);        
}

void cam_off() {
  digitalWrite(CAM_POW, HIGH);
  delay(3000); //power down camera (if still on)
  digitalWrite(CAM_POW, LOW);           
  CAMON=0;
  if (briteFlag) digitalWrite(ledWhite, LOW);
}

void AudioInit(){
    // Enable the audio shield, select input, and enable output
 // sgtl5000_1.enable();

 // Instead of using audio library enable; do custom so only power up what is needed in sgtl5000_LHI
  audio_enable();
 
  sgtl5000_1.inputSelect(myInput);
  sgtl5000_1.volume(0.0);
  sgtl5000_1.lineInLevel(2);  //default = 8
  // CHIP_ANA_ADC_CTRL
// Actual measured full-scale peak-to-peak sine wave input for max signal
//  0: 3.12 Volts p-p
//  1: 2.63 Volts p-p
//  2: 2.22 Volts p-p
//  3: 1.87 Volts p-p
//  4: 1.58 Volts p-p
//  5: 1.33 Volts p-p
//  6: 1.11 Volts p-p
//  7: 0.94 Volts p-p
//  8: 0.79 Volts p-p (+8.06 dB)
//  9: 0.67 Volts p-p
// 10: 0.56 Volts p-p
// 11: 0.48 Volts p-p
// 12: 0.40 Volts p-p
// 13: 0.34 Volts p-p
// 14: 0.29 Volts p-p
// 15: 0.24 Volts p-p
  sgtl5000_1.autoVolumeDisable();
  sgtl5000_1.audioProcessorDisable();
}

void checkDielTime(){
  unsigned int startMinutes = (startHour * 60) + (startMinute);
  unsigned int endMinutes = (endHour * 60) + (endMinute );
  unsigned int startTimeMinutes =  (hour(startTime) * 60) + (minute(startTime));
  
  tmElements_t tmStart;
  tmStart.Year = year(startTime) - 1970;
  tmStart.Month = month(startTime);
  tmStart.Day = day(startTime);
  // check if next startTime is between startMinutes and endMinutes
  // e.g. 06:00 - 12:00 or 
  if(startMinutes<endMinutes){
     if ((startTimeMinutes < startMinutes) | (startTimeMinutes > endMinutes)){
       // set startTime to startHour startMinute
       tmStart.Hour = startHour;
       tmStart.Minute = startMinute;
       tmStart.Second = 0;
       startTime = makeTime(tmStart);
       Serial.print("New diel start:");
       printTime(startTime);
       if(startTime < getTeensy3Time()) startTime += SECS_PER_DAY;  // make sure after current time
       Serial.print("New diel start:");
       printTime(startTime);
       }
     }
  else{  // e.g. 23:00 - 06:00
    if((startTimeMinutes<startMinutes) & (startTimeMinutes>endMinutes)){
      // set startTime to startHour:startMinute
       tmStart.Hour = startHour;
       tmStart.Minute = startMinute;
       tmStart.Second = 0;
       startTime = makeTime(tmStart);
       Serial.print("New diel start:");
       printTime(startTime);
       if(startTime < getTeensy3Time()) startTime += SECS_PER_DAY;  // make sure after current time
       Serial.print("New diel start:");
       printTime(startTime);
    }
  }
}

time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}

unsigned long processSyncMessage() {
  unsigned long pctime = 0L;
  const unsigned long DEFAULT_TIME = 1451606400; // Jan 1 2016
} 
  
// Calculates Accurate UNIX Time Based on RTC Timestamp
unsigned long RTCToUNIXTime(TIME_HEAD *tm){
    int i;
    unsigned const char DaysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned long Ticks = 0;

    long yearsSince = tm->year + 30; // Years since 1970
    long numLeaps = yearsSince >> 2; // yearsSince / 4 truncated

    if((!(tm->year%4)) && (tm->month>2))
            Ticks+=SECONDS_IN_DAY;  //dm 8/9/2012  If current year is leap, add one day

    // Calculate Year Ticks
    Ticks += (yearsSince-numLeaps)*SECONDS_IN_YEAR;
    Ticks += numLeaps * SECONDS_IN_LEAP;

    // Calculate Month Ticks
    for(i=0; i < tm->month-1; i++){
         Ticks += DaysInMonth[i] * SECONDS_IN_DAY;
    }

    // Calculate Day Ticks
    Ticks += (tm->day - 1) * SECONDS_IN_DAY;

    // Calculate Time Ticks CHANGES ARE HERE
    Ticks += (ULONG)tm->hour * SECONDS_IN_HOUR;
    Ticks += (ULONG)tm->minute * SECONDS_IN_MINUTE;
    Ticks += tm->sec;

    return Ticks;
}

void sampleSensors(void){  //interrupt at 10 Hz
  ptCounter++;
    
  if(ptCounter>=(1.0 / sensor_srate) / 0.1){
      ptCounter = 0;
      if (rgbFlag){
  //      islRead();  
        //RGBbuffer[bufferposRGB] = islRed;
        incrementRGBbufpos(islRed);
        //RGBbuffer[bufferposRGB] = islGreen;
        incrementRGBbufpos(islGreen);
       // RGBbuffer[bufferposRGB] = islBlue;
        incrementRGBbufpos(islBlue);
        // Serial.print("RGB:");Serial.print("\t");
        //Serial.print(islRed); Serial.print("\t");
        //Serial.print(islGreen); Serial.print("\t");
        //Serial.println(islBlue); 
      }
      
      // MS5803 pressure and temperature
      if (pressure_sensor>0){
        PTbuffer[bufferposPT] = depth;
        incrementPTbufpos();
        PTbuffer[bufferposPT] = temperature;
        incrementPTbufpos();
        if(printDiags){
          Serial.print("Depth/Temp: "); Serial.print("\t");
          Serial.print(depth); Serial.print("\t");
          Serial.println(temperature);
        }
      }
  }
}

boolean pollImu(){
  FIFOpts=getImuFifo();
  //Serial.print("IMU FIFO pts: ");
  //if (printDiags) Serial.println(FIFOpts);
  if(FIFOpts>BUFFERSIZE)  //once have enough data for a block, download and write to disk
  {
     Read_Gyro(BUFFERSIZE);  //download block from FIFO
  
     
    if (printDiags){
    // print out first line of block
    // MSB byte first, then LSB, X,Y,Z
    accel_x = (int16_t) ((int16_t)imuBuffer[0] << 8 | imuBuffer[1]);    
    accel_y = (int16_t) ((int16_t)imuBuffer[2] << 8 | imuBuffer[3]);   
    accel_z = (int16_t) ((int16_t)imuBuffer[4] << 8 | imuBuffer[5]);    
    
    gyro_temp = (int16_t) (((int16_t)imuBuffer[6]) << 8 | imuBuffer[7]);   
   
    gyro_x = (int16_t)  (((int16_t)imuBuffer[8] << 8) | imuBuffer[9]);   
    gyro_y = (int16_t)  (((int16_t)imuBuffer[10] << 8) | imuBuffer[11]); 
    gyro_z = (int16_t)  (((int16_t)imuBuffer[12] << 8) | imuBuffer[13]);   
    
    magnetom_x = (int16_t)  (((int16_t)imuBuffer[14] << 8) | imuBuffer[15]);   
    magnetom_y = (int16_t)  (((int16_t)imuBuffer[16] << 8) | imuBuffer[17]);   
    magnetom_z = (int16_t)  (((int16_t)imuBuffer[18] << 8) | imuBuffer[19]);  

    Serial.print("a/g/m/t:\t");
    Serial.print( accel_x); Serial.print("\t");
    Serial.print( accel_y); Serial.print("\t");
    Serial.print( accel_z); Serial.print("\t");
    Serial.print(gyro_x); Serial.print("\t");
    Serial.print(gyro_y); Serial.print("\t");
    Serial.print(gyro_z); Serial.print("\t");
    Serial.print(magnetom_x); Serial.print("\t");
    Serial.print(magnetom_y); Serial.print("\t");
    Serial.print(magnetom_z); Serial.print("\t");
    Serial.println((float) gyro_temp/337.87+21);
    }
    
    return true;
  }
  return false;
}



void resetFunc(void){
  CPU_RESTART
}


void read_EE(uint8_t word, uint8_t *buf, uint8_t offset)  {
  noInterrupts();
  FTFL_FCCOB0 = 0x41;             // Selects the READONCE command
  FTFL_FCCOB1 = word;             // read the given word of read once area

  // launch command and wait until complete
  FTFL_FSTAT = FTFL_FSTAT_CCIF;
  while(!(FTFL_FSTAT & FTFL_FSTAT_CCIF))
    ;
  *(buf+offset+0) = FTFL_FCCOB4;
  *(buf+offset+1) = FTFL_FCCOB5;       
  *(buf+offset+2) = FTFL_FCCOB6;       
  *(buf+offset+3) = FTFL_FCCOB7;       
  interrupts();
}

    
void read_myID() {
  read_EE(0xe,myID,0); // should be 04 E9 E5 xx, this being PJRC's registered OUI
  read_EE(0xf,myID,4); // xx xx xx xx

}