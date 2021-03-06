#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>

#define batteryArraySize 6
#define batteriesPerSelector 6
#define batteriesPerPage 6
#define numberOfChargers 6
#define numberOfDischargers 2
#define numberOfSelectors 2
#define supplyVoltage 5.0

#define buttonPin 8

#define MCP23017_IODIRA 0x00
#define MCP23017_IODIRB 0x01
#define MCP23017_GPIOA 0x12
#define MCP23017_GPIOB 0x13
#define MCP23017_OLATA 0x14
#define MCP23017_OLATB 0x15

#define UNUSED 255

#define DEBUG 0

enum STATE {
  WAITING_CHARGE,
  CHARGE,
  WAITING_DISCHARGE,
  DISCHARGE,
  DONE
};

enum SELECTOR {
  CHARGER,
  DISCHARGER
};

uint8_t page = 0;
uint8_t cycleStarted = 0;
uint8_t dischargerStatus[numberOfDischargers] = {UNUSED, UNUSED};
uint8_t dischargerPins[numberOfDischargers] = {A0, A1};
double batteryCapacities[batteryArraySize];
enum STATE batteryStates[batteryArraySize];
uint8_t chargerPins[numberOfChargers] = {2, 3, 4, 5, 6, 7};
uint8_t selectorAddresses[numberOfSelectors] = {0x20, 0x21};
uint8_t chargerStatus[numberOfChargers] = {UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED};


LiquidCrystal_PCF8574 display(0x3F);

uint8_t readIOExpander(uint8_t i2caddr, uint8_t addr) {
  Wire.beginTransmission(i2caddr);
  Wire.write(addr);
  Wire.endTransmission();
  Wire.requestFrom(i2caddr, 1);
  return Wire.read();
}

void updateIOExpander(uint8_t i2caddr, uint8_t regAddr, uint8_t regValue) {
  Wire.beginTransmission(i2caddr);
  Wire.write(regAddr);
  Wire.write(regValue);
  Wire.endTransmission();
}

void updateSelector(int8_t batteryNumber, enum SELECTOR reg, uint8_t enabled) {
  uint8_t selectorNumber = batteryNumber / batteriesPerSelector;
  uint8_t i2caddr = selectorAddresses[selectorNumber];
  uint8_t gpio = readIOExpander(i2caddr, (reg == CHARGER) ? MCP23017_OLATA : MCP23017_OLATB);
  bitWrite(gpio, batteryNumber % batteriesPerSelector, enabled);
  updateIOExpander(selectorAddresses[selectorNumber], (reg == CHARGER) ? MCP23017_GPIOA : MCP23017_GPIOB, gpio);
}

uint8_t setBatteryChargeStatus(uint8_t batteryNumber, uint8_t enabled) {
  uint8_t chargerNumber = batteryNumber % numberOfChargers;
  if (enabled == 1 && chargerStatus[chargerNumber] != UNUSED) {
    return 0;
  }
  updateSelector(batteryNumber, CHARGER, enabled);
  chargerStatus[chargerNumber] = (enabled == 1) ? batteryNumber : UNUSED;
  return 1;
}

uint8_t setBatteryDischargeStatus(uint8_t batteryNumber, uint8_t enabled) {
  uint8_t dischargerNumber = batteryNumber % numberOfDischargers;
  if (enabled == 1 && dischargerStatus[dischargerNumber] != UNUSED) {
    return 0;
  }
  updateSelector(batteryNumber, DISCHARGER, enabled);
  dischargerStatus[dischargerNumber] = (enabled == 1) ? batteryNumber : UNUSED;
  return 1;
}

void logMilliampHoursForTheLastSecond() {
  for (uint8_t j = 0; j < numberOfDischargers; j++) {
    uint8_t batteryNumber = dischargerStatus[j];
    if(batteryNumber != UNUSED) {
      batteryCapacities[batteryNumber] = batteryCapacities[batteryNumber] + 0.000277777777778;
   }
  }
}

double readVoltage(uint8_t voltagePin) {
  double reading = 0;
  reading += analogRead(voltagePin);
  reading += analogRead(voltagePin);
  reading += analogRead(voltagePin);
  reading += analogRead(voltagePin);
  reading += analogRead(voltagePin);
  reading += analogRead(voltagePin);
  reading += analogRead(voltagePin);
  reading += analogRead(voltagePin);
  reading += analogRead(voltagePin);
  reading += analogRead(voltagePin);
  return (supplyVoltage * (reading / 10)) / 1024;
}

