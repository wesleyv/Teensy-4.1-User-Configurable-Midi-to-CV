#include <SPI.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <MIDI.h>

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

#define DAC1 41
#define DAC2 40
#define DAC3 39
#define DAC4 38
#define GATE_CH0 37
#define GATE_CH1 36
#define GATE_CH2 35
#define GATE_CH3 34

int GatePins[] = { 37, 36, 35, 34 };

#define PRESET_SELECT_A 29
#define PRESET_SELECT_B 28

#define ENC_A 31
#define ENC_B 32
#define ENC_BTN 30
#define OLED_RESET 17
#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels
#define NOTE_SF 47.069f

#define MIDI_TYPE_NOTE_OFF 0x80
#define MIDI_TYPE_NOTE_ON 0x90
#define MIDI_TYPE_CC 0xB0
#define MIDI_TYPE_PB 0xE0
#define MIDI_TYPE_AT 0xD0
#define MIDI_TYPE_PC 0xC0

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int PresetValueNew = 0;
int PresetValueOld = 0;
int encoderPos, encoderPosPrev;

enum Menu {
  OUTPUT_SETTINGS,
  CV_FUNCTION,
  CV_MIDICH,
  CV_RESPONSE,
  CV_SF_ADJUST,
  CV_SELECT,
  CC_NO,
  CV_PBRANGE,
  CV_CONFIG,
  GATE_SELECT,
  GATE_CONFIG,
  GATE_FUNCTION,
  GATE_CHANNEL,
  PRESET_SAVE,
  MAIN_MIDI_CH_SET,
  MIDI_MONITOR,
  PRESET_DISPLAY,
  CV_MIDI_LEARN,
  GATE_MIDI_LEARN,
  MAIN_MIDI_LEARN
} menu;

const char *CVFuncList[] = { "NOTE", "CC", "VELOCITY", "MOD", "PITCH BEND", "GATE", "TRIGGER", "AFTERTOUCH", "LEARN MODE" };
const char *CVRespList[] = { "HI", "LOW", "LST" };
const char *GateFuncList[] = { "GATE", "TRIGGER", "ALWAYS ON", "ALWAYS OFF", "LEARN MODE" };
const char *TrigLetterList[] = { "A", "B", "C", "D" };  // add more letters to the list to match the number of Gate/Trigger ouputs
uint8_t pitchBendChan;
uint8_t SelectedCV;
uint8_t SelectedGate;
uint8_t SelectedPreset;
uint8_t initialGate;
uint8_t initialCV;

const int trigDuration = 20;           // number of mS a trigger pulse will last
const int NumberOfCV = 8;              //put the number of CV channels in your circuit
const int NumberOfGate = 4;            //put the number of trig/gate channels in your circuit
const int NumberOfPreset = 20;         //Choose however many preset slots you want - enabling midi preset selection will allow you to access them even if the physical UI can not. Do not exceed your number of EEPROM addresses. At least 20 should be fine
const int NumberOfHardwarePreset = 3;  //Enter the number of presets that can be selected from your swith or other hardware UI
// EEPROM Addresses
const int ADDR_GATE_FUNCTION = 0;
const int ADDR_GATE_MIDICH = NumberOfGate * NumberOfPreset;
const int ADDR_CV_FUNCTION = ADDR_GATE_MIDICH + (NumberOfGate * NumberOfPreset);
const int ADDR_CV_RESPONSE = ADDR_CV_FUNCTION + (NumberOfCV * NumberOfPreset);
const int ADDR_CC_NO = ADDR_CV_RESPONSE + (NumberOfCV * NumberOfPreset);
const int ADDR_CV_MIDICH = ADDR_CC_NO + (NumberOfCV * NumberOfPreset);
const int ADDR_CV_PBRANGE = ADDR_CV_MIDICH + (NumberOfCV * NumberOfPreset);
const int ADDR_CV_SF_ADJUST = ADDR_CV_PBRANGE + (NumberOfCV * NumberOfPreset);
const int ADDR_MAIN_MIDI_CH = ADDR_CV_SF_ADJUST + (NumberOfCV * NumberOfPreset);
const int ADDR_REMOTE_PRESET = ADDR_MAIN_MIDI_CH + 1;

int CurrentGateFunc[NumberOfGate];
int CurrentGateCh[NumberOfGate];
int CurrentCVFunc[NumberOfCV];
int CurrentCCNO[NumberOfCV];
int CurrentCVResp[NumberOfCV];
int CurrentCVPBRange[NumberOfCV];
int CurrentCVCh[NumberOfCV];
float PBOffset[NumberOfCV];
int PrevNoteBuffer[NumberOfCV];
float sfAdj[NumberOfCV];
bool gateOverride[15] = { false };
int mainMidiCH;
bool remotePC;  // change this to be uninitialized later
int setCh;
int menuInitialValue;

bool notes[16][88] = { 0 }, initial_loop = 1;
#define MAX_HELD_NOTES 16
int8_t noteOrder[15][MAX_HELD_NOTES];
int8_t noteCount[15] = { 0 };

bool learnMode = false;
bool PresetDisplay = false;
bool highlightEnabled = false;  // Flag indicating whether highighting should be enabled on menu
unsigned long int highlightTimer = 0;
#define HIGHLIGHT_TIMEOUT 20000  // Highlight disappears after 20 seconds.  Timer resets whenever encoder turned or button pushed

unsigned long currentMillis = 0;
unsigned long trigTimer[NumberOfGate] = { 0 };
unsigned long cvTrigTimer[NumberOfCV] = { 0 };

void setup() {
  for (int pin = 1; pin <= 54; pin++) {
    pinMode(pin, OUTPUT);
  }
  pinMode(GATE_CH0, OUTPUT);
  pinMode(GATE_CH1, OUTPUT);
  pinMode(GATE_CH2, OUTPUT);
  pinMode(GATE_CH3, OUTPUT);
  pinMode(DAC1, OUTPUT);
  pinMode(DAC2, OUTPUT);
  pinMode(DAC3, OUTPUT);
  pinMode(DAC4, OUTPUT);
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);
  pinMode(PRESET_SELECT_A, INPUT_PULLUP);
  pinMode(PRESET_SELECT_B, INPUT_PULLUP);

  digitalWrite(GATE_CH0, LOW);
  digitalWrite(GATE_CH1, LOW);
  digitalWrite(GATE_CH2, LOW);
  digitalWrite(GATE_CH3, LOW);
  digitalWrite(DAC1, HIGH);
  digitalWrite(DAC2, HIGH);
  digitalWrite(DAC3, HIGH);
  digitalWrite(DAC4, HIGH);

  SPI.begin();
  Wire.begin();
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.turnThruOff();
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // OLED I2C Address, may need to change for different device,

  // Initialize EEPROM
  for (int i = ADDR_GATE_FUNCTION; i <= (ADDR_GATE_FUNCTION + ((NumberOfGate * NumberOfPreset) - 1)); i++) {
    if (EEPROM.read(i) > 3) EEPROM.write(i, 0);
  }
  for (int i = ADDR_GATE_MIDICH; i <= (ADDR_GATE_MIDICH + ((NumberOfGate * NumberOfPreset) - 1)); i++) {
    if (EEPROM.read(i) > 20) EEPROM.write(i, 0);
  }
  for (int i = ADDR_CV_FUNCTION; i <= (ADDR_CV_FUNCTION + ((NumberOfCV * NumberOfPreset) - 1)); i++) {
    if (EEPROM.read(i) > 7) EEPROM.write(i, 0);
  }
  for (int i = ADDR_CV_RESPONSE; i <= (ADDR_CV_RESPONSE + ((NumberOfCV * NumberOfPreset) - 1)); i++) {
    if (EEPROM.read(i) > 2) EEPROM.write(i, 0);
  }
  for (int i = ADDR_CV_MIDICH; i <= (ADDR_CV_MIDICH + ((NumberOfCV * NumberOfPreset) - 1)); i++) {
    if (EEPROM.read(i) > 20) EEPROM.write(i, 0);
  }
  for (int i = ADDR_CC_NO; i <= (ADDR_CC_NO + ((NumberOfCV * NumberOfPreset) - 1)); i++) {
    if (EEPROM.read(i) > 126) EEPROM.write(i, 0);
  }
  for (int i = ADDR_CV_PBRANGE; i <= (ADDR_CV_PBRANGE + ((NumberOfCV * NumberOfPreset) - 1)); i++) {
    if (EEPROM.read(i) > 24) EEPROM.write(i, 0);
  }
  for (int i = 0; i < NumberOfCV; i++) {
    EEPROM.get(ADDR_CV_SF_ADJUST + i * sizeof(float), sfAdj[i]);
    if ((sfAdj[i] < 0.8f) || (sfAdj[i] > 1.2f) || isnan(sfAdj[i])) sfAdj[i] = 1.0f;
  }
  if (EEPROM.read(ADDR_MAIN_MIDI_CH) > 16) EEPROM.write(ADDR_MAIN_MIDI_CH, 0);
  if (EEPROM.read(ADDR_REMOTE_PRESET) > 1) EEPROM.write(ADDR_MAIN_MIDI_CH, 0);
  currentfunctionupdate();
  menu = OUTPUT_SETTINGS;
  updateSelection();
}

