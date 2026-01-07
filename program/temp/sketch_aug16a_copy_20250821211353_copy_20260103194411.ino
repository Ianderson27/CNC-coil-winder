/**
CNC coil winder program 
Designed by Isaac A 
Instructable link: https://www.instructables.com/Customizable-CNC-Coil-Winder/
**/ 

#include <Arduino.h>
#include <Keypad.h>
#include <AccelStepper.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <MenuSystem.h>
#include <EEPROM.h>
#include <limits.h>

const int spindleDir = 1; // direction for spindle motor
const int spindleStep = 10; // step for spindle motor
const int gantryDir = 0; // direction for gantry motor
const int gantryStep = 9; // step for gantry motor
const int buzzer = 13; // control pin for buzzer
const int tensionSwi = A1; // input pin for wire tension switch
const int eStopPin = 2; // input pin for emergency stop switch
const int gantryLimMax = A3; // gantry limit switch for maximum position
const int gantryLimMin = A2; // gantry limit switch for minimum position
const int LCD_SDA = A4; // LCD display data pin
const int LCD_SCL = A5; // LCD display clock pin


struct CoilParam {
    float wireDia; // in MM
    float turnNum; 
    float turnSpace; // in MM
    long startPos; // in Steps
    long stopPos; // in Steps
    long spindleSpeed; // in Steps
    CoilParam() :
        wireDia(-1),
        turnNum(-1),
        turnSpace(-1),
        startPos(-1),
        stopPos(-1),
        spindleSpeed(-1)
    {}
};
CoilParam CoilParameters;

struct Settings {
    long spindleMaxSpeedValue;
    long gantryMaxSpeedValue;
    long accelSpeedValue;
    bool accelMode;
    bool buzzerEnabled;
    bool tensionSwitchEnabled;
    Settings():
        spindleMaxSpeedValue(4000),//default values, later updated with stored values from EERPOM
        gantryMaxSpeedValue(4000),//default values, later updated with stored values from EERPOM
        accelMode(false),
        buzzerEnabled(true),
        tensionSwitchEnabled(false)
        {}

  };
Settings settings;

struct physicalParameters {
    long gantryStepsPerRev;
    long gantryMicrosteps;
    long gantryPitch_mm; // pitch in mm
    long spindleStepsPerRev;
    long spindleMicrosteps;
    float spindleGearRatio; // 
    long gantryLength; // length of gantry in steps   
    physicalParameters():
        gantryStepsPerRev(200), //Stepper motor specific 
        gantryMicrosteps(4), 
        gantryPitch_mm(2), // 2mm pitch
        spindleStepsPerRev(200), // full step
        spindleMicrosteps(2),
        spindleGearRatio(1.66667), // 36 teath / 60 teath belt reduction
        gantryLength(100000) // default value, initialized in void loop with settingRecal()
    {}
};
physicalParameters physicalParams;

//Standard calculations used throughout the program
const int spindle_stepsPerRev = (physicalParams.spindleStepsPerRev *physicalParams.spindleMicrosteps * physicalParams.spindleGearRatio);
const int gantry_stepsPerMM = ((physicalParams.gantryStepsPerRev *physicalParams.gantryMicrosteps) / physicalParams.gantryPitch_mm ); 
const int CoilParameters_size = sizeof(CoilParameters); // defines the size in bytes of coilParameters struct 
const int settings_location = sizeof(physicalParams.gantryLength); // defines memory location to store settings struct 
const int numSavedSettings_location = settings_location + sizeof(settings); // defines memory location to store numSavedSettings parameter 
const int savedSettings_location = numSavedSettings_location + 2; // defines starting memory location to store saved settings, numSavedSettings is unsigned short int

//Memory space| 0: gantryLength, settings_location: settings struct, numSavedSettings_location: number of saved settings, savedSettings_location: saved settings
template<typename T>
void saveEEPROM(const T& value, int address) {
    EEPROM.put(address, value);
}
template<typename T>
void readEEPROM(T& value, int address){
    EEPROM.get(address, value);
}

//Initilizing keypad
const byte ROWS = 5;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'T','P','.','C'},  //start, stop/pause, ., clear
  {'1','2','3','U'},  //1, 2, 3, up 
  {'4','5','6','D'},  //4, 5, 6, down 
  {'7','8','9','S'},  // 7, 8, 9, escape
  {'<','0','>','E'}   // Left, 0, right, enter
};
const byte rowPins[ROWS] = {A0, 12, 11, 8, 7}; // connect to the row pinouts of the keypad
const byte colPins[COLS] = {3, 4, 5, 6}; // connect to the column pinouts of the keypad
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

//Initilizing stepper motors
AccelStepper spindleStepper(1, spindleStep, spindleDir); // mode 1 for use with a stepper driver
AccelStepper gantryStepper(1, gantryStep, gantryDir); // mode 1 for use with a stepper driver

//Initilizing LCD
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Custom renderer for LCD
class MyRenderer : public MenuComponentRenderer {
public:
    mutable uint8_t top_index = 0; // First visible menu item index
    static const uint8_t DISPLAY_ROWS = 4; // Your LCD rows
    static const uint8_t TITLE_ROWS   = 1; // Reserve top row for title

