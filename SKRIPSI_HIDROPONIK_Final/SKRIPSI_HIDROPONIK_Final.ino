#include <Wire.h>
#include "RTClib.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_ADS1X15.h>
#include "thingProperties.h"

#define TdsPin 35
#define ONE_WIRE_BUS 32

// ================= POMPA NUTRISI =================
#define nA 26
#define nB 27

// ================= POMPA MIXING =================
#define pump 25

RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
Adafruit_ADS1115 ads;

// ================= STATE =================
enum State {
  MONITORING,
  DOSING,
  MIXING,
  WAITING,
  CHECKING,
  THRESHOLD
};

State currentState = MONITORING;
State prevState = MONITORING;

// ================= TANAM =================
int tanamTahun = 2026;
int tanamBulan = 6;
int tanamHari = 25;

// ================= DILUSI =================
float v1 = 0;
float v2 = 60;

float ca = 95000;
float cb = 176000;

float t = 0;

// ================= DEBIT POMPA =================
float debitA = 70.0; // ml/s
float debitB = 50.0; // ml/s

// ================= TDS =================
unsigned long lastBacaTds = 0;
unsigned long intervalBacaTds = 5000;

const float TDS_FACTOR   = 0.5;   // set ini agar mendekati meter komersial (0.5..0.7) – tweak saat kalibrasi
const float K_CELL       = 1.00;  // cell constant (~1.0), tweak saat kalibrasi EC
const float tdsCalFactor = 1.8;

float targetPpm = 0;
float tegangan = 0;
float avgRaw = 0;
float totalTds = 0;
float tdsValueFix = 0;
int numVoltageSamples = 32;

const uint8_t numBacaTds = 20;
float sampleTds[numBacaTds];
int countBacaTds = 0;

bool flagTdsValid = false;

// ================= TANDON =================
float volumeTandon = 10000;
float volumeMax = 10000;

// ================= RTC =================
int lastMinute = -1;

// ================= SUHU =================
unsigned long lastBacaSuhu = 0;
// float suhu = 0; // Ada di header

// ================= POMPA NUTRISI =================
unsigned long startPompaNutrisi = 0;

float durasiA = 0;
float durasiB = 0;

bool flagSudahTuangNutrisi = false;

int stepNutrisi = 0;

// ================= POMPA MIXING =================
unsigned long startPompaNyala = 0;
unsigned long durasiPompaNyala = 300000;

bool pompaSudahJalan = false;
bool pompaSedangNyala = false;
bool flagSudahMixing = false;

// ================= LCD =================
unsigned long lastLCD = 0;
const unsigned long intervalLCD = 2000;

// ================= SETUP =================
void setup() {

  Serial.begin(115200);

  Wire.begin(21, 22);

  initProperties();
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);

  sensors.begin();

  lcd.init();
  lcd.backlight();

  ads.begin();
  ads.setGain(GAIN_ONE);

  pinMode(nA, OUTPUT);
  pinMode(nB, OUTPUT);
  pinMode(pump, OUTPUT);

  digitalWrite(nA, LOW);
  digitalWrite(nB, LOW);
  digitalWrite(pump, LOW);

  rtc.begin();

  suhu = bacaSuhu();

  resetTdsSampling();
}

// ================= LCD =================
void tampilLcd() {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("TDS: ");
  lcd.print(tdsValueFix);

  lcd.setCursor(12, 0);
  lcd.print("U=");
  lcd.print(umurTanaman);

  lcd.setCursor(0, 1);
  lcd.print("Target: ");
  lcd.print(targetPpm);
}

// ================= UMUR TANAMAN =================
void tentukanUmurPpm(DateTime now) {

  DateTime tanam(tanamTahun, tanamBulan, tanamHari);

  int umur = (now - tanam).days();

  umurTanaman = umur;

  if (umur <= 14) {
    targetPpm = 750;
  }
  else if (umur <= 28) {
    targetPpm = 900;
  }
}

// ================= SUHU =================
float bacaSuhu() {

  sensors.requestTemperatures();

  return sensors.getTempCByIndex(0);
}

// && now.minute() % 2 == 0 && now.second() >= 2

// ================= TDS =================
float bacaTegangan() {
  long acc = 0;
  for (uint16_t i = 0; i < numVoltageSamples; i++) {
    int16_t raw = ads.readADC_SingleEnded(0); // A0
    acc += raw;
  }
  avgRaw = (float)acc / numVoltageSamples;
  return ads.computeVolts((int16_t)avgRaw);
}

