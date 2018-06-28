/*
   The bGeigie-nano
   A device for car-borne radiation measurement (aka Radiation War-driving).

   Copyright (c) 2013/17, Rob Oudendijk/Eyes-Japan
   Copyright (c) 2013, Lionel Bergeret and Rob Oudendijk
   Copyright (c) 2012, Lionel Bergeret
   Copyright (c) 2011, Robin Scheibler aka FakuFaku, Christopher Wang aka Akiba
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Contact rob@yr-design.biz


History:
  orignal bGeigieNano files modified for use with temperature and humidity sensor (SHT1) 
  and MICS 6814 (CO,NOX, NH3 sensor)

2017-06-27 V2.0.3 added ascii only version to reduce mememory.

*/

#include <limits.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include "TinyGPS.h"
#include "NanoSetup.h"
#include "NanoConfig.h"
#include "NanoDebug.h"
#include "SSD1306Ascii.h"
#include "SSD1306AsciiSoftSpi.h"


// OLED settings --------------------------------------------------------------
SSD1306AsciiSoftSpi display;


// Geiger settings ------------------------------------------------------------
#define LINE_SZ 135
#define BUFFER_SZ 12
#define STRBUFFER_SZ 32
#define AVAILABLE 'A'  // indicates geiger data are ready (available)
#define VOID      'V'  // indicates geiger data not ready (void)
#define DEFAULT_YEAR 2013
#define NX 12
#define TIME_INTERVAL 5000
#define IS_READY (1)
#define IS_READY (interruptCounterAvailable())

// For distance computation ----------------------------------------------------
bool gps_fix_first = true;
float gps_last_lon = 0, gps_last_lat = 0;
unsigned long int gps_distance = 0;

// Pulse counter --------------------------------------------------------------
#include "InterruptCounter.h"

// log file headers
#define LOGFILE_HEADER "# NEW LOG\n# format="
char logfile_name[13];  // placeholder for filename
bool logfile_ready = false;

// geiger statistics ----------------------------------------------------
unsigned long shift_reg[NX] = {0};
unsigned long reg_index = 0;
unsigned long total_count = 0;
unsigned long max_count = 0;
unsigned long uptime = 0;
int uphour = 0;
int upminute = 0;
int str_count = 0;
int maxLength_over_k = 3;
char geiger_status = VOID;
int CO_read=10;
int NOX_read=20;
int NH3_read=10;
int PPM25_read=34;
int  UV_read=5;
float  pollen_read=3.5;

// the line buffer for serial receive and send --------------------------
static char line[LINE_SZ];
static char strbuffer[STRBUFFER_SZ];
static char strbuffer1[STRBUFFER_SZ];


// OpenLog settings -----------------------------------------------------------
#define OPENLOG_RETRY 200
SoftwareSerial OpenLog(OPENLOG_RX_PIN, OPENLOG_TX_PIN);
static const int resetOpenLog = OPENLOG_RST_PIN;
bool openlog_ready = false;

// Gps settings ------------------------------------------------------------
TinyGPS gps(true);
#define GPS_INTERVAL 1000
char gps_status = VOID;

#if ENABLE_SOFTGPS
SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
#endif

// Gps data buffers --------------------------------------------------------------
static char lat[BUFFER_SZ];
static char lon[BUFFER_SZ];

// MTK33x9 chipset ---------------------------------------------------------------
#define PMTK_SET_NMEA_UPDATE_1HZ "$PMTK220,1000*1F"
#define PMTK_SET_NMEA_OUTPUT_ALLDATA "$PMTK314,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0*28"
#define PMTK_SET_NMEA_OUTPUT_RMCGGA "$PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*28"
#define PMTK_HOT_START "$PMTK101*32"
#define PMTK_COLD_START "$PMTK104*37"
#define SBAS_ENABLE "$PMTK313,1*2E\r\n"
#define DGPS_WAAS_ON "$PMTK301,2*2E\r\n"


// Function definitions ---------------------------------------------------------
// Atmel Tips and Tricks: 3.6 Tip #6 – Access types: Static
static unsigned long cpm_gen();
static bool gps_gen_filename(TinyGPS &gps, char *buf);
static bool gps_gen_timestamp(TinyGPS &gps, char *buf, unsigned long counts, unsigned long cpm, unsigned long cpb);
static char checksum(char *s, int N);
#if ENABLE_OPENLOG
static void setupOpenLog();
static bool loadConfig(char *fileName);
static void createFile(char *fileName);
#endif
static void gps_program_settings();
static float read_voltage(int pin);
static int availableMemory();
static unsigned long elapsedTime(unsigned long startTime);


// Nano Settings --------------------------------------------------------------
static ConfigType config;
static DoseType dose;
#if ENABLE_OPENLOG
NanoSetup nanoSetup(OpenLog, config, dose, line, LINE_SZ);
#endif


// SHT1 (sensors temp/humidity settings ---------------------------------------
 #if ENABLE_SHT1
   #include "SHT1x.h"
   SHT1x SHT1x(dataPin, clockPin);
   int temperature_read=-40;
   int humidity_read=-10;
 #endif