    void render(Menu const& menu) const override {
        lcd.clear();

        // Row 0 → menu title
        lcd.setCursor(0, 0);
        lcd.print(menu.get_name());

        uint8_t total_items    = menu.get_num_components();
        uint8_t selected_index = get_selected_index(menu);

        // Adjust scroll window
        if (selected_index < top_index) {
            top_index = selected_index;
        } 
        else if (selected_index >= top_index + (DISPLAY_ROWS - TITLE_ROWS)) {
            top_index = selected_index - (DISPLAY_ROWS - TITLE_ROWS) + 1;
        }

        // Draw visible menu items
        for (uint8_t row = 0; row < (DISPLAY_ROWS - TITLE_ROWS); row++) {
            uint8_t item_index = top_index + row;
            if (item_index >= total_items) break;

            lcd.setCursor(0, row + TITLE_ROWS);

            if (item_index == selected_index) {
                lcd.print(">");
            } else {
                lcd.print(" ");
            }

            menu.get_menu_component(item_index)->render(*this);
        }
    }

    void render_menu_item(MenuItem const& menu_item) const override {
        lcd.print(menu_item.get_name());
    }

    void render_back_menu_item(BackMenuItem const& menu_item) const override {
        lcd.print(menu_item.get_name());
    }

    void render_numeric_menu_item(NumericMenuItem const& menu_item) const override {
        lcd.print(menu_item.get_name());
    }

    void render_menu(Menu const& menu) const override {
        lcd.print(menu.get_name());
    }

private:
    uint8_t get_selected_index(Menu const& menu) const {
        uint8_t total = menu.get_num_components();
        MenuComponent const* selected = menu.get_current_component();
        for (uint8_t i = 0; i < total; i++) {
            if (menu.get_menu_component(i) == selected) {
                return i;
            }
        }
        return 0; // default fallback
    }
};

MyRenderer my_renderer;

// Forward declarations for menu callbacks
void save_setting(MenuComponent* menu_component);
void wireDia_set(MenuComponent* menu_component);
void turnNum_set(MenuComponent* menu_component);
void turnSpace_set(MenuComponent* menu_component);
void startPos_set(MenuComponent* menu_component);
void stopPos_set(MenuComponent* menu_component);
void spindleSpeed_set(MenuComponent* menu_component);
void accelSpeed_set(MenuComponent* menu_component);
void accelMode_set(MenuComponent* menu_component);
void start(MenuComponent* menu_component);
void tensionSwi_set(MenuComponent* menu_component);
void spindleMaxSpeed_set(MenuComponent* menu_component);
void gantryMaxSpeed_set(MenuComponent* menu_component);
void homing_set(MenuComponent* menu_component);
void move_set(MenuComponent* menu_component);
void version_set(MenuComponent* menu_component);
void buzzer_set(MenuComponent* menu_component);
void gantryCal_set(MenuComponent* menu_component);

MenuSystem ms(my_renderer);

// Create menu structure
Menu singleLayCoil("Single layer coil");
Menu multiLayCoil("Multi layer coil");
Menu tranformerWind("Transformer winding");
Menu jog("Jog");
Menu cal("Calibration");
Menu setting("Settings");
Menu gradualAccel("Acceleration");

MenuItem settingsSave("Saved settings", &save_setting);
MenuItem wireDia("Wire diameter", &wireDia_set);
MenuItem turnNum("Number of turns", &turnNum_set);
MenuItem turnSpace("Turn spacing", &turnSpace_set);
MenuItem startPos("Start position", &startPos_set);
MenuItem stopPos("Stop position", &stopPos_set);
MenuItem spindleSpeed("Spindle speed", &spindleSpeed_set);
MenuItem accelSpeed("Accel speed", &accelSpeed_set);
MenuItem accelMode("Accel on/off", &accelMode_set);
MenuItem run("Run", &start);
MenuItem tensionSwiMode("Tension switch", &tensionSwi_set);
MenuItem spindleMaxSpeed("Spindle max Speed", &spindleMaxSpeed_set);
MenuItem gantryMaxSpeed("Gantry max speed", &gantryMaxSpeed_set);
MenuItem homing("Homing", &homing_set);
MenuItem move("Move", &move_set);
MenuItem version("Version", &version_set);
MenuItem buzzerMode("Buzzer", &buzzer_set);
MenuItem gantryCal("Gantry cal", &gantryCal_set);

//E stop callback function
volatile bool emergencyStopTriggered = false;
void eStop() {
    emergencyStopTriggered = true;
}

//Beeps buzzer once, takes duration and freq input
void beepBuzzer(int duration = 50, int freq = 2000) {
    if (settings.buzzerEnabled) {
        tone(buzzer, freq, duration);
        delay(duration);
        noTone(buzzer);
    }
}

//Beeps buzzer multiple times, takes duration and freq input
void alarmBuzzer(int duration = 500, int freq = 2000) {
    if (settings.buzzerEnabled) {
        for (int i = 0; i < 3; ++i) {
            tone(buzzer, freq, duration / 3);
            delay(duration / 3);
            noTone(buzzer);
            delay(duration / 3);
        }
    }
}

