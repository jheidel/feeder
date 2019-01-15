#include <EEPROM.h> // Arduino core
#include <Wire.h>  // Arduino core
#include "ds3231.h" // https://github.com/rodan/ds3231

#include "pitches.h"

// Pin for status LED / heartbeats
#define PIN_STATUS 13
// Pin for feeder relay mechanism
#define PIN_FEEDER 12
// Pin for piezo warning buzzer
#define PIN_PIEZO 8

// Used for time sanity checking.
#define COMPILE_CURRENT_YEAR 2019

// ---------

#define BUFF_MAX 128
uint8_t time[8];
char recv[BUFF_MAX];
unsigned int recv_size = 0;
unsigned long print_prev, print_interval = 5000;
unsigned long check_prev, check_interval = 100;

void parse_cmd(char *cmd, int cmdsize);

#define FEEDING_MAGIC 0xFEED
#define MAX_FEEDINGS 9

struct Feeding {
  uint8_t hour;
  uint8_t min;
  uint16_t duration;
};

struct Feedings {
  uint16_t magic;  // Config is valid if matches FEEDING_MAGIC. Otherwise, should re-initialize.
  uint16_t payload; // Will reset if the size doesn't match, indicates likely schema change.
  uint8_t count;
  struct Feeding feedings[MAX_FEEDINGS];
};

// Global feeding configuration.
Feedings feeding_config;

void setup()
{
  // Initialize modules.
  Serial.begin(9600);
  Wire.begin();
  DS3231_init(DS3231_CONTROL_INTCN);
  pinMode(PIN_FEEDER, OUTPUT);
  digitalWrite(PIN_FEEDER, LOW);
  pinMode(PIN_STATUS, OUTPUT);
  digitalWrite(PIN_STATUS, LOW);

  // Initialize local state.
  memset(recv, 0, BUFF_MAX);

  // Initialize config from EEPROM
  readFeedings();
  if (feeding_config.magic != FEEDING_MAGIC || feeding_config.payload != sizeof(Feedings)) {
    resetFeedings();
  }

  // Test read timestamp, verifies that the RTC battery hasn't died on us.
  struct ts t;
  DS3231_get(&t);
  if (t.year < COMPILE_CURRENT_YEAR) {
    for (int i = 0; i < 100; i++) {
      digitalWrite(PIN_STATUS, HIGH);
      delay(100);
      digitalWrite(PIN_STATUS, LOW);
      delay(100);
    }
    Serial.println("Current time looks incorrect!!!");
  }
}

void feed(uint16_t duration) {
  // TODO
  Serial.println("Feeding triggered.");

  for (int i = 0; i < 4; i++) {
    Serial.print(5 - i);
    Serial.println(" sec...");
    tone(PIN_PIEZO, 1000);
    digitalWrite(PIN_STATUS, HIGH);
    delay(500);
    noTone(PIN_PIEZO);
    digitalWrite(PIN_STATUS, LOW);
    delay(500);
  }
  Serial.println("1 sec...");
  for (int i = 0; i < 10; i++) {
    tone(PIN_PIEZO, 1000);
    digitalWrite(PIN_STATUS, HIGH);
    delay(50);
    noTone(PIN_PIEZO);
    digitalWrite(PIN_STATUS, LOW);
    delay(50);
  }

  Serial.println("Dispensing...");
  digitalWrite(PIN_FEEDER, HIGH);
  digitalWrite(PIN_STATUS, HIGH);
  delay(duration);
  digitalWrite(PIN_FEEDER, LOW);
  digitalWrite(PIN_STATUS, LOW);

  // Play dispensing complete melody.
  int melody[] = {
    //NOTE_C4, NOTE_G3, NOTE_G3, NOTE_A3, NOTE_G3, 0, NOTE_B3,
    NOTE_C5, NOTE_G4, NOTE_G4, NOTE_A4, NOTE_G4, 0, NOTE_B4, NOTE_C5
  };
  // note durations: 4 = quarter note, 8 = eighth note, etc.:
  int noteDurations[] = {
    4, 8, 8, 4, 4, 4, 4, 4
  };
  for (int i = 0; i < 8; i++) {
    int noteDuration = 1000 / noteDurations[i];
    tone(PIN_PIEZO, melody[i], noteDuration);
    int pauseBetweenNotes = noteDuration * 1.30;
    delay(pauseBetweenNotes);
    noTone(PIN_PIEZO);
  }

  Serial.println("Dispensing complete.");
}

void resetFeedings() {
  feeding_config = {0};
  feeding_config.magic = FEEDING_MAGIC;
  feeding_config.payload = sizeof(Feedings);
  writeFeedings();
}

void readFeedings() {
  EEPROM.get(0, feeding_config);
  Serial.println("Read feeder config from EEPROM");
}

void writeFeedings() {
  EEPROM.put(0, feeding_config);
  Serial.println("Wrote feeder config to EEPROM");
}

int16_t feedIdxMatch = -1;  // feeder polling global state

void pollFeedings(const struct ts& current_time) {
  for (int i = 0; i < feeding_config.count; ++i) {
    const struct Feeding& feeding = feeding_config.feedings[i];
    if (feeding.hour == current_time.hour && feeding.min == current_time.min) {
      if (feedIdxMatch == i) {
        return;  // Already did this feed for the current minute.
      }
      feed(feeding.duration);
      feedIdxMatch = i;
      return;
    }
  }
  feedIdxMatch = -1;  // No match.
}

void testFeeding() {
  // Trigger feed with duration of first feeding, or if that doesn't exist, some sane value.
  uint16_t duration = 3000;
  if (feeding_config.count > 0) {
    duration = feeding_config.feedings[0].duration;
  }
  feed(duration);
}