float voltageToTDS(float v) {
  if (v < 0) v = 0;
  if (v > 3.3) v = 3.3;

  // kompensasi suhu (linear 2%/°C)
  float ec25 = v / (1.0f + 0.02f * (suhu - 25.0f));

  float ec_comp = (133.42f * ec25 * ec25 * ec25 - 255.86f * ec25 * ec25 + 857.39f * ec25) * K_CELL; // mS/cm

  float ppm = ec_comp * TDS_FACTOR * tdsCalFactor; // ppm
  if (ppm < 0) ppm = 0;
  return ppm;
}

// ================= RESET POMPA NUTRISI =================
void resetPompaNutrisi() {

  flagSudahTuangNutrisi = false;

  stepNutrisi = 0;

  digitalWrite(nA, LOW);
  digitalWrite(nB, LOW);
}

// ================= RESET POMPA MIXING =================
void resetPompa() {

  pompaSudahJalan = false;
  pompaSedangNyala = false;

  flagSudahMixing = false;
}

// =================== RESET SAMPLING ====================
void resetTdsSampling() {
    countBacaTds = 0;
    totalTds = 0;
    flagTdsValid = false;
    lastBacaTds = millis();
}

// ================= POMPA NUTRISI BERGANTIAN =================
void pompaNutrisiBergantian(float totalVolume, unsigned long durasiThA, unsigned long durasiThB) {

  unsigned long now = millis();

  if(currentState == DOSING) {
    float volumeA = totalVolume;
    float volumeB = totalVolume;

    // hitung durasi
    durasiA = (volumeA / debitA) * 1000.0;
    durasiB = (volumeB / debitB) * 1000.0;
  }
  else if(currentState == THRESHOLD) {
    durasiA = durasiThA;
    durasiB = durasiThB;
  }

  // minimal ON
  if (durasiA < 200) durasiA = 200;
  if (durasiB < 200) durasiB = 200;

  // ================= STEP 0 =================
  // nyalakan pompa A

  if (stepNutrisi == 0) {

    digitalWrite(nA, HIGH);

    startPompaNutrisi = now;

    stepNutrisi = 1;
  }

  // ================= STEP 1 =================
  // tunggu pompa A selesai

  else if (stepNutrisi == 1) {

    if (now - startPompaNutrisi >= durasiA) {

      digitalWrite(nA, LOW);

      startPompaNutrisi = now;

      stepNutrisi = 2;
    }
  }

  // ================= STEP 2 =================
  // nyalakan pompa B

  else if (stepNutrisi == 2) {

    digitalWrite(nB, HIGH);

    startPompaNutrisi = now;

    stepNutrisi = 3;
  }

  // ================= STEP 3 =================
  // tunggu pompa B selesai

  else if (stepNutrisi == 3) {

    if (now - startPompaNutrisi >= durasiB) {

      digitalWrite(nB, LOW);

      flagSudahTuangNutrisi = true;

      stepNutrisi = 0;
    }
  }
}

// ================= POMPA MIXING =================
void pompaNyala() {

  unsigned long now = millis();

  if (!pompaSudahJalan) {

    digitalWrite(pump, HIGH);

    startPompaNyala = now;

    pompaSudahJalan = true;
    pompaSedangNyala = true;
  }

  if (pompaSedangNyala &&
      now - startPompaNyala >= durasiPompaNyala) {

    digitalWrite(pump, LOW);

    pompaSedangNyala = false;

    flagSudahMixing = true;
  }
}

// ================= PERHITUNGAN =================
float hitungVolume() {

  float v = ((targetPpm - tdsValueFix) * v2) / (ca + cb);

  if (v < 0) v = 0;

  return v;
}

// ================ RESET SAMPLING ================
// void resetTdsSampling() {
//   countBacaTds = 0;
//   totalTds = 0;
// }

// ================  CEK STABIL ================ 
bool isTdsStable() {
  float minValue = sampleTds[0];
  float maxValue = sampleTds[0];

  for(int i = 1; i < numBacaTds; i++) {
    if(sampleTds[i] < minValue) {
      minValue = sampleTds[i];
    }
    if(sampleTds[i] > maxValue) {
      maxValue = sampleTds[i];
    }
  }

  return (maxValue - minValue) <= 10;
}