//Takes numeric input from keypad 
void numeric_input(const __FlashStringHelper* header,
                   const __FlashStringHelper* paramName,
                   float& value) {
    char input[12];        // buffer for up to 10 chars + dot + null
    byte length = 0;       
    bool hasDecimal = false;
    input[0] = '\0';

    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(header);
    lcd.setCursor(0, 1);
    lcd.print(paramName);
    lcd.print(F(": "));
    lcd.print(value, 2);  // show 2 decimal places by default
    lcd.setCursor(0, 3);
    lcd.print(F("ESC:Cancel ENT:OK"));

    lcd.setCursor(0, 2);

    while (true) {
        char key = keypad.getKey();
        if (key) {
            beepBuzzer();

            if (key >= '0' && key <= '9') {
                if (length < 11) {
                    input[length++] = key;
                    input[length] = '\0';
                }
            } else if (key == '.' && !hasDecimal) {
                if (length < 11) {
                    input[length++] = '.';
                    input[length] = '\0';
                    hasDecimal = true;
                }
            } else if (key == 'C') {  // Clear input
                length = 0;
                hasDecimal = false;
                input[0] = '\0';
            } else if (key == 'E') {  // Enter/OK
                if (length > 0) {
                    value = atof(input); // convert to float
                    break;
                }
            } else if (key == 'S') {  // ESC/Cancel
                length = 0;
                input[0] = '\0';
                break;
            }

            // Update display
            lcd.setCursor(0, 2);
            lcd.print("                    "); // clear line
            lcd.setCursor(0, 2);
            lcd.print(input);
        }
    }
}

//Takes boolean input from keypad (yes/no) returns a bool value
bool bool_input(const char* paramName, bool& value) {
    bool selected = value; // Start with current value
    bool confirmed = false;
    bool cancelled = false;

    while (!confirmed && !cancelled) {
        lcd.setCursor(0, 1);
        lcd.print(paramName);
        lcd.print(F(": "));
        lcd.setCursor(0,2);


        if (selected) {
            lcd.print(F(">")); // Yes inverted
            lcd.print(F(" Yes "));
            lcd.print(F(" No "));
        } else {
            lcd.print(F(" Yes "));
            lcd.print(F(">")); // No inverted
            lcd.print(F(" No "));
        }
        lcd.print("   "); // Clear rest of line

        char key = keypad.getKey();
        if (key) {
            beepBuzzer();
            if (key == '<' || key == '>') {
                selected = !selected; // Toggle selection
            } else if (key == 'E') { // Enter
                confirmed = true;
            } else if (key == 'C') { // Esc
                cancelled = true;
            }
        }
        delay(100); // Debounce
    }
    if (confirmed) {
        value = selected;
        return true;
    }
    return false; // Cancelled
}

void displayText(const __FlashStringHelper* text = nullptr) {
    lcd.clear();
    lcd.setCursor(0, 0);
    if (text) {
        lcd.print(text);
    } else {
        lcd.print(F("Setting Saved")); // fallback default
    }
}

bool spindle_jogActive = false; // enables spindle to be controlled by motor jog
bool gantry_jogActive = false; // enables gantry to be controlled by motor jog

// Motor jog function is attached as EventListener to keypad 
// Runs each time a key is pressed
// Used for jogging the stepper motor while key is pressed 
void motorJog(KeypadEvent key){
    if (!spindle_jogActive && !gantry_jogActive) return;  

    KeyState state = keypad.getState();
    bool minTriggered = (digitalRead(gantryLimMin) == LOW);
    bool maxTriggered = (digitalRead(gantryLimMax) == LOW);
    long maxLongVal = LONG_MAX;
    if (state == PRESSED) {
        if (key == '<' && !minTriggered && gantry_jogActive) {
            gantryStepper.move(-maxLongVal);
        } else if (key == '>' && !maxTriggered && gantry_jogActive) {
            gantryStepper.move(maxLongVal);
        } else if (key == 'U' && spindle_jogActive) {
            spindleStepper.move(-maxLongVal);
        } else if (key == 'D' && spindle_jogActive) {
            spindleStepper.move(maxLongVal);
        }
    } 
    else if (state == RELEASED) {
        if ((key == '<' || key == '>') && gantry_jogActive){
            gantryStepper.setCurrentPosition(gantryStepper.currentPosition());
            gantryStepper.stop();
        }
        if ((key == 'U' || key == 'D') && spindle_jogActive){
            spindleStepper.setCurrentPosition(spindleStepper.currentPosition());
            spindleStepper.stop();
        }

    }
}

void homeGantry() {
    lcd.setCursor(0,0);
    displayText(F("Homing Gantry..."));
    lcd.setCursor(0,3);
    displayText(F("ESC to cancle"));
    // Set a reasonable speed and acceleration for homing
    gantryStepper.setCurrentPosition(gantryStepper.currentPosition()); // Reset planner position
    gantryStepper.setSpeed(settings.gantryMaxSpeedValue);      // adjust as needed
    gantryStepper.setAcceleration(300);  // adjust as needed
    gantryStepper.move(-LONG_MAX); //1.0/0 results in infinity 
    while (digitalRead(gantryLimMin) == HIGH) { // While not triggered (low when pressed)
        gantryStepper.run();
    
        char key = keypad.getKey();
        if (key) {
            if (key == 'S'){
                displayText(F("Cancled"));
                delay(1000);
                return;
            }
        }  
    }
    gantryStepper.stop(); // Stop movement
    gantryStepper.setCurrentPosition(0); // Set home position to zero
    lcd.setCursor(0, 1);
    lcd.print(F("Homed!"));
    delay(1000);
}

// Calculation helper functions for converting units 

// ---- START POSITION ---
static float stepsToCM(long steps) {
    return (steps / (gantry_stepsPerMM*10.00));
}