// setup gasses variables
    float c=2;

 enum{CO, NO2, NH3, C3H8, C4H10, CH4, H2, C2H5OH};

    float measure_CO(){return calcGas(CO);}
    float measure_NO2(){return calcGas(NO2);}
    float measure_NH3(){return calcGas(NH3);}
    float measure_C3H8(){return calcGas(C3H8);}
    float measure_C4H10(){return calcGas(C4H10);}
    float measure_CH4(){return calcGas(CH4);}
    float measure_H2(){return calcGas(H2);}
    float measure_C2H5OH(){return calcGas(C2H5OH);}






// ****************************************************************************
// Setup
// ****************************************************************************
void setup()
{

#ifdef GPS_LED_PIN
  pinMode(GPS_LED_PIN, OUTPUT);
#endif
#ifdef LOGALARM_LED_PIN
  pinMode(LOGALARM_LED_PIN, OUTPUT);
#endif
  pinMode(GEIGIE_TYPE_PIN, INPUT);
  Serial.begin(9600);


#if ENABLE_OPENLOG
  // Load EEPROM settings
  nanoSetup.initialize();

  OpenLog.begin(9600);
  setupOpenLog();
  if (openlog_ready) {
    nanoSetup.loadFromFile("SAFECAST.TXT");
  }
#endif


  // Create pulse counter
  interruptCounterSetup(INTERRUPT_COUNTER_PIN, TIME_INTERVAL);

  // And now Start the Pulse Counter!
  interruptCounterReset();

  gpsSerial.begin(9600);

  // Put GPS serial in listen mode----------------------------------------------------
  gpsSerial.listen();

  // initialize and program the GPS module--------------------------------------------
  gps_program_settings();



#if ENABLE_EEPROM_DOSE
  EEPROM_readAnything(BMRDD_EEPROM_DOSE, dose);
#endif

  // setup analog reference to read battery and boost voltage--------------------------
  analogReference(DEFAULT);

  //Display for SSD1306 setup ----------------------------------------------------
  display.begin(&Adafruit128x64, OLED_CS, OLED_DC, OLED_CLK, OLED_DATA, OLED_RESET);
  display.setFont(lcd5x7);

  // clear display
  display.clear();

  sprintf_P(strbuffer, PSTR("Geigie Nano %s"), NANO_VERSION);
  display.setCursor(0, 0);
  if (config.type == GEIGIE_TYPE_B) {
    display.print("b");
  } else {
    display.print("x");
  }
  display.print(strbuffer);

  display.setCursor(0, 1);
  int battery = ((read_voltage(VOLTAGE_PIN)-3.5)*166);
  // battery = (battery + 20);
  if (battery < 0) battery = 1;
  if (battery > 100) battery = 100;
  sprintf_P(strbuffer, PSTR("Battery= %02d"), battery);
  display.print(strbuffer);
  sprintf_P(strbuffer, PSTR("%%"));
  display.print(strbuffer);

  display.setCursor(0, 2);
  sprintf_P(strbuffer, PSTR("User= #%04d"), config.device_id);
  display.print(strbuffer);

  temperature_read = int(SHT1x.readTemperatureC());
  humidity_read = int(SHT1x.readHumidity()); 
  display.setCursor(0, 3);
  sprintf_P(strbuffer, PSTR("Temp=%d"), temperature_read);
  display.print(strbuffer);
    sprintf_P(strbuffer, PSTR("C"));
  display.print(strbuffer);
    
  display.setCursor(52, 3);
  sprintf_P(strbuffer, PSTR("Humid=%d"), humidity_read);
  display.print(strbuffer);
  sprintf_P(strbuffer, PSTR("%%"));
  display.print(strbuffer);

  // display.setCursor(0, 4);
  // display.print("CO=");
  // dtostrf((float)(analogRead(CO_Pin)), 3, 2, strbuffer);
  // display.print(strbuffer);
  
  // display.setCursor(35, 4);
  // display.print("NO=");
  // dtostrf((float)(analogRead(NOX_Pin)), 3, 2, strbuffer);
  // display.print(strbuffer);
  
  // display.setCursor(85, 4);
  // display.print("NH=");
  // dtostrf((float)(analogRead(NH3_Pin)), 3, 2, strbuffer);
  // display.print(strbuffer);

  display.setCursor(0, 7);
  display.print("EJ/Safecast 2018");

  delay(3000);
  display.clear();

}