void loop() {
  currentMillis = millis();
  encoderTimerHandler();
  highlightTimerHandler();
  gateTriggerTimerHandler();
  presetSwitchHandler();
  encButtonHandler();
  int8_t channel, data1, data2;
  if (usbMIDI.read()) {
    byte type = usbMIDI.getType();
    channel = usbMIDI.getChannel() - 1;
    data1 = usbMIDI.getData1();
    data2 = usbMIDI.getData2();
    if (learnMode == false) handleMIDIMessage(type, channel, data1, data2, true);
    else midiLearnHandler(type, channel, data1);
  }
  if (MIDI.read()) {
    byte type = MIDI.getType();
    channel = MIDI.getChannel() - 1;
    data1 = MIDI.getData1();
    data2 = MIDI.getData2();
    if (learnMode == false) handleMIDIMessage(type, channel, data1, data2, false);
    else midiLearnHandler(type, channel, data1);
  }
}

void midiLearnHandler(byte type, int8_t channel, int8_t data1) {
  bool doChannelSet = false;
  switch (menu) {
    case CV_MIDI_LEARN:
      switch (type) {
        case MIDI_TYPE_NOTE_OFF:
        case MIDI_TYPE_NOTE_ON:
          EEPROM.write(ADDR_CV_FUNCTION + SelectedCV + (NumberOfCV * PresetValueNew), 0);
          doChannelSet = true;
          break;
        case MIDI_TYPE_CC:
          if (data1 == 1) {
            EEPROM.write(ADDR_CV_FUNCTION + SelectedCV + (NumberOfCV * PresetValueNew), 3);
            doChannelSet = true;
          } else {
            EEPROM.write(ADDR_CV_FUNCTION + SelectedCV + (NumberOfCV * PresetValueNew), 1);
            EEPROM.write(ADDR_CC_NO + SelectedCV + (NumberOfCV * PresetValueNew), data1);
            doChannelSet = true;
          }
          break;
        case MIDI_TYPE_PB:
          EEPROM.write(ADDR_CV_FUNCTION + SelectedCV + (NumberOfCV * PresetValueNew), 4);
          doChannelSet = true;
          break;
        case MIDI_TYPE_AT:
          EEPROM.write(ADDR_CV_FUNCTION + SelectedCV + (NumberOfCV * PresetValueNew), 7);
          doChannelSet = true;
          break;
        default:
          break;
      }
      if (doChannelSet == true) {
        if (channel == mainMidiCH) EEPROM.write(ADDR_CV_MIDICH + SelectedCV + (NumberOfCV * PresetValueNew), 17);
        else EEPROM.write(ADDR_CV_MIDICH + SelectedCV + (NumberOfCV * PresetValueNew), channel);
      }
      learnMode = false;
      menu = CV_CONFIG;
      break;

    case GATE_MIDI_LEARN:
      if (type == MIDI_TYPE_NOTE_ON || type == MIDI_TYPE_NOTE_OFF) {  //maybe change this to just midi note on
        if (channel == mainMidiCH) EEPROM.write(ADDR_GATE_MIDICH + SelectedGate + (NumberOfGate * PresetValueNew), 17);
        else EEPROM.write(ADDR_GATE_MIDICH + SelectedGate + (NumberOfGate * PresetValueNew), channel);
        if (EEPROM.read(ADDR_GATE_FUNCTION + SelectedGate + (NumberOfGate * PresetValueNew)) > 1) EEPROM.write(ADDR_GATE_FUNCTION + SelectedGate + (NumberOfGate * PresetValueNew), 0);
      }
      learnMode = false;
      menu = GATE_CONFIG;
      break;

    case MAIN_MIDI_LEARN:
      EEPROM.write(ADDR_MAIN_MIDI_CH, channel);
      learnMode = false;
      menu = OUTPUT_SETTINGS;
      break;
    default:
      break;
  }
  updateSelection();
  currentfunctionupdate();
}

void handleMIDIMessage(byte type, int8_t channel, int8_t data1, int8_t data2, bool isUSB) {
  switch (type) {
    case MIDI_TYPE_NOTE_OFF:
    case MIDI_TYPE_NOTE_ON:
      data1 = data1 - 21;  // A0 = 21, Top Note = 108
      if ((data1 < 0) || (data1 > 87)) return;
      if (type == MIDI_TYPE_NOTE_OFF) {
        data2 = 0;
      }
      if (type == MIDI_TYPE_NOTE_ON || MIDI_TYPE_NOTE_OFF) {
        midiNoteHandler(data1, data2, channel);
        midiVelHandler(data2, channel);
        midiGateHandler(data2, channel);
      }
      break;
    case MIDI_TYPE_CC:
      midiCCHandler(data1, data2, channel);
      break;
    case MIDI_TYPE_PB:
      midiPBHandler(data1, data2, channel);
      break;
    case MIDI_TYPE_AT:
      midiAfterTouchHandler(data1, channel);
      break;
    case MIDI_TYPE_PC:
      midiPCHandler(data1, channel);
      break;
  }
  if (menu == MIDI_MONITOR) {
    midiMonitor(type, channel, data1, data2, isUSB ? 1 : 0);
  }
}

void midiPCHandler(int8_t pcNumber, int8_t channel) {
  if (channel == mainMidiCH && remotePC == true && pcNumber < NumberOfPreset) {
    presetSelectHandler(pcNumber, false);
  }
}