static long cmToSteps(float cm) {
    return (long)(cm * 10.0 * gantry_stepsPerMM);
}
// ---- STOP POSITION ----
static float stepsToCM_offset(long steps) {
    return ((steps - CoilParameters.startPos) / (gantry_stepsPerMM *10.0));
}

static long cmToSteps_offset(float cm) {
    return CoilParameters.startPos + (long)(cm * 10.0 * gantry_stepsPerMM);
}
// ---- SPINDLE ----
static float stepsToRPM(long steps) {
    return ((steps * 60.0) / spindle_stepsPerRev);
}

static long rpmToSteps(float rpm) {
    return (long)((rpm / 60.0) * spindle_stepsPerRev);
}


void windCoil(const CoilParam& params) {
    beepBuzzer();
    // --- Coil parameters ---
    const float wireDia = params.wireDia;
    const long turns = max(1L, params.turnNum); // finds greater of two numbers (1L) = long data type
    const long turnSpace = params.turnSpace;
    const long startPos = params.startPos; // in steps
    const long stopPos  = params.stopPos; // in steps
    long spindleSpeed = max(10L, params.spindleSpeed);

    // --- spindle setup ---
    const long spindleTurns = (spindle_stepsPerRev*turns);
    
    // --- gantry setup ---
    float gantryStepPerSpindleStep = (((wireDia+turnSpace)*gantry_stepsPerMM)/spindle_stepsPerRev );

    spindleStepper.setMaxSpeed(spindleSpeed);
    gantryStepper.setMaxSpeed(4000);

    long currentTurn = 0;
    bool gantryDir = true;
    bool cancelled = false;
    
    long lastSpindlePos = spindleStepper.currentPosition();
    float gantryAccumulator = 0;

    bool movingRight = true;
    gantryStepper.setCurrentPosition(gantryStepper.currentPosition()); // reset planer position
    spindleStepper.setCurrentPosition(spindleStepper.currentPosition()); // reset planer position
    beepBuzzer();
    gantryStepper.setAcceleration(1000);
    spindleStepper.setAcceleration(300);
    gantryStepper.moveTo(startPos);
    beepBuzzer();

    lcd.clear();
    lcd.print(F("Moving to Star Pos"));
    lcd.setCursor(0,1);
    delay(5000);

    while (gantryStepper.distanceToGo() != 0 && !cancelled) {
        gantryStepper.run();
    }    


    lcd.clear();
    lcd.print("Winding coil:");
    
    spindleStepper.move(spindleTurns);
    gantryStepper.setCurrentPosition(gantryStepper.currentPosition());
    beepBuzzer();
    while (!cancelled){

        spindleStepper.run();
        if (spindleStepper.distanceToGo() == 0) break;   

        long spindlePos = spindleStepper.currentPosition();
        long delta = spindlePos - lastSpindlePos;
        lastSpindlePos = spindlePos;

        // pitch ratio
        gantryAccumulator += delta * gantryStepPerSpindleStep;

        while (abs(gantryAccumulator) >= 1.0) {
            gantryStepper.move((gantryAccumulator > 0) ? 1 : -1);
            gantryAccumulator += (gantryAccumulator > 0) ? -1.0 : 1.0;
            
        }
        gantryStepper.run();

        long gpos = gantryStepper.currentPosition();

        if (movingRight && gpos >= stopPos) {
            gantryStepPerSpindleStep = -abs(gantryStepPerSpindleStep);
            movingRight = false;
        }

        if (!movingRight && gpos <= startPos) {
            gantryStepPerSpindleStep = abs(gantryStepPerSpindleStep);
            movingRight = true;
        }

        char key = keypad.getKey();
        if (key){
            if (key == 'S'){
                cancelled = true;
                break;
            }
        }
    }
    
    // --- Done / Cancel ---
    spindleStepper.stop();
    gantryStepper.stop();
    gantryStepper.setCurrentPosition(gantryStepper.currentPosition());
    spindleStepper.setCurrentPosition(spindleStepper.currentPosition());
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print(gantryAccumulator);
    lcd.setCursor(0,0);
    if (cancelled) {
        lcd.print(F("Cancelled"));
        beepBuzzer(1000);
    } else {
        lcd.print(F("Winding Done!"));
        alarmBuzzer();
    }
    delay(2000);
}