// ****************************************************************************
// Main loop
// ****************************************************************************
void loop()
{
  bool gpsReady = false;

#if ENABLE_GEIGIE_SWITCH
  // Check geigie mode switch
  if (analogRead(GEIGIE_TYPE_PIN) > GEIGIE_TYPE_THRESHOLD) {
    config.type = GEIGIE_TYPE_B; // XGeigie;
  } else {
    config.type = GEIGIE_TYPE_X; // BGeigie
    digitalWrite(LOGALARM_LED_PIN, LOW);
  }
#endif

#if ENABLE_GEIGIE_SWITCH
  //Switch to bGeigie Xmode on low battery
int battery = ((read_voltage(VOLTAGE_PIN)-3.5)*125);
  if (battery < 20)  {
    delay(1000);
    config.type = GEIGIE_TYPE_X; // BGeigie
  }
#endif

#if ENABLE_SOFTGPS
  // Put GPS serial in listen mode
  gpsSerial.listen();
#endif

  // For GPS_INTERVAL we work on parsing GPS sentences
  for (unsigned long start = millis(); (elapsedTime(start) < GPS_INTERVAL) and !IS_READY;)
  {

#if ENABLE_SOFTGPS
    while (gpsSerial.available())
    {
      char c = gpsSerial.read();
#else
    while (Serial.available())
    {
      char c = Serial.read();
#endif

#if ENABLE_GPS_NMEA_LOG
      Serial.print(c); // uncomment this line if you want to see the GPS data flowing
#endif
      if (gps.encode(c)) // Did a new valid sentence come in?
        gpsReady = true;
    }
  }


#ifdef GPS_LED_PIN
  if ((gpsReady) || (gps_status == AVAILABLE)) {
    // digitalWrite(GPS_LED_PIN, HIGH);
  } else {
    // digitalWrite(GPS_LED_PIN, LOW);
  }
#endif

  // generate CPM every TIME_INTERVAL seconds
  if IS_READY {
  unsigned long cpm = 0, cpb = 0;


  // obtain the count in the last bin
  cpb = interruptCounterCount();

    // reset the pulse counter
    interruptCounterReset();

    // insert count in sliding window and compute CPM
    shift_reg[reg_index] = cpb;     // put the count in the correct bin
    reg_index = (reg_index + 1) % NX; // increment register index
    cpm = cpm_gen();                // compute sum over all bins

    // update the total counter
    total_count += cpb;
    uptime += 5;

    // update max cpm
    if (cpm > max_count) max_count = cpm;

#if ENABLE_EEPROM_DOSE
    dose.total_count += cpb;
    dose.total_time += 5;
    if (dose.total_time % BMRDD_EEPROM_DOSE_WRITETIME == 0) {
      EEPROM_writeAnything(BMRDD_EEPROM_DOSE, dose);
    }
#endif

    // set status of Geiger
    if (str_count < NX)
    {
      geiger_status = VOID;
      str_count++;
    } else if (cpm == 0) {
      geiger_status = VOID;
    } else {
      geiger_status = AVAILABLE;
    }

#if ENABLE_WAIT_GPS_FOR_LOG
    if ((!logfile_ready) && (gps_status == AVAILABLE))
#else
    if (!logfile_ready)
#endif
    {
      if (gps_gen_filename(gps, logfile_name)) {
        logfile_ready = true;

#if ENABLE_OPENLOG
#ifdef LOGALARM_LED_PIN
        //digitalWrite(LOGALARM_LED_PIN, HIGH);
#endif
        createFile(logfile_name);
        // print header to serial
        sprintf_P(strbuffer, PSTR(LOGFILE_HEADER));
        OpenLog.print(strbuffer);
        DEBUG_PRINT(strbuffer);
        sprintf_P(strbuffer, PSTR(NANO_VERSION));
        OpenLog.print(strbuffer);
        DEBUG_PRINT(strbuffer);

#ifdef ENABLE_LND_DEADTIME
        sprintf_P(strbuffer, PSTR("nano\n# deadtime=on\n"));
#else
        sprintf_P(strbuffer, PSTR("nano\n"));
#endif
        OpenLog.print(strbuffer);

#endif // ENABLE_OPENLOG
      }
    }

    // generate timestamp. only update the start time if
    // we printed the timestamp. otherwise, the GPS is still
    // updating so wait until its finished and generate timestamp
    memset(line, 0, LINE_SZ);
    gps_gen_timestamp(gps, line, shift_reg[reg_index], cpm, cpb);

    // Printout line
    Serial.println(line);

#if ENABLE_OPENLOG
    if ((logfile_ready) && (GEIGIE_TYPE_B == config.type)) {
#ifdef LOGALARM_LED_PIN
      //digitalWrite(LOGALARM_LED_PIN, HIGH);
#endif
      // Put OpenLog serial in listen mode
      OpenLog.listen();
      OpenLog.println(line);

#if ENABLE_DIAGNOSTIC
      dtostrf(read_voltage(VOLTAGE_PIN), 0, 1, strbuffer);
      OpenLog.print("$DIAG,");
      OpenLog.println(strbuffer);
#endif
    }
#ifdef LOGALARM_LED_PIN
    //digitalWrite(LOGALARM_LED_PIN, LOW);
#endif
#endif
  }
}

// ****************************************************************************
// Utility functions
// ****************************************************************************

/* calculate elapsed time. this takes into account rollover */
unsigned long elapsedTime(unsigned long startTime) {
  unsigned long stopTime = millis();

  if (startTime >= stopTime) {
    return startTime - stopTime;
  } else {
    return (ULONG_MAX - (startTime - stopTime));
  }
}

#if ENABLE_OPENLOG
/* wait for openlog prompt */
bool waitOpenLog(bool commandMode) {
  int safeguard = 0;
  bool result = false;

  while (safeguard < OPENLOG_RETRY) {
    safeguard++;
    if (OpenLog.available())
      if (OpenLog.read() == (commandMode ? '>' : '<')) break;
    delay(10);
  }

  if (safeguard >= OPENLOG_RETRY) {
  } else {
    result = true;
  }

  return result;
}

/* setups up the software serial, resets OpenLog */
void setupOpenLog() {
  pinMode(resetOpenLog, OUTPUT);
  OpenLog.listen();

  // reset OpenLog
  digitalWrite(resetOpenLog, LOW);
  delay(100);
  digitalWrite(resetOpenLog, HIGH);

  if (!waitOpenLog(true)) {
    logfile_ready = true;
  } else {
    openlog_ready = true;
  }
}

/* create a new file */
void createFile(char *fileName) {
  int result = 0;
  int safeguard = 0;

  OpenLog.listen();

  do {
    result = 0;

    do {
      OpenLog.print("append ");
      OpenLog.print(fileName);
      OpenLog.write(13); //This is \r

      if (!waitOpenLog(false)) {
        break;
      }
      result = 1;
    } while (0);

    if (0 == result) {
      // reset OpenLog
      digitalWrite(resetOpenLog, LOW);
      delay(100);
      digitalWrite(resetOpenLog, HIGH);

      // Wait for OpenLog to return to waiting for a command
      waitOpenLog(true);
    }
  } while (0 == result);

  //OpenLog is now waiting for characters and will record them to the new file
}
#endif

/* compute check sum of N bytes in array s */
char checksum(char *s, int N)
{
  int i = 0;
  char chk = s[0];

  for (i = 1 ; i < N ; i++)
    chk ^= s[i];

  return chk;
}

/* compute cpm */
unsigned long cpm_gen()
{
  unsigned int i;
  unsigned long c_p_m = 0;

  // sum up
  for (i = 0 ; i < NX ; i++)
    c_p_m += shift_reg[i];

#ifdef ENABLE_LND_DEADTIME
  //  deadtime compensation (medcom international)
  c_p_m = (unsigned long)((float)c_p_m / (1 - (((float)c_p_m * 1.8833e-6))));
#endif
  return c_p_m;
}

/* generate log filename */
bool gps_gen_filename(TinyGPS &gps, char *buf) {
  int year = DEFAULT_YEAR;
  byte month = 0, day = 0, hour = 0, minute = 0, second = 0, hundredths = 0;
  unsigned long age;

  gps.crack_datetime(&year, &month, &day, &hour, &minute, &second, &hundredths, &age);
  if (TinyGPS::GPS_INVALID_AGE == age) {
    return false;
  }

  // Create the filename for that drive
  sprintf_P(buf, PSTR("%04d%02d%02d.log"), config.device_id, month, day);

  return true;
}

/* convert long integer from TinyGPS to string "xxxxx.xxxx" */
void get_coordinate_string(bool is_latitude, unsigned long val, char *buf)
{
  unsigned long left = 0;
  unsigned long right = 0;

  left = val / 100000;
  right = (val - left * 100000) / 10;
  if (is_latitude) {
    sprintf_P(buf, PSTR("%04ld.%04ld"), left, right);
  } else {
    sprintf_P(buf, PSTR("%05ld.%04ld"), left, right);
  }
}

/* convert long integer from TinyGPS to float WGS84 degrees */
float get_wgs84_coordinate(unsigned long val)
{
  double result = 0.0;
  result = val / 10000000.0;
  result = ((result - (int)result) / 60.0) * 100 + (int)result;
  return (float)result;
}

/* render measurement in big digit on display */
void render_measurement(unsigned long value, bool is_cpm, int offset)
{
  display.setCursor(0, 0);
  display.set2X();
  if (VOID == geiger_status) {
//    display.setTextColor(BLACK, WHITE); // 'inverted' text
  } else {
//    display.setTextColor(WHITE);
  }

  // Cleanup temp buffer
  memset(strbuffer1, 0, sizeof(strbuffer1));

  // display in CPM
  if (is_cpm) {
    if (value >= 10000) {
      dtostrf((float)(value / 1000.0), 4, 3, strbuffer);
      strncpy (strbuffer1, strbuffer, 4);
      if (strbuffer1[strlen(strbuffer1) - 1] == '.') {
        strbuffer1[strlen(strbuffer1) - 1] = 0;
      }
      display.print(strbuffer1);
      display.set1X();
      sprintf_P(strbuffer, PSTR("kCPM"));
      display.print(strbuffer);
    } else {
      dtostrf((float)value, 0, 0, strbuffer);
      display.print(strbuffer);
      display.set1X();
      sprintf_P(strbuffer, PSTR(" CPM"));
      display.print(strbuffer);
    }
  } else {
    // display in Sievert/h
    if ((value / config.cpm_factor) >= 1000) {
      dtostrf((float)(value / config.cpm_factor / 1000.0), 4, 2, strbuffer);
      strncpy (strbuffer1, strbuffer, 5);
      if (strbuffer1[strlen(strbuffer1) - 1] == '.') {
        strbuffer1[strlen(strbuffer1) - 1] = 0;
      }
      display.print(strbuffer1);
      display.set1X();
      sprintf_P(strbuffer, PSTR(" mS/h"));
      display.print(strbuffer);
    } else if ((value / config.cpm_factor) >= 10) {
      dtostrf((float)(value / config.cpm_factor / 1.0), 4, 2, strbuffer);
      strncpy (strbuffer1, strbuffer, 5);
      if (strbuffer1[strlen(strbuffer1) - 1] == '.') {
        strbuffer1[strlen(strbuffer1) - 1] = 0;
      }
      display.print(strbuffer1);
      display.set1X();
      sprintf_P(strbuffer, PSTR(" uS/h"));
      display.print(strbuffer);
    } else {
      dtostrf((float)(value / config.cpm_factor / 1.0), 4, 3, strbuffer);
      strncpy (strbuffer1, strbuffer, 6);
      if (strbuffer1[strlen(strbuffer1) - 1] == '.') {
        strbuffer1[strlen(strbuffer1) - 1] = 0;
      }
      display.print(strbuffer1);
      display.set1X();
      sprintf_P(strbuffer, PSTR(" uS/h"));
      display.print(strbuffer);
    }
  }
}
/* generate log result line */
bool gps_gen_timestamp(TinyGPS &gps, char *buf, unsigned long counts, unsigned long cpm, unsigned long cpb)
{
  int year = DEFAULT_YEAR;
  byte month = 0, day = 0, hour = 0, minute = 0, second = 0, hundredths = 0;
  long int x = 0, y = 0;
  float faltitude = 0, fspeed = 0;
  unsigned short nbsat = 0;
  unsigned long precission = 0;
  unsigned long age;
  byte len, chk;
  char NS = 'N';
  char WE = 'E';
  static int toggle = 0;

  memset(lat, 0, BUFFER_SZ);
  memset(lon, 0, BUFFER_SZ);
  memset(strbuffer, 0, STRBUFFER_SZ);

  // get GPS date
  gps.crack_datetime(&year, &month, &day, &hour, &minute, &second, &hundredths, &age);
  if (TinyGPS::GPS_INVALID_AGE == age) {
    year = 2012, month = 0, day = 0, hour = 0, minute = 0, second = 0, hundredths = 0;
  }

  // get GPS position, altitude and speed
  gps.get_position(&x, &y, &age);
  if (!gps.status()) {
    gps_status = VOID;
  } else {
    gps_status = AVAILABLE;
  }
  faltitude = gps.f_altitude();
  fspeed = gps.f_speed_kmph();
  nbsat = gps.satellites();
  precission = gps.hdop();

  if (x < 0) { NS = 'S'; x = -x;}
  if (y < 0) { WE = 'W'; y = -y;}
  get_coordinate_string(true, x == TinyGPS::GPS_INVALID_ANGLE ? 0 : x, lat);
  get_coordinate_string(false, y == TinyGPS::GPS_INVALID_ANGLE ? 0 : y, lon);
  dtostrf(faltitude == TinyGPS::GPS_INVALID_F_ALTITUDE ? 0.0 : faltitude, 0, 2, strbuffer);

#if ENABLE_100M_TRUNCATION
  truncate_100m(lat, lon);
#endif



  #if ENABLE_SHT1
    temperature_read = int(SHT1x.readTemperatureC());
    humidity_read = int(SHT1x.readHumidity());
    #else
     int temperature_read= -40;
       int humidity_read=0;
  #endif

// reading from sensors

  NH3_read= (analogRead(NH3_Pin));

  calcGas(CO);
  CO_read=c ;


  // prepare the log entry
  memset(buf, 0, LINE_SZ);
  sprintf_P(buf, PSTR("$%s,%04d,%02d-%02d-%02dT%02d:%02d:%02dZ,%ld,%ld,%ld,%c,%s,%c,%s,%c,%s,%c,%d,%ld"),  \
            NANO_HEADER, \
            config.device_id, \
            year, month, day,  \
            hour, minute, second, \
            cpm, \
            cpb, \
            total_count, \
            geiger_status, \
            lat, NS, \
            lon, WE, \
            strbuffer, \
            gps_status, \
            nbsat  == TinyGPS::GPS_INVALID_SATELLITES ? 0 : nbsat, \
            precission == TinyGPS::GPS_INVALID_HDOP ? 0 : precission);

  len = strlen(buf);
  buf[len] = '\0';

  // generate checksum
  chk = checksum(buf + 1, len);


//quick fix to get NOX data in the string
  dtostrf((float)(analogRead(NOX_Pin)), 4, 0, strbuffer);
  dtostrf((float)pollen_read, 2, 1, strbuffer1);


    sprintf_P(buf + len, PSTR("*%X%s$%s,%04d,%d,%d,%d,%s,%d,%d,%d,%d"), 
              (int)chk, \
              "\n", \
              NANO_HEADER_SENSOR, \
              config.device_id, \
              temperature_read, \
              humidity_read,\
              CO_read, \
              strbuffer, \
              NH3_read, \
              PPM25_read, \ 
              UV_read, \
              strbuffer1);


#if ENABLE_SSD1306
  // compute distance
  if (gps.status()) {
    int trigger_dist = 25;
    float flat = get_wgs84_coordinate(x);
    float flon = get_wgs84_coordinate(y);

    if (fspeed > 5)
      // fpspeed/3.6 * 5s = 6.94 m
      trigger_dist = 5;
    if (fspeed > 10)
      trigger_dist = 10;
    if (fspeed > 15)
      trigger_dist = 20;

    if (gps_fix_first)
    {
      gps_last_lat = flat;
      gps_last_lon = flon;
      gps_fix_first = false;
    }
    else
    {
      // Distance in meters
      unsigned long int dist = (long int)TinyGPS::distance_between(flat, flon, gps_last_lat, gps_last_lon);

      if (dist > trigger_dist)
      {
        gps_distance += dist;
        gps_last_lat = flat;
        gps_last_lon = flon;
      }
    }
  }

// ready to display the data on screen
//  display.clearDisplay();
  display.clear();
  int offset = 0;

  if (config.type == GEIGIE_TYPE_B) {
    // **********************************************************************
    // bGeigie mode
    // **********************************************************************
    // Display uptime
    uphour = uptime / 3600;
    upminute = uptime / 60 - uphour * 60;
    sprintf_P(strbuffer, PSTR("%02dh%02dm"), uphour, upminute);
    display.setCursor(92, 2);
    display.set1X();
//    display.setTextColor(WHITE);
    display.println(strbuffer);

    //Display Alarm LED if GPS is locked and Radiation is valid
#ifdef LOGALARM_LED_PIN
    if ((geiger_status == AVAILABLE) && (gps.status())) {
      if (openlog_ready) {
        digitalWrite(LOGALARM_LED_PIN, HIGH);
      } else {
        digitalWrite(LOGALARM_LED_PIN, LOW);
      }
    } else {
      digitalWrite(LOGALARM_LED_PIN, LOW);
    }
#endif

    // Display CPM (with deadtime compensation)
    render_measurement(cpm, true, offset);

    // Display SD, GPS and Geiger states
    display.set1X();
    if (!gps.status()) {
      display.setCursor(92, 1);
      sprintf_P(strbuffer, PSTR("No GPS"));
      display.println(strbuffer);
    } else {
      display.setCursor(110, 1);
      sprintf(strbuffer, "%2d", nbsat);
      display.print(strbuffer);
      sprintf_P(strbuffer, PSTR("^"));
      display.println(strbuffer);

    }

    // Display uSv/h
    display.setCursor(0, 2); // textsize*8
    if (config.mode == GEIGIE_MODE_USVH) {
      dtostrf((float)(cpm / config.cpm_factor), 0, 3, strbuffer);
      display.print(strbuffer);
      sprintf_P(strbuffer, PSTR(" uSv/h"));
      display.println(strbuffer);
    }
    else if (config.mode == GEIGIE_MODE_BQM2) {
      dtostrf((float)(cpm * config.bqm_factor), 0, 3, strbuffer);
      display.print(strbuffer);
      sprintf_P(strbuffer, PSTR(" Bq/m2"));
      display.println(strbuffer);
    }

    if (toggle) {
      // Display distance
      dtostrf((float)(gps_distance / 1000.0), 0, 1, strbuffer);
      // display.setCursor(116-(strlen(strbuffer)*6), offset+16); // textsize*8
      display.setCursor(116 - (strlen(strbuffer) * 6), 3); // textsize*8
      display.print(strbuffer);
      sprintf_P(strbuffer, PSTR("km"));
      display.println(strbuffer);
    } else {
      // Display altidude
      if (gps.status()) {
        dtostrf(faltitude, 0, 0, strbuffer);
      } else {
        sprintf_P(strbuffer, PSTR("--"));
      }
      display.setCursor(122 - (strlen(strbuffer) * 6), 3); // textsize*8
      display.print(strbuffer);
      display.println("m");
    }
  }
  else if (config.type == GEIGIE_TYPE_X) {
    // **********************************************************************
    // xGeigie mode
    // **********************************************************************
    // LED Log/alarm set for alarm
    digitalWrite(LOGALARM_LED_PIN, LOW);
    if (cpm > config.alarm_level) {
      digitalWrite(LOGALARM_LED_PIN, HIGH);
    } else {
      digitalWrite(LOGALARM_LED_PIN, LOW);
    }

    // Display uSv/h
    render_measurement(cpm, false, offset);

    // Cleanup temp buffer
    memset(strbuffer1, 0, sizeof(strbuffer1));

    display.setCursor(0, 2);
    display.set1X();
    if (toggle) {
      int battery = ((read_voltage(VOLTAGE_PIN)-3.5)*166);
      if (battery < 20) {
        display.print("BATTERY LOW.NO LOGGER");
      } else {
        // Display CPM
        if (cpm > 1000) {
          dtostrf((float)(cpm / 1000.00), 0, 1, strbuffer);
          strncpy (strbuffer1, strbuffer, 5);
          if (strbuffer1[strlen(strbuffer1) - 1] == '.') {
            strbuffer1[strlen(strbuffer1) - 1] = 0;
          }
          display.print(strbuffer1);
          sprintf_P(strbuffer, PSTR("kCPM "));
          display.print(strbuffer);
        } else {
          dtostrf((float)cpm, 0, 0, strbuffer);
          display.print(strbuffer);
          sprintf_P(strbuffer, PSTR("CPM "));
          display.print(strbuffer);
        }

        // Display bq/m2
        if ((cpm * config.bqm_factor) > 1000000) {
          dtostrf((float)(cpm * config.bqm_factor / 1000000.0), 0, 1, strbuffer);
          strncpy (strbuffer1, strbuffer, 5);
          display.print(strbuffer1);
          sprintf_P(strbuffer, PSTR("mBq/m2"));
          display.print(strbuffer);

        } else {
          if ((cpm * config.bqm_factor) > 10000) {
            dtostrf((float)(cpm * config.bqm_factor / 1000.0), 0, 0, strbuffer);
            strncpy (strbuffer1, strbuffer, 5);
            display.print(strbuffer1);
            sprintf_P(strbuffer, PSTR("kBq/m2"));
            display.print(strbuffer);
          } else {
            dtostrf((float)(cpm * config.bqm_factor), 0, 0, strbuffer);
            display.print(strbuffer);
            sprintf_P(strbuffer, PSTR("Bq/m2"));
            display.print(strbuffer);
          }
        }
      }
    } else {
      int battery = ((read_voltage(VOLTAGE_PIN)-3.5)*166);
      if (battery < 20 ) {
        display.print("BATTERY LOW.NO LOGGER");
      } else {
        // Total dose and max count
        sprintf_P(strbuffer, PSTR("Mx="));
        display.print(strbuffer);
        dtostrf((float)(max_count / config.cpm_factor), 0, 1, strbuffer);
        display.print(strbuffer);
        sprintf_P(strbuffer, PSTR("uS/h "));
        display.print(strbuffer);
        sprintf_P(strbuffer, PSTR("Ds="));
        display.print(strbuffer);
        dtostrf((float)( ((dose.total_count / (dose.total_time / 60.0)) / config.cpm_factor) * (dose.total_time / 3600.0) ), 0, 0, strbuffer);
        display.print(strbuffer);
        sprintf_P(strbuffer, PSTR("uS"));
        display.print(strbuffer);
      }
    }
  } else {
    // Wrong mode
    display.setCursor(0, 5);
    sprintf_P(strbuffer, PSTR("Wrong mode !"));
    display.print(strbuffer);
  }


  // **********************************************************************
  // Common display parts
  // **********************************************************************
  if (openlog_ready) {
    // Display date
    sprintf_P(strbuffer, PSTR("%02d/%02d %02d:%02d:%02d"),  \
              day, month, \
              hour, minute, second);
    display.setCursor(0, 3); // textsize*8
    display.println(strbuffer);
  } else {
    display.setCursor(0, 3); // textsize*8
    sprintf_P(strbuffer, PSTR("NO SD CARD/ GPS reset"));
    display.print(strbuffer);

    //reset GPS
    digitalWrite(LOGALARM_LED_PIN, LOW);
    memset(line, 0, LINE_SZ);
    sprintf_P(line, PSTR(PMTK_COLD_START));
    gpsSerial.println(line);
  }


  // Display battery indicator
  // Range = [3.5v to 4.3v]
  display.setCursor(105, 0);
  int battery = ((read_voltage(VOLTAGE_PIN)-3.5)*166);
  if (battery < 0) battery = 1;
  if (battery > 100) battery = 100;
  sprintf_P(strbuffer, PSTR("%02d"), battery);
  display.print(strbuffer);
  display.print("%");



// display sensor data


  //Temperature
  display.setCursor(0, 4);
  sprintf_P(strbuffer, PSTR("Temp=%d"), temperature_read);
  display.print(strbuffer);
  sprintf_P(strbuffer, PSTR("C"));
  display.print(strbuffer);

  //Humidity
  display.setCursor(52, 4);
  sprintf_P(strbuffer, PSTR("Humid=%d"), humidity_read);
  display.print(strbuffer);
  sprintf_P(strbuffer, PSTR("%%"));
  display.print(strbuffer);

  //CO
  display.setCursor(0, 5);
  display.print("CO=");
  measure_CO();
  dtostrf(((float)(c)), 4, 0, strbuffer);
  display.print(strbuffer);

  //NOX
  display.setCursor(0, 6);
  display.print("NOX=");
  // calcGas(NO2);
  // NOX_read=c ;
  // display.print(c);
   // dtostrf((float)(analogRead(NOX_read)), 3, 0, strbuffer);
   display.print(analogRead(NOX_Pin));


  //C3H8
  display.setCursor(54, 5);
  display.print("C3H8=0");
  // dtostrf(((float)(pow(analogRead(CO_Pin), -1.179)*4.385)), 4, 0, strbuffer);
  // display.print(strbuffer);


  //H2
  display.setCursor(54, 6);
  display.print("H2=0");
  // dtostrf((float)(analogRead(NOX_Pin)), 4, 0, strbuffer);
  // display.print(strbuffer);


  if (toggle) {

  //NH3
  display.setCursor(0, 7);
  display.print("NH3=");
  // dtostrf((float)(analogRead(NH3_read)), 3, 0, strbuffer);
  display.print(analogRead(NH3_Pin));

  //C2H5O
  display.setCursor(54, 7);
  display.print("C2H5OH=0");
  // dtostrf((float)(analogRead(NH3_Pin)), 4, 0, strbuffer);
  // display.print(strbuffer);
  } else {

  display.setCursor(0, 7);
  display.print("C4H10=0");
  // // dtostrf((float)(analogRead(NH3_Pin)), 4, 0, strbuffer);
  // display.print(strbuffer);

  display.setCursor(84, 7);
  display.print("EJ 2018");
  }


#endif

  // Display items toggling
  toggle ^= 1;

  return (gps_status == AVAILABLE);
}

/* setup the GPS module to 1Hz and RMC+GGA messages only */
void gps_program_settings()
{
#if ENABLE_MEDIATEK
  memset(line, 0, LINE_SZ);
  sprintf_P(line, PSTR(PMTK_SET_NMEA_OUTPUT_RMCGGA));
  gpsSerial.println(line);

  memset(line, 0, LINE_SZ);
  sprintf_P(line, PSTR(PMTK_SET_NMEA_UPDATE_1HZ));
  gpsSerial.println(line);

  memset(line, 0, LINE_SZ);
  sprintf_P(line, PSTR(SBAS_ENABLE));
  gpsSerial.println(line);

  memset(line, 0, LINE_SZ);
  sprintf_P(line, PSTR(DGPS_WAAS_ON));
  gpsSerial.println(line);
#endif

#if ENABLE_SKYTRAQ
  // all GPS command taken from datasheet
  // "Binary Messages Of SkyTraq Venus 6 GPS Receiver"

  // set GGA and RMC output at 1Hz
  uint8_t GPS_MSG_OUTPUT_GGARMC_1S[9] = { 0x08, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01 }; // with update to RAM and FLASH
  uint16_t GPS_MSG_OUTPUT_GGARMC_1S_L = 9;

  // Power Save mode (not sure what it is doing at the moment
  uint8_t GPS_MSG_PWR_SAVE[3] = { 0x0C, 0x01, 0x01 }; // update to FLASH too
  uint16_t GPS_MSG_PWR_SAVE_L = 3;

  // wait for GPS to start
  while (!gpsSerial.available())
    delay(10);

  // send all commands
  gps_send_message(GPS_MSG_OUTPUT_GGARMC_1S, GPS_MSG_OUTPUT_GGARMC_1S_L);
  gps_send_message(GPS_MSG_PWR_SAVE, GPS_MSG_PWR_SAVE_L);
#endif
}

void gps_send_message(const uint8_t *msg, uint16_t len)
{
  uint8_t chk = 0x0;
  // header
  gpsSerial.write(0xA0);
  gpsSerial.write(0xA1);
  // send length
  gpsSerial.write(len >> 8);
  gpsSerial.write(len & 0xff);
  // send message
  for (unsigned int i = 0 ; i < len ; i++)
  {
    gpsSerial.write(msg[i]);
    chk ^= msg[i];
  }
  // checksum
  gpsSerial.write(chk);
  // end of message
  gpsSerial.write(0x0D);
  gpsSerial.write(0x0A);
  gpsSerial.write('\n');
}

/* retrieve battery voltage */
  // Range = [3.5v to 4.17v] delta =0.67V = 100
  // reference = default 3.3V =1023
// to display % (voltage on measurepin *3.3 /1023/voltage_divider)-0.35*153 
float read_voltage(int pin)
{
  static float voltage_divider = (float)VOLTAGE_R2 / (VOLTAGE_R1 + VOLTAGE_R2);
  float result = (float)analogRead(pin) * 3.3/ 1023.0 / voltage_divider;

  return result;
}


/* setup gasses */

float calcGas(int gas)
{

    float ratio0, ratio1, ratio2, logPPM;

        // ratio0 = (float)res[0] / res0[0];
        // ratio1 = (float)res[1] / res0[1];
        // ratio2 = (float)res[2] / res0[2];


        ratio0 = (float)(analogRead(NH3_Pin));

        ratio1 = (float)(analogRead(CO_Pin));
        ratio2 = (float)(analogRead(NOX_Pin));

    switch(gas)
    {
        case CO:
        {

        // max CO =900 (Rs=1.5m and 220k pullup) on scale 0-1023 max co=1000ppm at 1.5m min CO=312 (Rs=100k and 220k) min co=1ppm at 100k scale is log base 10  

            // c = pow(ratio1, -1.179)*4.385;  //mod by jack
         c= pow(10,((ratio1-312)/588/1000*900)*log10(588));
          // Serial.print(ratio1);
            break;
        }
        case NO2:
        {
            c = pow(ratio2, 1.007)/6.855;  //mod by jack
            break;
        }
        case NH3:
        {
            c = pow(ratio0, -1.67)/1.47;  //modi by jack
            break;
        }
        case C3H8:  //add by jack
        {
            c = pow(ratio0, -2.518)*570.164;
            break;
        }
        case C4H10:  //add by jack
        {
            c = pow(ratio0, -2.138)*398.107;
            break;
        }
        case CH4:  //add by jack
        {
            c = pow(ratio1, -4.363)*630.957;
            break;
        }
        case H2:  //add by jack
        {
            c = pow(ratio1, -1.8)*0.73;
            break;
        }
        case C2H5OH:  //add by jack
        {
            c = pow(ratio1, -1.552)*1.622;
            break;
        }
        default:
            break;
    }
    
}