void midiMonitor(int8_t type, int8_t channel, int8_t data1, int8_t data2, bool isUSB) {
  display.clearDisplay();
  display.setCursor(20, 0);
  display.setTextSize(2);
  display.setTextColor(WHITE, BLACK);
  display.println(F("MONITOR"));
  display.setTextSize(1);
  // Show source
  display.print(F("SOURCE: "));
  display.println(isUSB ? F("USB MIDI") : F("5 PIN MIDI"));
  // Mask message type
  uint8_t messageType = type & 0xF0;
  // Show MIDI type
  display.print(F("TYPE: "));
  display.println(getMidiTypeName(messageType));
  // Show channel (adjusted to 1–16)
  display.print(F("CHANNEL: "));
  //display.println((type & 0x0F) + 1);
  display.println(channel + 1);
  // Interpret based on message type
  switch (messageType) {
    case 0x80:  // Note Off
    case 0x90:  // Note On
      display.print(F("NOTE: "));
      display.print(getNoteName(data1));
      display.print(F(" ("));
      display.print(data1);
      display.println(F(")"));
      display.print(F("VELOCITY: "));
      display.println(data2);
      break;

    case 0xB0:  // Control Change
      display.print(F("CONTROL #: "));
      display.println(data1);
      if (data1 == 123) display.println(F("(All NOTES OFF)"));
      if (data1 == 120) display.println(F("(All SOUND OFF)"));
      display.print(F("VALUE: "));
      display.print(data2);
      break;

    case 0xC0:  // Program Change
      display.print(F("PROGRAM #: "));
      display.println(data1);
      break;

    default:
      display.print(F("DATA1: "));
      display.println(data1);
      display.print(F("DATA2: "));
      display.println(data2);
      break;
  }

  display.display();
}

const char *getMidiTypeName(uint8_t type) {
  switch (type) {
    case 0x80: return "Note Off";
    case 0x90: return "Note On";
    case 0xA0: return "Poly Aftertouch";
    case 0xB0: return "Control Change";
    case 0xC0: return "Program Change";
    case 0xD0: return "Channel Aftertouch";
    case 0xE0: return "Pitch Bend";
    default: return "Unknown";
  }
}

String getNoteName(uint8_t noteNumber) {
  const char *noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
  uint8_t noteIndex = noteNumber % 12;
  int8_t octave = (noteNumber / 12) - 1;  // MIDI note 60 = C4
  return String(noteNames[noteIndex]) + String(octave);
}

void midiNoteHandler(int8_t noteMsg, int8_t velocity, int8_t channel) {
  for (int i = 0; i < NumberOfCV; i++) {
    if (matchesNoteRule(CurrentCVFunc[i], CurrentCVCh[i], channel)) {
      if (velocity == 0) {
        notes[channel][noteMsg] = false;
        for (int j = 0; j < noteCount[channel]; j++) {
          if (noteOrder[channel][j] == noteMsg) {
            // Shift remaining notes left
            for (int k = j; k < noteCount[channel] - 1; k++) {
              noteOrder[channel][k] = noteOrder[channel][k + 1];
            }
            noteCount[channel]--;
            break;
          }
        }
      } else {
        notes[channel][noteMsg] = true;
        bool exists = false;
        for (int j = 0; j < noteCount[channel]; j++) {
          if (noteOrder[channel][j] == noteMsg) {
            exists = true;
            break;
          }
        }
        if (!exists && noteCount[channel] < MAX_HELD_NOTES) {
          noteOrder[channel][noteCount[channel]] = noteMsg;
          noteCount[channel]++;
        }
      }
      switch (CurrentCVResp[i]) {
        case 0:
          hiNoteHandler(channel, i);
          break;
        case 1:
          lowNoteHandler(channel, i);
          break;
        case 2:
          //if (notes[channel][noteMsg]) {
          //  orderIndx[channel] = (orderIndx[channel] + 1) % 10;
          //  noteOrder[channel][orderIndx[channel]] = noteMsg;
          //}
          lstNoteHandler(channel, i);
          break;
      }
    }
  }
}

void lstNoteHandler(int8_t channel, int dacAddress) {
  uint16_t dacOutputLevel;
  gateOverride[channel] = false;
  // Start from last held note (most recent)
  for (int i = noteCount[channel] - 1; i >= 0; i--) {
    int8_t noteIndx = noteOrder[channel][i];
    if (notes[channel][noteIndx]) {
      dacOutputLevel = (unsigned int)(((float)noteIndx + PBOffset[dacAddress]) * NOTE_SF * sfAdj[dacAddress] + 0.5);
      PrevNoteBuffer[dacAddress] = noteIndx;
      setVoltage(dacAddress, dacOutputLevel);
      gateOverride[channel] = true;
      return;
    }
  }
}

void hiNoteHandler(int8_t channel, int dacAddress) {
  uint16_t dacOutputLevel;
  int topNote = 0;
  bool noteActive = false;
  for (int i = 0; i < 88; i++) {
    if (notes[channel][i]) {
      topNote = i;
      noteActive = true;
    }
  }
  if (noteActive) {
    dacOutputLevel = (unsigned int)(((float)topNote + PBOffset[dacAddress]) * NOTE_SF * sfAdj[dacAddress] + 0.5);
    PrevNoteBuffer[dacAddress] = topNote;
    setVoltage(dacAddress, dacOutputLevel);
    gateOverride[channel] = true;
  } else {
    gateOverride[channel] = false;
  }
}

void lowNoteHandler(int8_t channel, int dacAddress) {
  uint16_t dacOutputLevel;
  int bottomNote = 0;
  bool noteActive = false;
  for (int i = 87; i >= 0; i--) {
    if (notes[channel][i]) {
      bottomNote = i;
      noteActive = true;
    }
  }
  if (noteActive) {
    dacOutputLevel = (unsigned int)(((float)bottomNote + PBOffset[dacAddress]) * NOTE_SF * sfAdj[dacAddress] + 0.5);
    PrevNoteBuffer[dacAddress] = bottomNote;
    setVoltage(dacAddress, dacOutputLevel);
    gateOverride[channel] = true;
  } else {
    gateOverride[channel] = false;
  }
}

bool matchesNoteRule(uint8_t currentCVfunction, uint8_t currentCVChannel, uint8_t msgChannel) {
  if (currentCVfunction != 0) return false;
  if (currentCVChannel == msgChannel) return true;
  if (currentCVChannel == 16) return true;  // gate set to omni
  if (currentCVChannel > 16 && (currentCVChannel - 17 == msgChannel - mainMidiCH)) return true;
  if (mainMidiCH == 16) return true;  //main channel omni
  return false;
}

void midiGateHandler(int8_t velocity, int8_t channel) {
  for (int i = 0; i < NumberOfGate; i++) {
    if (matchesGateRule(CurrentGateFunc[i], CurrentGateCh[i], channel)) {
      if (velocity == 0 && gateOverride[channel] == false) digitalWrite(GatePins[i], LOW);
      if (velocity != 0) {
        digitalWrite(GatePins[i], HIGH);
        trigTimer[i] = currentMillis;
      }
    }
  }
  for (int i = 0; i < NumberOfCV; i++) {
    if (matchesCVasGateRule(CurrentCVFunc[i], CurrentCVCh[i], channel)) {
      if (velocity == 0 && gateOverride[channel] == false) {
        uint16_t dacOutputLevel = 0;
        setVoltage(i, dacOutputLevel);
      }
      if (velocity != 0) {
        uint16_t dacOutputLevel = 2825;
        setVoltage(i, dacOutputLevel);
        cvTrigTimer[i] = currentMillis;
      }
    }
  }
}

bool matchesGateRule(uint8_t currentGatefunction, uint8_t currentGateChannel, uint8_t msgChannel) {
  if (currentGatefunction > 1) return false;
  if (currentGateChannel == msgChannel) return true;
  if (currentGateChannel == 16) return true;  // gate set to omni
  if (currentGateChannel > 16 && (currentGateChannel - 17 == msgChannel - mainMidiCH)) return true;
  if (mainMidiCH == 16) return true;  //main channel omni
  return false;
}