// Used for recalling and displaying winding parameters saved in EEPROM memory
void save_setting(MenuComponent* menu_component) {
    CoilParam savedSettings[5];     // adjust if more slots are needed
    unsigned short numSaved = 0;
    unsigned short selected = 0;
    const byte itemsPerPage = 4;
    byte page = 0;

    // Load number of saved settings
    readEEPROM(numSaved, 2);

    if (numSaved == 0) {
        displayText(F("None Saved"));
        delay(2000);
        return;
    }

    // Preload settings into RAM
    for (int i = 0; i < numSaved; i++) {
        readEEPROM(savedSettings[i], 3 + i);
    }

    while (true) {
        // ---- Draw Menu Page ----
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Saved config:"));

        for (byte i = 0; i < itemsPerPage; i++) {
            int idx = page * itemsPerPage + i;
            if (idx >= numSaved) break;

            lcd.setCursor(0, i + 1);
            lcd.print((selected == idx) ? '>' : ' ');
            lcd.print(idx + 1);
            lcd.print(F(": "));
            lcd.print(savedSettings[idx].stopPos == -1 ? F("S,D ") : F("M,D "));
            lcd.print(savedSettings[idx].wireDia);
            lcd.print(F(",T"));
            lcd.print(savedSettings[idx].turnNum);
        }

        // ---- Input handling ----
        char key = keypad.getKey();
        if (key) beepBuzzer();

        if (key == 'D' && selected < numSaved - 1) {
            selected++;
            if (selected >= (page + 1) * itemsPerPage) page++;
        } 
        else if (key == 'U' && selected > 0) {
            selected--;
            if (selected < page * itemsPerPage) page--;
        } 
        else if (key == 'E') {
            // ---- Show Details ----
            while (true) {
                lcd.clear();
                lcd.setCursor(0,0);
                lcd.print(F("Turns:")); lcd.print(selected + 1);
                lcd.setCursor(0,1);
                lcd.print(savedSettings[selected].stopPos != -1 ? F("MultiLayer") : F("SingleLayer"));
                lcd.setCursor(0,2);
                lcd.print(F("WireDia:")); lcd.print(savedSettings[selected].wireDia);
                lcd.setCursor(0,3);
                lcd.print(F("TurnNum:")); lcd.print(savedSettings[selected].turnNum);

                // Wait for user action
                char k = keypad.getKey();
                if (k) beepBuzzer();

                if (k == 'E') {
                    // ---- Ask to Run ----
                    lcd.clear();
                    lcd.setCursor(0,0); lcd.print(F("Run setting?"));
                    lcd.setCursor(0,1); lcd.print(F("ENT=Run ESC=Back"));

                    const char* parentName = ms.get_current_menu()->get_name();
                    bool validSingle = (strcmp(parentName, "Single layer coil") == 0 && savedSettings[selected].stopPos == -1);
                    bool validMulti  = (strcmp(parentName, "Multi layer coil")  == 0 && savedSettings[selected].stopPos != -1);

                    if (validSingle || validMulti) {
                        while (true) {
                            char runKey = keypad.getKey();
                            if (runKey) beepBuzzer();
                            if (runKey == 'E') {
                                CoilParameters = savedSettings[selected];
                                windCoil(CoilParameters);
                                return;
                            } 
                            else if (runKey == 'S') break; // Back to details
                        }
                    } else {
                        displayText(F("Invalid param"));
                        delay(1200);
                    }
                } 
                else if (k == 'S') {
                    break; // exit details, go back to list
                }
            }
        } 
        else if (key == 'S') {
            return; // Exit function
        }
    }
}


void wireDia_set(MenuComponent* menu_component) {
  numeric_input(F("Set Wire Dia"), F("Dia (mm)"), CoilParameters.wireDia);
  displayText();
  delay(1000);
  return;
}

void turnNum_set(MenuComponent* menu_component) {
  numeric_input(F("Set Turn Num"),F("Turn Num"), CoilParameters.turnNum);
  displayText();
  delay(1000);
  return;
}

void turnSpace_set(MenuComponent* menu_component) {
  numeric_input(F("Set Turn spacing"),F("Space (mm)"), CoilParameters.turnSpace);
  displayText();
  delay(1000);
  return;
}


// Struct for generic parameter adjustment function

struct ParameterConfig {
    const __FlashStringHelper* title;   // Title text
    const __FlashStringHelper* unit;    // Display unit ("CM", "RPM", etc.)
    float initialValue;                  // Starting value
    long minValue;                      // Lower clamp
    long maxValue;                      // Upper clamp
    long stepSize;                      // Increment/decrement step
    bool incrementMode;                 // Increase in set steps vs continuous motion 
    void (*onSave)(long value);         // Callback for saving result
    void (*stepperRun) (long value);    // Callback for running stepper 
    float (*toDisplay)(long value);      // Optional conversion for display
    long (*fromDisplay)(float value);    // Optional conversion from user input
    float (*loopFunction)(long value);   // Runs each time in while loop 
};
// Generic parameter adjustment function
void adjustParameter(const struct ParameterConfig& cfg) {
    float value = cfg.initialValue;  
    float inputValue = (cfg.toDisplay) ? cfg.toDisplay(value) : value;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(cfg.title);
    lcd.setCursor(0,1);
    lcd.print(F("Value: "));
    lcd.print(inputValue);
    lcd.print(cfg.unit);
    lcd.setCursor(0, 2);
    lcd.print(F("< >:Manual ENT:Num"));
    lcd.setCursor(0, 3);
    lcd.print(F("ESC:Cancel SAVE:Save"));
    while (true) {
        char key = keypad.getKey();
        if (key) beepBuzzer();

        if (key == 'E') {
            // Numeric input mode
            inputValue = (cfg.toDisplay) ? cfg.toDisplay(value) : value;
            numeric_input(cfg.title, cfg.unit, inputValue);
            value = (cfg.fromDisplay) ? cfg.fromDisplay(inputValue) : inputValue;
    
            if (value < cfg.minValue) {
                value = cfg.minValue;
            }
            if (value > cfg.maxValue){
                value = cfg.maxValue;
            }
            if (cfg.onSave) cfg.onSave(value); 
            displayText();
            delay(1500);
            break;
        }
        else if (key == 'S') {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print(F("Cancelled"));
            delay(1000);
            break;
        }
        else if (key == 'C'){
            if (cfg.onSave) cfg.onSave(value);
            displayText();
            delay(1000);
            break;      
        }
        if (cfg.incrementMode){
            if (key == '<') {
                value -= cfg.stepSize;
                if (value < cfg.minValue) value = cfg.minValue;
                if (cfg.stepperRun) cfg.stepperRun(value); 
            } 
            else if (key == '>') {
                value += cfg.stepSize;
                if (value > cfg.maxValue) value = cfg.maxValue;
                if (cfg.stepperRun) cfg.stepperRun(value);
            } 
        }
        else {
            cfg.stepperRun(value);
            value = gantryStepper.currentPosition();

            bool minTriggered = (digitalRead(gantryLimMin) == LOW);
            bool maxTriggered = (digitalRead(gantryLimMax) == LOW);
        }

        // utility loop function 
        if (cfg.loopFunction) cfg.loopFunction(value);
    }
    gantry_jogActive = false;
    spindle_jogActive = false;
}