void loop()
{
  char in;
  char buff[BUFF_MAX];
  unsigned long now = millis();
  struct ts t;


  if (Serial.available() > 0) {
    // HANDLE SERIAL
    in = Serial.read();

    if ((in == 10 || in == 13) && (recv_size > 0)) {
      parse_cmd(recv, recv_size);
      recv_size = 0;
      recv[0] = 0;
    } else if (in < 48 || in > 122) {
      ;       // ignore ~[0-9A-Za-z]
    } else if (recv_size > BUFF_MAX - 2) {   // drop lines that are too long
      // drop
      recv_size = 0;
      recv[0] = 0;
    } else if (recv_size < BUFF_MAX - 2) {
      recv[recv_size] = in;
      recv[recv_size + 1] = 0;
      recv_size += 1;
    }
  } else if (now - check_prev > check_interval) {
    // CHECK FOR FEEDING TIMES
    DS3231_get(&t);
    pollFeedings(t);
    check_prev = now;

    // HEARTBEAT, shows both arduino and RTC are functional.
    digitalWrite(PIN_STATUS, t.sec % 2 == 0 ? HIGH : LOW);
  }
  else if (now - print_prev > print_interval) {
    // PERIODICALLY PRINT INFO
    DS3231_get(&t);

    // Print Time.
    snprintf(buff, BUFF_MAX, "Current time: %d.%02d.%02d %02d:%02d:%02d (W %d, 1=Sunday)", t.year,
             t.mon, t.mday, t.hour, t.min, t.sec, t.wday);
    Serial.println(buff);

    // Print feedings.
    snprintf(buff, BUFF_MAX, "%d configured feedings.", feeding_config.count);
    Serial.println(buff);
    for (int i = 0; i < feeding_config.count; ++i) {
      const struct Feeding& feeding = feeding_config.feedings[i];
      snprintf(buff, BUFF_MAX, "\tFeeding #%d: T %02d:%02d, dur %d ms", i + 1, feeding.hour, feeding.min, feeding.duration);
      Serial.println(buff);
    }

    // Print help notice.
    Serial.println("Command 'H' for help.");
    print_prev = now;
  }
}

void parse_cmd(char *cmd, int cmdsize)
{
  uint8_t i;
  uint8_t reg_val;
  char buff[BUFF_MAX];
  struct ts t;

  if (cmd[0] == 'T' && cmdsize == 1) {
    Serial.println("Usage: TYYYYMMDDHHMMSSW (W 1=Sunday)");
  } else if (cmd[0] == 'T' && cmdsize == 16) {
    // SET TIME
    t.year = inp2toi(cmd, 1) * 100 + inp2toi(cmd, 3);
    t.mon = inp2toi(cmd, 5);
    t.mday = inp2toi(cmd, 7);
    t.hour = inp2toi(cmd, 9);
    t.min = inp2toi(cmd, 11);
    t.sec = inp2toi(cmd, 13);
    t.wday = cmd[15] - 48;
    DS3231_set(t);
    Serial.println("Time set OK");
  } else if (cmd[0] == 'C' && cmdsize == 1) {
    // READ TEMPERATURE
    Serial.print("Temp *C: ");
    Serial.println(DS3231_get_treg(), DEC);
  } else if (cmd[0] == 'A' && cmdsize == 1) {
    Serial.println("Usage: AHHMMDDDD (D, feed duration in ms)");
  } else if (cmd[0] == 'A' && cmdsize == 9) {
    // ADD FEEDING
    if (feeding_config.count == MAX_FEEDINGS) {
      Serial.println("ERROR: max feeding reached");
      return;
    }
    struct Feeding* feeding = &feeding_config.feedings[feeding_config.count++];
    feeding->hour = inp2toi(cmd, 1);
    feeding->min = inp2toi(cmd, 3);
    feeding->duration = inp2toi(cmd, 5) * 100 + inp2toi(cmd, 7);
    writeFeedings();
  } else if (cmd[0] == 'D' && cmdsize == 1) {
    Serial.println("Usage: DN (N = feeding number)");
  } else if (cmd[0] == 'D' && cmdsize == 2) {
    // REMOVE FEEDING
    uint8_t remove_idx = (cmd[1] - 48) - 1;
    if (remove_idx >= feeding_config.count) {
      Serial.println("ERROR: value out of range");
      return;
    }
    for (int i = remove_idx; i < feeding_config.count - 1; i++) {
      feeding_config.feedings[i] = feeding_config.feedings[i + 1];
    }
    feeding_config.count--;
    writeFeedings();
  } else if (cmd[0] == 'R' && cmdsize == 1) {
    resetFeedings();
  } else if (cmd[0] == 'F' && cmdsize == 1) {
    // FEEDER TEST
    Serial.println("Optional usage: FDDDD (D = feeding duration ms)");
    testFeeding();
  } else if (cmd[0] == 'F' && cmdsize == 5) {
    uint16_t duration = inp2toi(cmd, 1) * 100 + inp2toi(cmd, 3);
    feed(duration);
  } else {
    // PRINT HELP
    if (cmd[0] != 'H') {
      Serial.print("Unknown command ");
      Serial.println(cmd[0]);
    }
    Serial.println("Help:");
    Serial.println("\tT = set time");
    Serial.println("\tC = read temperature");
    Serial.println("\tA = add feeding");
    Serial.println("\tD = delete feeding");
    Serial.println("\tR = reset all configuration");
    Serial.println("\tF = test feeder mechanism");
  }
}