bool matchesCVasGateRule(uint8_t currentCVfunction, uint8_t currentCVChannel, uint8_t msgChannel) {
  if (currentCVfunction != 5 && currentCVfunction != 6) return false;
  if (currentCVChannel == msgChannel) return true;
  if (currentCVChannel == 16) return true;  // gate set to omni
  if (currentCVChannel > 16 && (currentCVChannel - 17 == msgChannel - mainMidiCH)) return true;
  if (mainMidiCH == 16) return true;  //main channel omni
  return false;
}

void midiPBHandler(int8_t data1, int8_t data2, int8_t channel) {
  uint16_t pitchBend = (data2 << 7) | data1;
  uint16_t dacOutputLevel;
  for (int i = 0; i < NumberOfCV; i++) {  // updates the dedicated pitch bend-only outputs
    if (matchesPBRule(CurrentCVFunc[i], CurrentCVCh[i], channel)) {
      dacOutputLevel = pitchBend >> 2;  // this is a simplified way of scaling the 14 bit pitch bend value into the 12 bit DAC value
      setVoltage(i, dacOutputLevel);
    }
    if (matchesNoteRule(CurrentCVFunc[i], CurrentCVCh[i], channel)) {  //updates existing notes with single wire pitch bend
      float centeredBend = pitchBend - 8192.0f;
      PBOffset[i] = (centeredBend / 8192.0f) * CurrentCVPBRange[i];
      dacOutputLevel = (unsigned int)(((float)PrevNoteBuffer[i] + PBOffset[i]) * NOTE_SF * sfAdj[i] + 0.5);
      setVoltage(i, dacOutputLevel);
    }
  }
}

bool matchesPBRule(uint8_t currentCVfunction, uint8_t currentCVChannel, uint8_t msgChannel) {
  if (currentCVfunction != 4) return false;
  if (currentCVChannel == msgChannel) return true;
  if (currentCVChannel == 16) return true;  // gate set to omni
  if (currentCVChannel > 16 && (currentCVChannel - 17 == msgChannel - mainMidiCH)) return true;
  if (mainMidiCH == 16) return true;  //main channel omni
  return false;
}

void midiAfterTouchHandler(int8_t touchValue, int8_t channel) {
  uint16_t dacOutputLevel;
  for (int i = 0; i < NumberOfCV; i++) {
    if (matchesAfterTouchRule(CurrentCVFunc[i], CurrentCVCh[i], channel)) {
      dacOutputLevel = (32 * touchValue);  // this is a simplified way of scaling the 4095 12 bit dac value to the 7 bit midi value
      setVoltage(i, dacOutputLevel);
    }
  }
}

bool matchesAfterTouchRule(uint8_t currentCVfunction, uint8_t currentCVChannel, uint8_t msgChannel) {
  if (currentCVfunction != 7) return false;
  if (currentCVChannel == msgChannel) return true;
  if (currentCVChannel == 16) return true;  // gate set to omni
  if (currentCVChannel > 16 && (currentCVChannel - 17 == msgChannel - mainMidiCH)) return true;
  if (mainMidiCH == 16) return true;  //main channel omni
  return false;
}

void midiVelHandler(int8_t velocity, int8_t channel) {
  uint16_t dacOutputLevel;
  for (int i = 0; i < NumberOfCV; i++) {
    if (matchesVelRule(CurrentCVFunc[i], CurrentCVCh[i], velocity, channel)) {
      dacOutputLevel = (32 * velocity);  // this is a simplified way of scaling the 4095 12 bit dac value to the 7 bit midi value
      setVoltage(i, dacOutputLevel);
    }
  }
}

bool matchesVelRule(uint8_t currentCVfunction, uint8_t currentCVChannel, int8_t velocity, uint8_t msgChannel) {
  if (currentCVfunction != 2) return false;
  if (velocity == 0) return false;
  if (currentCVChannel == msgChannel) return true;
  if (currentCVChannel == 16) return true;  // gate set to omni
  if (currentCVChannel > 16 && (currentCVChannel - 17 == msgChannel - mainMidiCH)) return true;
  if (mainMidiCH == 16) return true;  //main channel omni
  return false;
}

void midiCCHandler(int8_t ccNumber, int8_t ccValue, int8_t channel) {
  uint16_t dacOutputLevel;
  for (int i = 0; i < NumberOfCV; i++) {
    if (matchesCCRule(CurrentCVFunc[i], CurrentCVCh[i], CurrentCCNO[i], ccNumber, channel)) {
      dacOutputLevel = (32 * ccValue);  // this is a simplified way of scaling the 4095 12 bit dac value to the 7 bit midi value
      setVoltage(i, dacOutputLevel);
      if ((ccNumber == 123 || ccNumber == 120) && ccValue == 0) midiGateHandler(0, channel);
    }
  }
}

bool matchesCCRule(uint8_t currentCVfunction, uint8_t currentCVChannel, uint8_t currentCCNumber, uint8_t msgCCNumber, uint8_t msgChannel) {
  if (currentCVfunction != 1 && currentCVfunction != 3) return false;
  if (currentCVfunction == 1 && currentCCNumber != msgCCNumber) return false;
  if (currentCVfunction == 3 && msgCCNumber != 1) return false;
  if (currentCVChannel == msgChannel) return true;
  if (currentCVChannel == 16) return true;  // gate set to omni
  if (currentCVChannel > 16 && (currentCVChannel - 17 == msgChannel - mainMidiCH)) return true;
  if (mainMidiCH == 16) return true;  //main channel omni
  return false;
}

void setVoltage(int dacOutputChannel, uint16_t dacOutputLevel) {
  int dacpin;       //comment this back in if shit stops working
  bool dacchannel;  //comment this back in if shit stops working
  switch (dacOutputChannel) {
    case 0:
      dacpin = DAC1;
      dacchannel = 0;
      break;
    case 1:
      dacpin = DAC1;
      dacchannel = 1;
      break;
    case 2:
      dacpin = DAC2;
      dacchannel = 0;
      break;
    case 3:
      dacpin = DAC2;
      dacchannel = 1;
      break;
    case 4:
      dacpin = DAC3;
      dacchannel = 0;
      break;
    case 5:
      dacpin = DAC3;
      dacchannel = 1;
      break;
    case 6:
      dacpin = DAC4;
      dacchannel = 0;
      break;
    case 7:
      dacpin = DAC4;
      dacchannel = 1;
      break;
    default:
      return;  // Early exit to prevent undefined behavior
  }
  int command = dacchannel ? 0x9000 : 0x1000;
  command |= 1 ? 0x0000 : 0x2000;  // sets gain for maximum output
  command |= (dacOutputLevel & 0x0FFF);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(dacpin, LOW);
  SPI.transfer(command >> 8);
  SPI.transfer(command & 0xFF);
  digitalWrite(dacpin, HIGH);
  SPI.endTransaction();
}