// start position 
void startPos_set(MenuComponent* menu_component) {
    gantry_jogActive = true;
    gantryStepper.setAcceleration(800);
    gantryStepper.setCurrentPosition(gantryStepper.currentPosition());
    ParameterConfig cfg = {
        F("Start Position"),
        F("CM"),
        CoilParameters.startPos,
        0,
        physicalParams.gantryLength,
        800, // step size
        false,
        [](long v){CoilParameters.startPos = v; },  // save callback
        [](long v){gantryStepper.run(); },
        stepsToCM,
        cmToSteps,
        nullptr    
        };
    adjustParameter(cfg);
}



// STOP POSITION
void stopPos_set(MenuComponent* menu_component) {
    gantry_jogActive = true;
    gantryStepper.setAcceleration(800);
    gantryStepper.setCurrentPosition(gantryStepper.currentPosition());
    ParameterConfig cfg = {
        F("Stop Position"),
        F("CM"),
        CoilParameters.stopPos,
        CoilParameters.startPos,
        physicalParams.gantryLength,
        800,
        false,
        [](long v){ CoilParameters.stopPos = v; },  
        [](long v){gantryStepper.run(); },
        stepsToCM_offset,
        cmToSteps_offset,
        nullptr
    };
    adjustParameter(cfg);
}


// SPINDLE SPEED
void spindleSpeed_set(MenuComponent* menu_component) {
    float steps_per_rev = physicalParams.spindleStepsPerRev * physicalParams.spindleMicrosteps * physicalParams.spindleGearRatio;
    spindleStepper.setSpeed(CoilParameters.spindleSpeed);
    float spindleSpeed = CoilParameters.spindleSpeed;
    float max = settings.spindleMaxSpeedValue;
    ParameterConfig cfg = {
        F("Spindle Speed"),
        F("RPM"),
        spindleSpeed,
        1,             // min (in steps)
        4000,  // max (in steps) 
        100,  // step size in steps 
        true,                              // step
        [](long v){CoilParameters.spindleSpeed = v; },
        [](long v){spindleStepper.setSpeed(v);  },
        stepsToRPM,                      // conversion
        rpmToSteps,                      // conversion
        [](long v){spindleStepper.runSpeed(); }                  // loop function

    };
    adjustParameter(cfg);
}

void accelSpeed_set(MenuComponent* menu_component) {
  float accelSpeedValue = settings.accelSpeedValue;
  numeric_input(F("Set Accel Spd"),F("Accel Spd"), accelSpeedValue);
  settings.accelSpeedValue = (long)accelSpeedValue;
  saveEEPROM(settings, settings_location); 
  delay(500);
}

void spindleMaxSpeed_set(MenuComponent* menu_component) {
    //float RPM_float = 0;

    float RPM = stepsToRPM(settings.spindleMaxSpeedValue);
    numeric_input(F("Spindle Max"),F("RPM"), RPM);
    // Optional: Clamp RPM to a reasonable range
    if (RPM < 1) RPM = 1;
    if (RPM > 10000) RPM = 10000; // Example upper limit
    long steps_per_s = rpmToSteps(RPM);
    settings.spindleMaxSpeedValue = steps_per_s;
    saveEEPROM(settings, settings_location);

    displayText();
    delay(2000);
}

void gantryMaxSpeed_set(MenuComponent* menu_component) {
  
  float gantryMaxSpeedValue = stepsToCM(settings.gantryMaxSpeedValue);
  numeric_input(F("Gantry Max"),F("cm/s"), gantryMaxSpeedValue);
  long steps_per_s = cmToSteps(gantryMaxSpeedValue);
  settings.gantryMaxSpeedValue = steps_per_s;
  saveEEPROM(settings, settings_location); // Save to EEPROM
  displayText();
  delay(2000);
  return;
}

void accelMode_set(MenuComponent* menu_component) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Accel Mode Set"));
    if (bool_input("Enable?", settings.accelMode)) {
        saveEEPROM(settings, settings_location); // Save to EEPROM
    }

  delay(1000);
}

//Checks that all winding perameters are valid before running

