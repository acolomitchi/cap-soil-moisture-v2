#include <LowPower.h>
#include <FreqCounter.h>
#include <EEPROM.h>


#define FPROBE_PWR_PIN 6
#define RELAY_PWR_PIN  3
#define HEARTHBEAT_PIN  13

#define FACTORY_SWITCHON_LEVEL       17000
#define FACTORY_SOAKING_SECS_DIV10   2880
#define FACTORY_WATERING_SECS        5

struct settings_struct {
  uint32_t switchOnLevel;
  uint16_t soakingSecsDiv10;
  uint16_t wateringSecs;
};

typedef struct settings_struct settings_t;

void sleepSecs(unsigned long numSeconds) {
  while(numSeconds>0) {
    period_t p=SLEEP_15MS;
    if(numSeconds>=8) {
      p=SLEEP_8S;
      numSeconds-=8;
    }
    else if(numSeconds>=4) {
      p=SLEEP_4S;
      numSeconds-=4;
    }
    else if(numSeconds>=2) {
      p=SLEEP_2S;
      numSeconds-=2;
    }
    else if(numSeconds) {
      p=SLEEP_1S;
      numSeconds--;
    }
    if(p!=SLEEP_15MS) {
      LowPower.powerDown(p, ADC_OFF, BOD_ON);
    }
  }
}

void settingsDump(const struct settings_struct& v) {
  Serial.print("Trhd:");
  Serial.println(v.switchOnLevel);
  Serial.print("Wsec:");
  Serial.println(v.wateringSecs);
  Serial.print("Soak:");
  Serial.println((uint32_t)(v.soakingSecsDiv10*10));  
}

unsigned long fProbe(bool report) {
  // switch on the power to the probe
  digitalWrite(FPROBE_PWR_PIN,1);
  digitalWrite(HEARTHBEAT_PIN,1);
  // let it really power on, approx 1 cycle @ 30kHz
  delayMicroseconds(333); 

  FreqCounter::f_comp = 0;
  FreqCounter::start(100);
  
  unsigned long frq=0;
  while(! FreqCounter::f_ready ) {
    frq=FreqCounter::f_freq;
  }
  if(report) {
    Serial.print("Count: ");
    Serial.println(frq);
  }
  // power off the probe
  digitalWrite(FPROBE_PWR_PIN,0);
  digitalWrite(HEARTHBEAT_PIN, 0);

  return frq;
}

#define READ_COUNT 8
double readFreq() {
  unsigned long sum=0;
  for(int i=0; i<READ_COUNT; i++) {
    sum+=fProbe(false);
    delay(500);
  }
  double frq=((double)sum)/READ_COUNT;
  return frq;
}

void switchPump(uint8_t mode) {
  digitalWrite(RELAY_PWR_PIN, mode);
  digitalWrite(HEARTHBEAT_PIN, mode);
}

void applyWatering(long secs) {
  switchPump(HIGH);
  for(int i=0; i<secs; i++) {
    delay(1000);
  }
  switchPump(LOW);
}

#define SETUP_NONE                ((uint8_t)0x00)
#define SETUP_FACTORY_DEFAULT     ((uint8_t)0x0F)
#define SETUP_WATERING_TIME       ((uint8_t)0x01)
#define SETUP_CURRENT_COUNT_LEVEL ((uint8_t)0x02)
#define SETUP_SOAKING_TIME_3M     ((uint8_t)0x03)
#define SETUP_SOAKING_TIME_1H     ((uint8_t)0x04)
#define SETUP_SOAKING_TIME_2H     ((uint8_t)0x05)
#define SETUP_SOAKING_TIME_3H     ((uint8_t)0x06)
#define SETUP_SOAKING_TIME_4H     ((uint8_t)0x07)
#define SETUP_SOAKING_TIME_6H     ((uint8_t)0x08)
#define SETUP_SOAKING_TIME_8H     ((uint8_t)0x09)
#define SETUP_SOAKING_TIME_12H    ((uint8_t)0x0A)
#define SETUP_CALIBRATION         ((uint8_t)0x0B)
#define SETUP_USB_IFACE           ((uint8_t)0x0C)
#define SETUP_RESERVED1           ((uint8_t)0x0D)
#define SETUP_RESERVED2           ((uint8_t)0x0E)

bool modeNeedsEepromWrite(uint8_t mode) {
  bool doesntNeedWrite= (
   (mode==SETUP_NONE) ||
   (mode==SETUP_CALIBRATION) ||
   (mode==SETUP_USB_IFACE) ||
   (mode==SETUP_RESERVED1) ||
   (mode==SETUP_RESERVED2)
  );
  return ! doesntNeedWrite;
}


struct settings_struct settings;

void wateringLoop() {
  // Serial.println("Watering loop");
  // settingsDump(settings);
  double frq=readFreq();
  // Serial.println(frq);
  if(frq>=settings.switchOnLevel) {
    applyWatering(settings.wateringSecs);
  }
  // wait for the water to soak in before taking another read
  uint32_t numSecs=settings.soakingSecsDiv10*5/4;
  sleepSecs(((uint32_t)settings.soakingSecsDiv10)*10);
}

void readAndReportLoop() {
  fProbe(true);
  delay(2000);
}

typedef void (*loopfunc_t)(void);

loopfunc_t execLoop=&wateringLoop;

#define READ_PIN_SETTINGS(ret) { \
  ret=0; \
  ret=(LOW==digitalRead(A3)) ? 1 : 0; \
  ret=(ret<<1) | (LOW==digitalRead(A2) ? 1 : 0); \
  ret=(ret<<1) | (LOW==digitalRead(A1) ? 1 : 0); \
  ret=(ret<<1) | (LOW==digitalRead(A0) ? 1 : 0); \
}