void protectFromOverDischarge() {
  for (uint8_t j = 0; j < numberOfDischargers; j++) {
    uint8_t batteryNumber = dischargerStatus[j];
    if(batteryNumber != UNUSED) {
      double voltage = readVoltage(dischargerPins[j]);
      
      if (voltage <= 3.0) {
        setState(batteryNumber, DONE);
      }
    }
  }
}

void checkForBatteryCharged() {
  for (uint8_t j = 0; j < numberOfChargers; j++) {
    uint8_t batteryNumber = chargerStatus[j];
    if(batteryNumber != UNUSED) {
      int chargeStatus = digitalRead(chargerPins[j]);
      
      if (chargeStatus == 1) {
        setState(batteryNumber, WAITING_DISCHARGE);
      }
    }
  }
}

void checkForFreeChargers() {
  uint8_t i;
  for (uint8_t i = 0; i < batteryArraySize; i++ ) {
    if(batteryStates[i] == WAITING_CHARGE) {
      setState(i, CHARGE);  
    }
  }
}

void checkForFreeDischargers() {
  uint8_t i;
  for (uint8_t i = 0; i < batteryArraySize; i++ ) {
    if(batteryStates[i] == WAITING_DISCHARGE) {
      setState(i, DISCHARGE);  
    }
  }
}

void checkForDone() {
  uint8_t i;
  bool done = true;
  for (uint8_t i = 0; i < batteryArraySize; i++ ) {
    if(batteryStates[i] != DONE) {
        done = false;
    }
  }
  cycleStarted = done ? 0 : 1;
}

void setState(uint8_t batteryNumber, enum STATE newState) {
  switch (newState) {
    case WAITING_CHARGE:
      batteryStates[batteryNumber] = newState;
      break;
    case CHARGE:    
      if(setBatteryChargeStatus(batteryNumber, 1) == 1) {
        batteryStates[batteryNumber] = newState;  
      }
      break;
    case WAITING_DISCHARGE:
      batteryStates[batteryNumber] = newState;
      setBatteryChargeStatus(batteryNumber, 0);
      break;
    case DISCHARGE:
      if(setBatteryDischargeStatus(batteryNumber, 1) == 1) {
        batteryStates[batteryNumber] = newState;
      } 
      break;
    default:
      batteryStates[batteryNumber] = newState;
      setBatteryDischargeStatus(batteryNumber, 0);
      break;
  }
}

void updateDisplayLine(uint8_t batteryNumber) {
  uint8_t dischargerNumber = batteryNumber % numberOfDischargers;
  if (batteryNumber < 10) {
    display.print("0");
  }
  display.print(batteryNumber);
  switch (batteryStates[batteryNumber]) {
    case WAITING_CHARGE:
    case WAITING_DISCHARGE:
      display.print("W ");
      break;
    case CHARGE:
      display.print("C");
      break;
    case DISCHARGE:
      display.print("D ");
      display.print((int)(batteryCapacities[batteryNumber] * 1000));
      break;
    case DONE:
      display.print("F ");
      display.print((int)(batteryCapacities[batteryNumber] * 1000));
      break;
  }
}

void updateDisplay() {
  uint8_t i, j;
  uint8_t startBattery = page * batteriesPerPage;
  uint8_t endBattery = startBattery + batteriesPerPage;

  for (uint8_t j = startBattery; j < endBattery; j++ ) {
    display.setCursor(j > (startBattery + 2) ? 9 : 0, j % 3);
    updateDisplayLine(j);
  }
  display.setCursor(18, 4);
  display.print("P");
  display.print(page);

  if (endBattery == batteryArraySize) {
    page = 0;
  } else {
    page++;
  }
}