void start(MenuComponent* menu_component) {
    lcd.clear();
    bool run = true;
    bool valid = false;
    long coilLength = 0;

    // Parameter validation and coil length calculation
    if (true) {
        const char* parentName = ms.get_current_menu()->get_name();
        if (parentName == "Single layer coil") {
            valid = (CoilParameters.wireDia > 0 && CoilParameters.turnNum > 0 && CoilParameters.turnSpace >= 0 &&
                     CoilParameters.startPos >= 0 && CoilParameters.spindleSpeed > 0);
            coilLength = ((physicalParams.gantryStepsPerRev * physicalParams.gantryMicrosteps) / physicalParams.gantryPitch_mm)
                         * CoilParameters.turnNum * (CoilParameters.wireDia + CoilParameters.turnSpace);
            CoilParameters.stopPos = coilLength + CoilParameters.startPos;
        } else if (strcmp(parentName, "Multi layer coil") == 0) {
            valid = (CoilParameters.wireDia > 0 && CoilParameters.turnNum > 0 && CoilParameters.turnSpace >= 0 &&
                     CoilParameters.startPos >= 0 && CoilParameters.stopPos > CoilParameters.startPos && CoilParameters.spindleSpeed > 0);
        }
    }

    if (!valid) {
        displayText(F("Invalid Param"));
        delay(2000);
        return;
    }
    
    // Prompt to run
    displayText(F("Wind coil"));
    if (!bool_input("Run", run) || !run) {
        // User said no or cancelled, offer to exit or return
        return;
    }

    // Prompt for dry run
    // Dry run used to double check start and stop positions of coil
    bool dryRun = false;
    displayText(F("Parameter Test"));
    if (bool_input("Dry Run", dryRun) && dryRun) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Start Position"));
        gantryStepper.moveTo(CoilParameters.startPos);
        while (gantryStepper.distanceToGo() != 0) gantryStepper.run();
        delay(1000);
        beepBuzzer(500);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("End Position"));
        long endPos = (CoilParameters.stopPos == -1) ?
                        CoilParameters.startPos + coilLength
                        : CoilParameters.stopPos;
        gantryStepper.moveTo(endPos);
        while (gantryStepper.run());
        delay(1000);
        beepBuzzer();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Dry Run Complete"));
        delay(1000);

        // Prompt to start winding after dry run
        displayText(F("Start Winding?"));
        if (!bool_input("Start?", run) || !run) {
            return;
        }
    } 
    lcd.clear();
    lcd.setCursor(0, 0);
    bool save_config = false;
    bool_input("Save config", save_config);
    if (save_config) {
        // Save current CoilParameters to EEPROM
        unsigned short int num_savedSettings = 0;
        readEEPROM(num_savedSettings,numSavedSettings_location);
        saveEEPROM(CoilParameters, savedSettings_location + CoilParameters_size*num_savedSettings);
        saveEEPROM(num_savedSettings + 1, numSavedSettings_location); // Update count
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Config Saved!"));
        delay(1000);
    }
    
    // Start winding
    lcd.clear();
    beepBuzzer();
    windCoil(CoilParameters); 
}


void tensionSwi_set(MenuComponent* menu_component) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Tension Swi Set"));
  if (bool_input("Enable?", settings.tensionSwitchEnabled)) {
    saveEEPROM(settings, settings_location); // Save to EEPROM
  }
  displayText();
  delay(2000);
  return;
}

void homing_set(MenuComponent* menu_component) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Home gantry");
  if (bool_input("Home?", settings.tensionSwitchEnabled)) {
    homeGantry();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Homing done!");
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Homing cancelled");
  }
  delay(2000);
  return;
}

void version_set(MenuComponent* menu_component) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Version 1.0");
  lcd.setCursor(0,2);
  lcd.print("Copyright IsaacA");
  delay(4000);
}

// Jog function 
void move_set(MenuComponent* menu_component) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Jog Mode");
    lcd.setCursor(0, 3);
    lcd.print("ESC to Exit");

    gantryStepper.setAcceleration(800);
    spindleStepper.setAcceleration(800);

    spindle_jogActive = true;
    gantry_jogActive = true;

    bool jogging = true;

    while (jogging) {
        // -----------------------
        // LIMIT SWITCH PROTECTION
        // -----------------------
        bool minTriggered = (digitalRead(gantryLimMin) == LOW);
        bool maxTriggered = (digitalRead(gantryLimMax) == LOW);

        // If at min limit and moving left -> stop
        if (minTriggered && gantryStepper.targetPosition() < 0) {
            gantryStepper.setCurrentPosition(gantryStepper.currentPosition());
            gantryStepper.stop();
        }
        // If at max limit and moving right -> stop
        if (maxTriggered && gantryStepper.targetPosition() > 0) {
            gantryStepper.setCurrentPosition(gantryStepper.currentPosition());
            gantryStepper.stop();
        }

        // -----------------------
        // KEYPAD HANDLING
        // -----------------------
        char key = keypad.getKey();
        char lastKey = NO_KEY;
        KeyState state = keypad.getState();

        if (key == 'S' && state == PRESSED) {
            jogging = false;
            break;
        }

        // -----------------------
        // MOTOR RUN LOOP
        // -----------------------
        gantryStepper.run();
        spindleStepper.run();        
    }

    spindle_jogActive = false;
    gantry_jogActive = false;
    lcd.clear();
    lcd.print("Jog Ended");
    delay(1000);
}


void buzzer_set(MenuComponent* menu_component) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Buzzer Mode");
  if (bool_input("Enable?", settings.buzzerEnabled)) {
    saveEEPROM(settings, settings_location); // Save to EEPROM
  }
  displayText();
  delay(2000);
  return;
}