void updateMenu() {                                                          // Called whenever button is pushed
  if (highlightEnabled || menu == MIDI_MONITOR || menu == PRESET_DISPLAY) {  // Highlight is active, choose selection
    switch (menu) {
      case OUTPUT_SETTINGS:
        switch (mod(encoderPos, 5)) {
          case 0:
            initialCV = 0;
            menu = CV_SELECT;
            break;
          case 1:
            initialGate = 0;
            menu = GATE_SELECT;
            break;
          case 2:
            menu = PRESET_SAVE;
            menuInitialValue = PresetValueNew;
            break;
          case 3:
            menu = MAIN_MIDI_CH_SET;
            break;
          case 4:
            menu = MIDI_MONITOR;
            break;
        }
        break;

      case MAIN_MIDI_CH_SET:
        setCh = mod(menuInitialValue + encoderPos, 19);
        if (mod(menuInitialValue + encoderPos, 19) == 17) {
          remotePC = !remotePC;
          EEPROM.write(ADDR_REMOTE_PRESET, remotePC);
          currentfunctionupdate();
          menu = OUTPUT_SETTINGS;
        } else if (mod(menuInitialValue + encoderPos, 19) == 18) {
          learnMode = true;
          menu = MAIN_MIDI_LEARN;
        } else {
          EEPROM.write(ADDR_MAIN_MIDI_CH, setCh);
          currentfunctionupdate();
          menu = OUTPUT_SETTINGS;
        }
        break;

      case CV_SELECT:
        if (SelectedCV == NumberOfCV) {
          menu = OUTPUT_SETTINGS;
        } else {
          menu = CV_CONFIG;
        }
        break;

      case CV_CONFIG:
        setCh = mod(encoderPos, 7);
        switch (setCh) {
          case 0:
            initialCV = SelectedCV;
            menu = CV_SELECT;
            break;
          case 1:
            menu = CV_FUNCTION;
            break;
          case 2:
            menu = CC_NO;
            break;
          case 3:
            menu = CV_MIDICH;
            break;
          case 4:
            menu = CV_RESPONSE;
            break;
          case 5:
            menu = CV_PBRANGE;
            break;
          case 6:
            menu = CV_SF_ADJUST;
            break;
        }
        break;

      case CV_FUNCTION:
        setCh = mod(menuInitialValue + encoderPos, 9);
        if (setCh == 8) {
          learnMode = true;
          menu = CV_MIDI_LEARN;
        } else {
          EEPROM.write(ADDR_CV_FUNCTION + SelectedCV + (NumberOfCV * PresetValueNew), setCh);
          currentfunctionupdate();
          menu = CV_CONFIG;
        }
        break;

      case CC_NO:
        setCh = mod(menuInitialValue + encoderPos, 127);
        EEPROM.write(ADDR_CC_NO + SelectedCV + (NumberOfCV * PresetValueNew), setCh);
        currentfunctionupdate();
        menu = CV_CONFIG;
        break;

      case CV_RESPONSE:
        setCh = mod(menuInitialValue + encoderPos, 3);
        EEPROM.write(ADDR_CV_RESPONSE + SelectedCV + (NumberOfCV * PresetValueNew), setCh);
        currentfunctionupdate();
        menu = CV_CONFIG;
        break;

      case CV_PBRANGE:
        setCh = mod(menuInitialValue + encoderPos, 25);
        EEPROM.write(ADDR_CV_PBRANGE + SelectedCV + (NumberOfCV * PresetValueNew), setCh);
        currentfunctionupdate();
        menu = CV_CONFIG;
        break;

      case CV_MIDICH:
        setCh = mod(menuInitialValue + encoderPos, 21);
        EEPROM.write(ADDR_CV_MIDICH + SelectedCV + (NumberOfCV * PresetValueNew), setCh);
        currentfunctionupdate();
        menu = CV_CONFIG;
        break;

      case CV_SF_ADJUST:
        EEPROM.put(ADDR_CV_SF_ADJUST + SelectedCV * sizeof(float), sfAdj[SelectedCV]);
        currentfunctionupdate();
        menu = CV_CONFIG;
        break;

      case GATE_SELECT:
        if (SelectedGate == 4) {
          menu = OUTPUT_SETTINGS;
        } else {
          menu = GATE_CONFIG;
        }
        break;

      case GATE_CONFIG:
        setCh = mod(encoderPos, 3);
        switch (setCh) {
          case 0:
            initialGate = SelectedGate;
            menu = GATE_SELECT;
            break;
          case 1:
            menu = GATE_FUNCTION;
            break;
          case 2:
            menu = GATE_CHANNEL;
            break;
        }
        break;

      case GATE_FUNCTION:
        setCh = mod(menuInitialValue + encoderPos, 5);
        if (setCh == 4) {
          learnMode = true;
          menu = GATE_MIDI_LEARN;
        } else {
          EEPROM.write(ADDR_GATE_FUNCTION + SelectedGate + (NumberOfGate * PresetValueNew), setCh);
          menu = GATE_CONFIG;
          currentfunctionupdate();
        }
        break;

      case GATE_CHANNEL:
        setCh = mod(menuInitialValue + encoderPos, 21);
        EEPROM.write(ADDR_GATE_MIDICH + SelectedGate + (NumberOfGate * PresetValueNew), setCh);
        currentfunctionupdate();
        menu = GATE_CONFIG;
        break;

      case CV_MIDI_LEARN:
        learnMode = false;
        menu = CV_CONFIG;
        break;

      case GATE_MIDI_LEARN:
        learnMode = false;
        menu = GATE_CONFIG;
        break;

      case MAIN_MIDI_LEARN:
        learnMode = false;
        menu = OUTPUT_SETTINGS;
        break;

      case PRESET_SAVE:
        switch (remotePC) {
          case true:
            setCh = mod(menuInitialValue + encoderPos, NumberOfPreset + 1);
            break;
          case false:
            setCh = mod(menuInitialValue + encoderPos, NumberOfHardwarePreset + 1);
            break;
        }
        if ((remotePC == false ? setCh != NumberOfHardwarePreset + 1 : setCh != NumberOfPreset + 1)) {
          for (int i = 0; i < NumberOfGate; i++) {
            EEPROM.write(i + ADDR_GATE_FUNCTION + (NumberOfGate * (setCh)), CurrentGateFunc[i]);
            EEPROM.write(i + ADDR_GATE_MIDICH + (NumberOfGate * (setCh)), CurrentGateCh[i]);
          }
          for (int i = 0; i < NumberOfCV; i++) {
            EEPROM.write(i + ADDR_CV_FUNCTION + (NumberOfCV * (setCh)), CurrentCVFunc[i]);
            EEPROM.write(i + ADDR_CC_NO + (NumberOfCV * (setCh)), CurrentCCNO[i]);
            EEPROM.write(i + ADDR_CV_RESPONSE + (NumberOfCV * (setCh)), CurrentCVResp[i]);
            EEPROM.write(i + ADDR_CV_PBRANGE + (NumberOfCV * (setCh)), CurrentCVPBRange[i]);
            EEPROM.write(i + ADDR_CV_MIDICH + (NumberOfCV * (setCh)), CurrentCVCh[i]);
          }
          presetSelectHandler(PresetValueNew, true);
        }
        menu = OUTPUT_SETTINGS;
        break;

      case MIDI_MONITOR:
        menu = OUTPUT_SETTINGS;
        break;

      case PRESET_DISPLAY:
        menu = OUTPUT_SETTINGS;
        break;
    }
  }
  highlightTimer = currentMillis;
  highlightEnabled = true;
  encoderPos = 0;  // Reset encoder position
  encoderPosPrev = 0;
  updateSelection();  // Refresh screen
}