#define ULONG_MAX (unsigned long)((unsigned long)0 - 1)

// order: switchOnLevel, wateringSecs, soakingSecsDiv10
void readEepromSettings() {
  uint8_t addr=0;
  EEPROM.get(addr, settings);
  bool notInited=false;
  if( ! settings.switchOnLevel) {
    // not set, return factory default and write it
    settings.switchOnLevel=FACTORY_SWITCHON_LEVEL;
    notInited=true;
  }
  if( ! settings.wateringSecs ) {
    settings.wateringSecs=FACTORY_WATERING_SECS;
    notInited=true;
  }
  if( ! settings.soakingSecsDiv10 ) {
    settings.soakingSecsDiv10=FACTORY_SOAKING_SECS_DIV10;
    notInited=true;
  }
  if(notInited) {
    adjustEepromSettings();
  }
}

void adjustEepromSettings() {
  EEPROM.put(0, settings);
  settings_t readBack;
  EEPROM.get(0, readBack);
  Serial.println("Postadj");
  settingsDump(readBack);
}

void setupAdjust(bool report) {
  uint8_t mode=0;

  readEepromSettings();  

  pinMode(A0, INPUT_PULLUP);
  pinMode(A1, INPUT_PULLUP);
  pinMode(A2, INPUT_PULLUP);
  pinMode(A3, INPUT_PULLUP);
  READ_PIN_SETTINGS(mode);

  switch(mode) {
    case SETUP_FACTORY_DEFAULT:
      settings.switchOnLevel=FACTORY_SWITCHON_LEVEL;
      settings.soakingSecsDiv10=FACTORY_SOAKING_SECS_DIV10;
      settings.wateringSecs=FACTORY_WATERING_SECS;
      break;      
    case SETUP_WATERING_TIME: {
      // maintain watering until the setup mode changes
      // hopefuly, the change will come by simply pulling the Setup pin out
      uint8_t newMode=0; // retain the old mode for reporting
      // Serial.println("Entering watering time capture");
      unsigned long startMillis=millis();
      switchPump(HIGH);
      digitalWrite(HEARTHBEAT_PIN, HIGH);
      do {
        READ_PIN_SETTINGS(newMode);
        // Serial.print("New mode:");
        // Serial.println(newMode);
        delay(200);
      } while(newMode==SETUP_WATERING_TIME);
      switchPump(LOW);
      digitalWrite(HEARTHBEAT_PIN, LOW);
      unsigned long endMillis=millis();
      endMillis=
          (startMillis>endMillis)
        ? (ULONG_MAX-startMillis)+endMillis // rollover
        : endMillis - startMillis
      ;
      // Serial.print("new Watering secs:");
      Serial.println(endMillis/1000);
      settings.wateringSecs=endMillis/1000;
      break;
    }
    case SETUP_CURRENT_COUNT_LEVEL:
      delay(40000);
      settings.switchOnLevel=readFreq()-8;
      break;
    case SETUP_SOAKING_TIME_3M:
      // good for testing purposes if need arise
      settings.soakingSecsDiv10=18;
      break;
    case SETUP_SOAKING_TIME_1H:
      settings.soakingSecsDiv10=360;
      break;
    case SETUP_SOAKING_TIME_2H:
      settings.soakingSecsDiv10=720;
      break;
    case SETUP_SOAKING_TIME_3H:
      settings.soakingSecsDiv10=1080;
      break;
    case SETUP_SOAKING_TIME_4H:
      settings.soakingSecsDiv10=1440;
      break;
    case SETUP_SOAKING_TIME_6H:
      settings.soakingSecsDiv10=2160;
      break;
    case SETUP_SOAKING_TIME_8H:
      settings.soakingSecsDiv10=2880;
      break;
    case SETUP_SOAKING_TIME_12H:
      settings.soakingSecsDiv10=4320;
      break;
    case SETUP_CALIBRATION:
      execLoop=&readAndReportLoop;
      break;
    case SETUP_USB_IFACE:
    case SETUP_RESERVED1:
    case SETUP_RESERVED2:
    case SETUP_NONE:
    default:
      break;
  }
  if(modeNeedsEepromWrite(mode)) {
    adjustEepromSettings();
  }
  if(report) {
    if(modeNeedsEepromWrite) {
      Serial.println("Eeprom.");
    }
    settingsDump(settings);
  }
  // setup adjust done. 
  // No need to read the setup pins again until next reset
  pinMode(A0, OUTPUT);
  pinMode(A1, OUTPUT);
  pinMode(A2, OUTPUT);
  pinMode(A3, OUTPUT);
  digitalWrite(A0,LOW);
  digitalWrite(A1,LOW);
  digitalWrite(A2,LOW);
  digitalWrite(A3,LOW);
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);        // connect to the serial port
  pinMode(FPROBE_PWR_PIN, OUTPUT);
  pinMode(RELAY_PWR_PIN, OUTPUT);

  setupAdjust(true);

  digitalWrite(FPROBE_PWR_PIN, 0);
  digitalWrite(RELAY_PWR_PIN, 0);

  unsigned long testFreq=0;
  for(int i=0; i<READ_COUNT; i++) {
    testFreq+=fProbe(false);
    delay(100);
  }
  Serial.println(testFreq/READ_COUNT);

  // cycle pump for 5 secs
  applyWatering(5);
}


void loop() {
  execLoop();
}