// Finds the maximum length of gantry
void gantryCal_set(MenuComponent* menu_component) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Gantry Calibrate");
  bool run = true;
  if (bool_input("Calibrate?", run) && run) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Calibrating...");
    delay(500);

    // Home the gantry first
    homeGantry();
    lcd.setCursor(0,1);
    lcd.print("Finding Gantry Max");
    // Move toward max limit switch
    gantryStepper.setSpeed(settings.gantryMaxSpeedValue); // Move far positive, will stop at switch
    while (digitalRead(gantryLimMax) == HIGH ) { // Not triggered
        gantryStepper.run();
    }
    gantryStepper.stop();
    beepBuzzer(500);
    // Record position as gantry length
    long length = gantryStepper.currentPosition();
    physicalParams.gantryLength = length;
    saveEEPROM(physicalParams.gantryLength, 0); // Save to EEPROM
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Gantry Len: ");
    lcd.setCursor(0, 1);
    lcd.print(stepsToCM(length));
    lcd.print("CM");
    delay(2000);
    homeGantry();// Return to home after calibration
  }
  return;
}
void settingRecal(){
    readEEPROM(physicalParams.gantryLength,0);
    readEEPROM(settings,1);
}

void startText(){
    lcd.clear();
    lcd.print(F("     Automated"));
    lcd.setCursor(0,1);
    lcd.print(F("    Coil Winder"));
    lcd.setCursor(0,2);
    lcd.print(F("    Version 1.0"));
    lcd.setCursor(0,3);
    lcd.print(F(" Designed by IsaacA"));
    delay(5000);
}

//Links key presses to display commands 
void keyHandler(){
    char key = keypad.getKey();
    if (key) {
    beepBuzzer();
    switch (key) {
      case 'U': // 'UP'
        ms.prev();
        ms.display();
        break;
      case 'D': // 'DOWN'
        ms.next();
        ms.display();
        break;
      case 'E': // 'ENT'
        ms.select();
        ms.display();
        break;
      case 'S': // 'ESC'
        ms.back();
        ms.display();
        break;
    }
  }

}
void setup() {  
  attachInterrupt(digitalPinToInterrupt(eStopPin), eStop, RISING);
  beepBuzzer();
  lcd.init();
  lcd.backlight();
  pinMode(spindleDir, OUTPUT);
  pinMode(spindleStep, OUTPUT);
  pinMode(gantryDir, OUTPUT);
  pinMode(gantryStep, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(tensionSwi, INPUT_PULLUP);
  pinMode(eStopPin, INPUT);
  pinMode(gantryLimMax, INPUT_PULLUP);
  pinMode(gantryLimMin, INPUT_PULLUP);
  pinMode(LCD_SDA, OUTPUT); 
  pinMode(LCD_SCL, OUTPUT); 

    // Initialize the menu system
    //adding menus
  ms.get_root_menu().add_menu(&singleLayCoil);
  ms.get_root_menu().add_menu(&multiLayCoil);
  ms.get_root_menu().add_menu(&tranformerWind);
  ms.get_root_menu().add_menu(&jog);
  ms.get_root_menu().add_menu(&cal);
  ms.get_root_menu().add_menu(&setting);

    //adding menu items
  singleLayCoil.add_item(&settingsSave);
  singleLayCoil.add_item(&wireDia);
  singleLayCoil.add_item(&turnNum);
  singleLayCoil.add_item(&turnSpace);
  singleLayCoil.add_item(&startPos);
  singleLayCoil.add_item(&spindleSpeed);
  singleLayCoil.add_item(&run);

  multiLayCoil.add_item(&settingsSave);
  multiLayCoil.add_item(&wireDia);
  multiLayCoil.add_item(&turnNum);
  multiLayCoil.add_item(&turnSpace);
  multiLayCoil.add_item(&startPos);
  multiLayCoil.add_item(&stopPos);
  multiLayCoil.add_item(&spindleSpeed);
  multiLayCoil.add_item(&run);

  setting.add_item(&version);
  setting.add_item(&spindleMaxSpeed);
  setting.add_item(&gantryMaxSpeed);
  setting.add_item(&tensionSwiMode);
  setting.add_menu(&gradualAccel);
  setting.add_item(&buzzerMode);
  
  gradualAccel.add_item(&accelSpeed);
  gradualAccel.add_item(&accelMode);

  jog.add_item(&homing);
  jog.add_item(&move);

  cal.add_item(&gantryCal);



  spindleStepper.setMaxSpeed(settings.spindleMaxSpeedValue);
  gantryStepper.setMaxSpeed(settings.gantryMaxSpeedValue);
  startText();
  settingRecal();
  physicalParams.gantryLength = 1000;
  ms.display();
  keypad.addEventListener(motorJog);
  
   
  
}


void loop(){
        // Runs when emergency interupt triggerd 
      if (emergencyStopTriggered) {
        int eStop = LOW; // Set emergency stop flag
        spindleStepper.stop();
        gantryStepper.stop();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Emergency Stop!");
        lcd.setCursor(0, 3);
        lcd.print("ESC to reset");
        alarmBuzzer(1000);
        while (keypad.getKey() != 'S' || eStop == HIGH) { delay(100); eStop = digitalRead(2);} // Wait for reset
        emergencyStopTriggered = false;
        beepBuzzer(500);
    }
    keyHandler();
}