void updateSelection() {  // Called whenever encoder is turned, or encoder button is pushed
  display.clearDisplay();
  switch (menu) {
    case OUTPUT_SETTINGS:
    case MAIN_MIDI_CH_SET:
      display.setCursor(0, 0);
      display.setTextColor(WHITE, BLACK);
      display.setTextSize(2);
      display.print(F("SETTINGS"));
      display.setTextSize(1);
      display.setCursor(0, 17);
      if (menu == OUTPUT_SETTINGS) setHighlight(0, 5);
      display.print(F("CONTROL VOLTAGE"));
      display.setCursor(0, 27);
      if (menu == OUTPUT_SETTINGS) setHighlight(1, 5);
      display.print(F("GATE/TRIG"));
      display.setCursor(0, 37);
      if (menu == OUTPUT_SETTINGS) setHighlight(2, 5);
      display.print(F("COPY PRESET"));
      display.setCursor(0, 47);
      if (menu == OUTPUT_SETTINGS) setHighlight(3, 5);
      display.print(F("MAIN MIDI CH: "));
      if (menu == OUTPUT_SETTINGS) {
        int e = EEPROM.read(ADDR_MAIN_MIDI_CH);
        if (e == 16) display.print(F("OMNI"));
        else display.print(e + 1);
      } else if (menu == MAIN_MIDI_CH_SET) {
        menuInitialValue = EEPROM.read(ADDR_MAIN_MIDI_CH);
        display.setTextColor(BLACK, WHITE);
        if (mod(menuInitialValue + encoderPos, 19) == 16) {
          display.print(F("OMNI"));
        } else if (mod(menuInitialValue + encoderPos, 19) == 17) {
          if (remotePC) {
            display.setCursor(0, 57);
            display.print(F("DISABLE MIDI PRESETS"));
          } else if (!remotePC) {
            display.setCursor(0, 57);
            display.print(F("ENABLE MIDI PRESETS"));
          }
        } else if (mod(menuInitialValue + encoderPos, 19) == 18) {
          display.setCursor(0, 57);
          display.print(F("MIDI LEARN"));
        } else if (mod(menuInitialValue + encoderPos, 19) < 16) {
          display.print(mod(menuInitialValue + encoderPos, 19) + 1);
        }
        display.setTextColor(WHITE, BLACK);
      }
      if (menu == OUTPUT_SETTINGS) setHighlight(4, 5);
      if (menu == OUTPUT_SETTINGS || mod(menuInitialValue + encoderPos, 19) < 17) {
        display.setCursor(0, 57);
        display.print(F("MIDI MONITOR"));
      }
      break;

    case CV_SELECT:
      SelectedCV = mod(encoderPos + initialCV, NumberOfCV + 1);
      if (SelectedCV == NumberOfCV) {
        display.setCursor(15, 17);
        display.setTextSize(4);
        display.print(F("BACK"));
        display.setTextSize(1);
      } else {
        case CV_FUNCTION:
        case CV_RESPONSE:
        case CC_NO:
        case CV_MIDICH:
        case CV_SF_ADJUST:
        case CV_PBRANGE:
        case CV_CONFIG:
          if (menu == CV_CONFIG && encoderPosPrev == 6 && encoderPos == 7) {
            encoderPos = 0;
          }
          if (menu == CV_CONFIG && encoderPosPrev == 0 && encoderPos == -1) {
            encoderPos = 6;
          }
          if (menu == CV_CONFIG && EEPROM.read(ADDR_CV_FUNCTION + SelectedCV + (NumberOfCV * PresetValueNew)) != 1 && encoderPosPrev == 1 && encoderPos == 2) {
            encoderPos = 3;
            updateSelection();
          }
          if (menu == CV_CONFIG && EEPROM.read(ADDR_CV_FUNCTION + SelectedCV + (NumberOfCV * PresetValueNew)) != 1 && encoderPosPrev == 3 && encoderPos == 2) {
            encoderPos = 1;
            updateSelection();
          }
          display.setCursor(0, 0);
          display.setTextSize(2);
          display.setTextColor(WHITE, BLACK);
          if (menu == CV_CONFIG) setHighlight(0, 7);
          display.print(F("CV OUT "));
          if (menu == CV_SELECT) display.setTextColor(BLACK, WHITE);
          display.println(SelectedCV + 1);
          display.setTextColor(WHITE, BLACK);
          display.setTextSize(1);
          display.setCursor(0, 17);
          if (menu == CV_CONFIG) setHighlight(1, 7);
          display.print(F("FUNCTION: "));
          menuInitialValue = EEPROM.read(ADDR_CV_FUNCTION + SelectedCV + (NumberOfCV * PresetValueNew));
          if (menu == CV_FUNCTION) {
            display.setTextColor(BLACK, WHITE);
            display.println(CVFuncList[mod(menuInitialValue + encoderPos, 9)]);  // sets the "initial value" that later determines if
            display.setTextColor(WHITE, BLACK);
          } else if (menu != CV_FUNCTION) display.println(CVFuncList[menuInitialValue]);
          if (menuInitialValue == 1 && !(menu == CV_FUNCTION && mod(menuInitialValue + encoderPos, 8) != 1)) {  // checks to see whether the CC value should be displayed. Very round about.
            if (menu == CV_CONFIG) setHighlight(2, 7);
            display.setCursor(78, 17);
            if (menu == CC_NO) {
              display.setTextColor(BLACK, WHITE);
              display.print(F("#"));
              menuInitialValue = EEPROM.read(ADDR_CC_NO + SelectedCV + (NumberOfCV * PresetValueNew));
              display.println(mod(menuInitialValue + encoderPos, 128));
              display.setTextColor(WHITE, BLACK);
            } else {
              display.print(F("#"));
              display.println(EEPROM.read(ADDR_CC_NO + SelectedCV + (NumberOfCV * PresetValueNew)));
            }
          }
          display.setCursor(0, 27);
          if (menu == CV_CONFIG) setHighlight(3, 7);
          display.print(F("MIDI CH: "));
          if (menu == CV_MIDICH) {
            display.setTextColor(BLACK, WHITE);
            menuInitialValue = EEPROM.read(ADDR_CV_MIDICH + SelectedCV + (NumberOfCV * PresetValueNew));
            if (mod(menuInitialValue + encoderPos, 21) == 16) display.print(F("OMNI"));
            else if (mod(menuInitialValue + encoderPos, 21) == 17) display.print("MAIN CH");
            else if (mod(menuInitialValue + encoderPos, 21) > 17) {
              display.print("MAIN CH+");
              display.print(mod(menuInitialValue + encoderPos, 21) - 17);
            } else if (mod(menuInitialValue + encoderPos, 21) < 16) {
              display.print(mod(menuInitialValue + encoderPos, 21) + 1);
            }
            display.setTextColor(WHITE, BLACK);
          } else if (menu != CV_MIDICH) {
            int c = EEPROM.read(ADDR_CV_MIDICH + SelectedCV + (NumberOfCV * PresetValueNew));
            if (c == 16) display.print(F("OMNI"));
            else if (c == 17) display.print("MAIN CH");
            else if (c > 17) {
              display.print("MAIN CH+");
              display.print(c - 17);
            } else if (c < 16) {
              display.print(c + 1);
            }
          }
          display.setCursor(0, 37);
          if (menu == CV_CONFIG) setHighlight(4, 7);
          display.print(F("PRIOR:"));
          if (menu == CV_RESPONSE) {
            menuInitialValue = EEPROM.read(ADDR_CV_RESPONSE + SelectedCV + (NumberOfCV * PresetValueNew));
            display.setTextColor(BLACK, WHITE);
            if (EEPROM.read(ADDR_CV_FUNCTION + SelectedCV + (NumberOfCV * PresetValueNew)) == 0) display.println(CVRespList[mod(menuInitialValue + encoderPos, 3)]);
            else display.print(F("N/A"));
            display.setTextColor(WHITE, BLACK);
          } else {
            if (EEPROM.read(ADDR_CV_FUNCTION + SelectedCV + (NumberOfCV * PresetValueNew)) == 0) display.println(CVRespList[EEPROM.read(ADDR_CV_RESPONSE + SelectedCV + (NumberOfCV * (PresetValueNew)))]);
            else display.print(F("N/A"));
          }
          display.setCursor(60, 37);
          if (menu == CV_CONFIG) setHighlight(5, 7);
          display.print(F("BEND:"));
          if (menu == CV_PBRANGE) {
            menuInitialValue = EEPROM.read(ADDR_CV_PBRANGE + SelectedCV + (NumberOfCV * PresetValueNew));
            display.setTextColor(BLACK, WHITE);
            if (EEPROM.read(ADDR_CV_FUNCTION + SelectedCV + (NumberOfCV * PresetValueNew)) == 0) display.println(mod(menuInitialValue + encoderPos, 25));
            else display.print(F("N/A"));
            display.setTextColor(WHITE, BLACK);
          } else {
            if (EEPROM.read(ADDR_CV_FUNCTION + SelectedCV + (NumberOfCV * PresetValueNew)) == 0) display.println(EEPROM.read(ADDR_CV_PBRANGE + SelectedCV + (NumberOfCV * PresetValueNew)));
            else display.print(F("N/A"));
          }
          display.setCursor(0, 47);
          if (menu == CV_CONFIG) setHighlight(6, 7);
          display.print(F("SCALE FACTOR:"));
          if (menu == CV_SF_ADJUST) {
            if ((encoderPos > encoderPosPrev) && (sfAdj[SelectedCV] < 1.1)) sfAdj[SelectedCV] += 0.001f;
            else if ((encoderPos < encoderPosPrev) && (sfAdj[SelectedCV] > 0.9)) sfAdj[SelectedCV] -= 0.001f;
            display.setTextColor(BLACK, WHITE);
          }
          display.println(sfAdj[SelectedCV], 3);
      }
      break;

    case GATE_SELECT:
      SelectedGate = mod(encoderPos + initialGate, 5);
      if (SelectedGate == 4) {
        display.setCursor(15, 17);
        display.setTextSize(4);
        display.print(F("BACK"));
        display.setTextSize(1);
      } else {
        case GATE_FUNCTION:
        case GATE_CHANNEL:
        case GATE_CONFIG:
          display.setCursor(0, 0);
          display.setTextSize(2);
          display.setTextColor(WHITE, BLACK);
          if (menu == GATE_CONFIG) setHighlight(0, 3);
          display.print(F("GATE OUT "));
          if (menu == GATE_SELECT) display.setTextColor(BLACK, WHITE);
          display.println(TrigLetterList[SelectedGate]);
          display.setTextColor(WHITE, BLACK);
          display.setTextSize(1);
          display.setCursor(0, 16);
          if (menu == GATE_CONFIG) setHighlight(1, 3);
          display.print(F("FUNCTION: "));
          menuInitialValue = EEPROM.read(ADDR_GATE_FUNCTION + SelectedGate + (NumberOfGate * PresetValueNew));
          if (menu == GATE_FUNCTION) {
            display.setTextColor(BLACK, WHITE);
            display.println(GateFuncList[mod(encoderPos + menuInitialValue, 5)]);
            display.setTextColor(WHITE, BLACK);
          } else {
            display.println(GateFuncList[EEPROM.read(ADDR_GATE_FUNCTION + SelectedGate + (NumberOfGate * PresetValueNew))]);
          }
          if (menu == GATE_CONFIG) setHighlight(2, 3);
          display.setCursor(0, 26);
          display.print(F("MIDI CH: "));
          if (menu == GATE_CHANNEL) {
            display.setTextColor(BLACK, WHITE);
            menuInitialValue = EEPROM.read(ADDR_GATE_MIDICH + SelectedGate + (NumberOfGate * PresetValueNew));
            if (mod(menuInitialValue + encoderPos, 21) == 16) display.println("OMNI");
            else if (mod(menuInitialValue + encoderPos, 21) == 17) display.println("MAIN CH");
            else if (mod(menuInitialValue + encoderPos, 21) > 17) {
              display.print("MAIN CH+");
              display.print((mod(menuInitialValue + encoderPos, 21)) - 17);
            } else display.println(mod(menuInitialValue + encoderPos, 21) + 1);
            display.setTextColor(WHITE, BLACK);
          } else if (menu != GATE_CHANNEL) {
            int e = EEPROM.read(ADDR_GATE_MIDICH + SelectedGate + (NumberOfGate * PresetValueNew));
            if (e == 16) display.println("OMNI");
            else if (e == 17) display.println("MAIN CH");
            else if (e > 17) {
              display.print("MAIN CH+");
              display.print(e - 17);
            } else display.println(e + 1);
          }
          break;
      }
      break;

    case PRESET_SAVE:
      display.setCursor(0, 0);
      display.setTextSize(2);
      display.setTextColor(WHITE, BLACK);
      display.print(F("CPY PRESET"));
      display.setTextSize(1);
      display.setCursor(0, 16);
      display.print(F("COPY CURRENT"));
      display.setTextSize(2);
      display.setCursor(78, 16);
      display.println(PresetValueNew + 1);
      display.setTextSize(1);
      display.setCursor(0, 26);
      display.print(F("    PRESET #"));
      display.setCursor(0, 46);
      display.print(F("   TO PRESET"));
      display.setTextColor(BLACK, WHITE);
      display.setTextSize(2);
      display.setCursor(78, 46);
      menuInitialValue = PresetValueNew;
      switch (remotePC) {
        case true:
          if (mod(menuInitialValue + encoderPos, NumberOfPreset + 1) == NumberOfPreset) display.println("BACK");
          else display.println(mod(menuInitialValue + encoderPos, NumberOfPreset + 1) + 1);
          break;
        case false:
          if (mod(menuInitialValue + encoderPos, NumberOfHardwarePreset + 1) == NumberOfHardwarePreset) display.println("BACK");
          else display.println(mod(menuInitialValue + encoderPos, NumberOfHardwarePreset + 1) + 1);
          break;
      }
      display.setTextSize(1);
      display.setTextColor(WHITE, BLACK);
      display.setCursor(0, 56);
      display.print(F("      SLOT # "));
      break;

    case MIDI_MONITOR:
      highlightEnabled = false;
      display.setCursor(20, 0);
      display.setTextSize(2);
      display.setTextColor(WHITE, BLACK);
      display.print(F("MONITOR"));
      break;

    case GATE_MIDI_LEARN:
      highlightEnabled = true;
      display.setCursor(44, 0);
      display.setTextSize(2);
      display.setTextColor(WHITE, BLACK);
      display.println(F("MIDI"));
      display.setCursor(37, 20);
      display.println(F("LEARN"));
      display.setTextSize(1);
      display.setCursor(25, 42);
      display.println(F("SEND MIDI NOTE"));
      display.setCursor(23, 52);
      display.println(F("CLICK TO CANCEL"));
      break;

    case CV_MIDI_LEARN:
      highlightEnabled = true;
      display.setCursor(44, 0);
      display.setTextSize(2);
      display.setTextColor(WHITE, BLACK);
      display.println(F("MIDI"));
      display.setCursor(37, 20);
      display.println(F("LEARN"));
      display.setTextSize(1);
      display.setCursor(17, 42);
      display.println(F("SEND MIDI MESSAGE"));
      display.setCursor(22, 52);
      display.println(F("CLICK TO CANCEL"));
      break;

    case MAIN_MIDI_LEARN:
      highlightEnabled = true;
      display.setCursor(44, 0);
      display.setTextSize(2);
      display.setTextColor(WHITE, BLACK);
      display.println(F("MIDI"));
      display.setCursor(37, 20);
      display.println(F("LEARN"));
      display.setTextSize(1);
      display.setCursor(17, 42);
      display.println(F("SEND MIDI MESSAGE"));
      display.setCursor(22, 52);
      display.println(F("CLICK TO CANCEL"));
      break;

    default:
      break;
  }
  PresetDisplay = false;
  display.display();
}