#if defined(DEBUG)
void debugOuput() {
  Serial.println("");
  Serial.println("----------------");
  Serial.println("| Debug Output |");
  Serial.println("----------------");
  Serial.println("");
  Serial.println("Dischargers");
  Serial.println("----------------");
  Serial.println("| id | battery |"); 
  Serial.println("----------------");
  for (uint8_t j = 0; j < numberOfDischargers; j++ ) {
      Serial.print("| ");
      Serial.print(j);
      Serial.print("  | ");
      if(dischargerStatus[j] == UNUSED) {
        Serial.println("UNUSED  |");
      } else {
        Serial.print(dischargerStatus[j]);
        Serial.println("      |");
      }
  }
  Serial.println("----------------");
  Serial.println("");

  Serial.println("Chargers");
  Serial.println("----------------");
  Serial.println("| id | battery |"); 
  Serial.println("----------------");
  for (uint8_t j = 0; j < numberOfChargers; j++ ) {
    Serial.print("| ");
    Serial.print(j);
    Serial.print("  | ");
    if(chargerStatus[j] == UNUSED) {
      Serial.println("UNUSED |");
    } else {
      Serial.print(chargerStatus[j]);
      Serial.println("      |");
    }
  }
  Serial.println("----------------");
  Serial.println("");

  Serial.println("Battery Charging");
  Serial.println("------------");
  Serial.println("| id | y/n |"); 
  Serial.println("------------");
  byte chargerSelectorStatus = readIOExpander(selectorAddresses[0], MCP23017_OLATA);
  for (byte i=0; i<8; i++) {
    byte state = bitRead(chargerSelectorStatus, i);
    Serial.print("| ");
    Serial.print(i);
    Serial.print("  | ");
    if(state == 1) {
      Serial.println("yes |");
    } else {
      Serial.println("no  |");
    }
  }
  Serial.println("------------");
  Serial.println("");
  
  Serial.println("Discharger Selector Status:");
  Serial.println("Battery Discharging");
  Serial.println("------------");
  Serial.println("| id | y/n |"); 
  Serial.println("------------");
  byte dischargerSelectorStatus = readIOExpander(selectorAddresses[0], MCP23017_OLATB);
    for (byte i=0; i<8; i++) {
    byte state = bitRead(dischargerSelectorStatus, i);
    Serial.print("| ");
    Serial.print(i);
    Serial.print("  | ");
    if(state == 1) {
      Serial.println("yes |");
    } else {
      Serial.println("no  |");
    }
  }
  Serial.println("------------");
  Serial.println("");


  Serial.println("Dischargers Voltages");
  Serial.println("----------------");
  Serial.println("| id | voltage |"); 
  Serial.println("----------------");
  for (uint8_t j = 0; j < numberOfDischargers; j++ ) {
    double voltage = readVoltage(dischargerPins[j]);
    Serial.print("| ");
    Serial.print(j);
    Serial.print("  | ");
    if(dischargerStatus[j] == UNUSED) {
      Serial.println("UNUSED  |");
    } else {
      Serial.print(voltage);
      Serial.println("     |");
    }
  }
  Serial.println("----------------");
  Serial.println("");
}
#endif

void loop() {

  if(cycleStarted == 1) {
    logMilliampHoursForTheLastSecond();
    checkForBatteryCharged();
    protectFromOverDischarge();
    checkForFreeChargers();
    checkForFreeDischargers();
    checkForDone();
  }

  uint8_t buttonStatus = digitalRead(buttonPin);

  #if defined(DEBUG)
  if (Serial.available() > 0) {
    char in = Serial.read();
    if(in == 'g') {
      buttonStatus = 0; 
    }
    if(in == 'd') {
      debugOuput();
    }
  }
  #endif
  
  if (buttonStatus == 0) {
    cycleStarted = 1;
    checkForFreeChargers();
    updateDisplay();
    delay(500);
  } else {
    updateDisplay();
  }
  delay(1000);
}

void setupDisplay() {
  display.begin(20, 4);
  display.setBacklight(255);
}

void setupSelectors() {
  uint8_t i;
  for (uint8_t i = 0; i < (batteryArraySize / batteriesPerSelector); i++ ) {
    updateIOExpander(selectorAddresses[i], MCP23017_IODIRA, 0x00);
    updateIOExpander(selectorAddresses[i], MCP23017_GPIOA, 0x00);
    updateIOExpander(selectorAddresses[i], MCP23017_IODIRB, 0x00);
    updateIOExpander(selectorAddresses[i], MCP23017_GPIOB, 0x00);
  }
}

void setupDefaultState() {
  uint8_t i;
  for (uint8_t i = 0; i < batteryArraySize; i++ ) {
    setState(i, WAITING_CHARGE);
    batteryCapacities[i] = 0;
  }
}

void setupButtonIOPins() {
  pinMode(buttonPin, INPUT_PULLUP);
}

void setupChargeIOPins() {
  for (uint8_t i = 0; i < numberOfChargers; i++ ) {
    pinMode(chargerPins[i], INPUT_PULLUP);
  }
}

void setupDischardIOPins() {
   pinMode(A0, INPUT);
   pinMode(A1, INPUT);
}

void setup() {
  #if defined(DEBUG)
  Serial.begin(9600);
  Serial.println("Discharge Station in Debug Mode");
  #endif
  
  Wire.begin();
  setupDisplay();
  setupSelectors();
  setupDefaultState();
  setupButtonIOPins();
  setupChargeIOPins();
  setupDischardIOPins();
}