// ================= LOOP =================
void loop() {

  ArduinoCloud.update();

  DateTime now = rtc.now();

  tentukanUmurPpm(now);

  // update cloud
  sisaNutrisi = volumeTandon;

  bool onEnter = (currentState != prevState);

  prevState = currentState;

  if(millis() - lastBacaSuhu >= 5000) {
    suhu = bacaSuhu();
    lastBacaSuhu = millis();
  }

  switch (currentState) {

    // ================= MONITORING =================
    case MONITORING:

      if(onEnter) {
        resetTdsSampling();
      }

      if(millis() - lastBacaTds >= intervalBacaTds) {
        tegangan = bacaTegangan();
        tdsValue = voltageToTDS(tegangan);
        sampleTds[countBacaTds] = tdsValue;
        totalTds += tdsValue;
        countBacaTds ++;
        lastBacaTds = millis();
      }
      if(countBacaTds >= numBacaTds) {
        if(isTdsStable()) {
          tdsValueFix = totalTds / countBacaTds;
          flagTdsValid = true;
        }
        countBacaTds = 0;
        totalTds = 0;
      }

      if (flagTdsValid) {

        flagTdsValid = false;

        if (tdsValueFix < targetPpm - 50) {
          currentState = DOSING;
        }
      }

      break;

    // ================= DOSING =================
    case DOSING:

      if (onEnter) {

        v1 = hitungVolume();

        resetPompaNutrisi();
      }

      pompaNutrisiBergantian(v1, 0, 0);

      if (flagSudahTuangNutrisi) {

        volumeTandon -= v1;

        if (volumeTandon < 0) {
          volumeTandon = 0;
        }

        resetPompa();

        currentState = MIXING;
      }

      break;

    // ================= MIXING =================
    case MIXING:

      if (onEnter) {

        resetPompa();
      }

      pompaNyala();

      if (flagSudahMixing) {

        startPompaNyala = millis();

        currentState = WAITING;
      }

      break;

    // ================= WAITING =================
    case WAITING:

      if (millis() - startPompaNyala >= 10000) {

        currentState = CHECKING;
      }

      break;

    // ================= CHECKING =================
    case CHECKING:

      if(onEnter) {
        resetTdsSampling();
      }

      if(millis() - lastBacaTds >= intervalBacaTds) {
        // tegangan = bacaTegangan();
        // tdsValue = voltageToTDS(tegangan);
        // countBacaTds ++;
        // totalTds += tdsValue;
        // lastBacaTds = millis();
        tegangan = bacaTegangan();
        tdsValue = voltageToTDS(tegangan);
        sampleTds[countBacaTds] = tdsValue;
        totalTds += tdsValue;
        countBacaTds ++;
        lastBacaTds = millis();
      }
      // if(countBacaTds >= numBacaTds) {
      //   tdsValueFix = totalTds / countBacaTds;
      //   flagTdsValid = true;
      //   countBacaTds = 0;
      //   totalTds = 0;
      if(countBacaTds >= numBacaTds) {
        if(isTdsStable()) {
          tdsValueFix = totalTds / countBacaTds;
          flagTdsValid = true;
        }
        countBacaTds = 0;
        totalTds = 0;
      }

      if (flagTdsValid) {

        flagTdsValid = false;

        if (tdsValueFix < targetPpm - 50) {

          currentState = THRESHOLD;
        }
        else {

          currentState = MONITORING;
        }
      }

      break;

    // ================= THRESHOLD =================
    case THRESHOLD:

      if (onEnter) {

        resetPompaNutrisi();
      }

      pompaNutrisiBergantian(0, 714, 1000); //B PAKE POMPA KENCENG

      if (flagSudahTuangNutrisi) {

        volumeTandon -= 150;

        if (volumeTandon < 0) {
          volumeTandon = 0;
        }

        resetPompa();

        currentState = MIXING;
      }

      break;
  }

  // ================= WARNING =================
  if (volumeTandon < 300) {

    Serial.println("WARNING: Nutrisi hampir habis!");
  }

  // ================= LCD =================
  if (millis() - lastLCD >= intervalLCD) {

    lastLCD = millis();

    tampilLcd();
    // Serial.println(tdsValue);
  }
}

// ================= CLOUD =================
void onRefillNutrisiChange() {

  if (refillNutrisi) {

    volumeTandon = volumeMax;

    sisaNutrisi = volumeTandon;
    
    refillNutrisi = false;

    Serial.println("Tandon diisi ulang!");
  }
}