void currentfunctionupdate() {
  mainMidiCH = EEPROM.read(ADDR_MAIN_MIDI_CH);
  remotePC = EEPROM.read(ADDR_REMOTE_PRESET);
  midiGateHandler(0, 16);
  for (int i = 0; i < NumberOfGate; i++) {
    CurrentGateFunc[i] = EEPROM.read(i + ADDR_GATE_FUNCTION + (NumberOfGate * PresetValueNew));
    CurrentGateCh[i] = EEPROM.read(i + ADDR_GATE_MIDICH + (NumberOfGate * PresetValueNew));
    if (CurrentGateFunc[i] == 2) {
      digitalWrite(GatePins[i], HIGH);
    } else if (CurrentGateFunc[i] != 2) {
      digitalWrite(GatePins[i], LOW);
    }
  }
  for (int i = 0; i < NumberOfCV; i++) {
    CurrentCVFunc[i] = EEPROM.read(i + ADDR_CV_FUNCTION + (NumberOfCV * PresetValueNew));
    CurrentCCNO[i] = EEPROM.read(i + ADDR_CC_NO + (NumberOfCV * PresetValueNew));
    CurrentCVResp[i] = EEPROM.read(i + ADDR_CV_RESPONSE + (NumberOfCV * PresetValueNew));
    CurrentCVPBRange[i] = EEPROM.read(i + ADDR_CV_PBRANGE + (NumberOfCV * PresetValueNew));
    CurrentCVCh[i] = EEPROM.read(i + ADDR_CV_MIDICH + (NumberOfCV * PresetValueNew));
    PBOffset[i] = 0;
    //setVoltage(i, 0);
  }
  for (int i = 0; i < 15; i++) {
    gateOverride[i] = false;
  }
}

void setHighlight(int menuItem, int numMenuItems) {
  if ((mod(encoderPos, numMenuItems) == menuItem) && highlightEnabled) {
    display.setTextColor(BLACK, WHITE);
  } else {
    display.setTextColor(WHITE, BLACK);
  }
}

int mod(int a, int b) {
  int r = a % b;
  return r < 0 ? r + b : r;
}

unsigned long encoderTimer = 0;
unsigned long encoderButtonTimer = 0;
bool encoderButtonState = LOW;
bool encoderButtonLastReading = LOW;

void encButtonHandler() {
  bool encButtonReading = digitalRead(ENC_BTN) == LOW;
  if (encButtonReading != encoderButtonLastReading) {
    encoderButtonTimer = currentMillis;
  }
  if ((currentMillis - encoderButtonTimer) > 50) {
    if (encButtonReading != encoderButtonState) {
      encoderButtonState = encButtonReading;

      if (encoderButtonState) updateMenu();
    }
  }
  encoderButtonLastReading = encButtonReading;
}

void encoderTimerHandler() {
  if (currentMillis - encoderTimer > 5) {
    encoderTimer = currentMillis;
    updateEncoderPos();
  }
}

void updateEncoderPos() {
  static int encoderA, encoderB, encoderA_prev;
  encoderA = digitalRead(ENC_A);
  encoderB = digitalRead(ENC_B);
  if ((!encoderA) && (encoderA_prev)) {  // A has gone from high to low
    if (highlightEnabled) {              // Update encoder position
      encoderPosPrev = encoderPos;
      encoderB ? encoderPos++ : encoderPos--;
    } else {
      highlightEnabled = true;
      encoderPos = 0;  // Reset encoder position if highlight timed out
      encoderPosPrev = 0;
    }
    highlightTimer = currentMillis;
    updateSelection();
  }
  encoderA_prev = encoderA;
}
/////////////

void presetSelectHandler(int8_t presetNum, bool forceDisplay) {
  PresetValueNew = presetNum;
  if (PresetValueNew != PresetValueOld || forceDisplay == true) {
    highlightEnabled = false;
    PresetDisplay = true;
    PresetValueOld = PresetValueNew;
    display.clearDisplay();
    display.setCursor(27, 0);
    display.setTextSize(2);
    display.print(F("PRESET"));
    if (PresetValueNew >= 9) display.setCursor(25, 16);
    else display.setCursor(45, 18);
    display.setTextSize(7);
    display.print(PresetValueNew + 1);
    display.setTextSize(1);
    display.display();
    //menu = PRESET_DISPLAY; //comment this back in if you want to get back to the main menu when clicking out of the preset. I prefer to stay in the sub-menu.
    if (!forceDisplay) currentfunctionupdate();
  }
}

void highlightTimerHandler() {
  if (highlightEnabled && ((currentMillis - highlightTimer) > HIGHLIGHT_TIMEOUT)) {  // Check if highlighting timer expired, and remove highlighting if so
    highlightEnabled = false;
    if (menu != MIDI_MONITOR) {
      menu = OUTPUT_SETTINGS;  // Return to top level menu
      presetSelectHandler(PresetValueNew, true);
    }
  }
}

void gateTriggerTimerHandler() {
  for (int i = 0; i < NumberOfGate; i++) {
    if ((trigTimer[i] > 0) && (currentMillis - trigTimer[i] > trigDuration) && CurrentGateFunc[i] == 1) {
      digitalWrite(GatePins[i], LOW);
      trigTimer[i] = 0;
    }
  }
  for (int i = 0; i < NumberOfCV; i++) {
    if ((cvTrigTimer[i] > 0) && (currentMillis - cvTrigTimer[i] > trigDuration) && CurrentCVFunc[i] == 6) {
      cvTrigTimer[i] = 0;
      uint16_t dacOutputLevel = 0;
      setVoltage(i, dacOutputLevel);
    }
  }
}

bool lastAState = HIGH;
bool lastBState = HIGH;
bool stableAState = HIGH;
bool stableBState = HIGH;
unsigned long lastDebounceTimeA = 0;
unsigned long lastDebounceTimeB = 0;
int lastPosition = 0;

void presetSwitchHandler() {  // Debounce pin A
  bool currentA = digitalRead(PRESET_SELECT_A);
  bool currentB = digitalRead(PRESET_SELECT_B);
  if (currentA != lastAState) {
    lastDebounceTimeA = currentMillis;
  }
  if ((currentMillis - lastDebounceTimeA) > 50) {
    if (currentA != stableAState) {
      stableAState = currentA;
    }
  }
  lastAState = currentA;
  if (currentB != lastBState) {  // Debounce pin A
    lastDebounceTimeB = currentMillis;
  }
  if ((currentMillis - lastDebounceTimeB) > 50) {
    if (currentB != stableBState) {
      stableBState = currentB;
    }
  }
  lastBState = currentB;
  int newPosition = 0;
  if (stableAState == LOW && stableBState == HIGH) {
    newPosition = 0;  // Top
  } else if (stableAState == HIGH && stableBState == LOW) {
    newPosition = 2;  // Bottom
  } else if (stableAState == HIGH && stableBState == HIGH) {
    newPosition = 1;  // Center
  }
  if (newPosition != lastPosition) {
    lastPosition = newPosition;
    presetSelectHandler(newPosition, false);
  }
}
