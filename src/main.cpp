// ============================================================================
// Digital Radio Receiver Firmware
// Hardware: ESP32 + Si4732 + SSD1315 OLED (128x64) + Rotary Encoder
//
// The Si4732 RESET pin is NOT connected to the MCU. An external RC circuit
// handles hardware reset at power-on, so the PU2CLR SI4735 library's
// GPIO-based reset sequence is bypassed with a manual init sequence.
// ============================================================================

// ============================================================================
// Section 1: Includes
// ============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SI4735.h>
#include <AiEsp32RotaryEncoder.h>

// ============================================================================
// Section 2: Pin Definitions & Constants
// ============================================================================

// --- I2C Bus (shared by OLED and Si4732) ---
constexpr uint8_t PIN_I2C_SDA = 21;
constexpr uint8_t PIN_I2C_SCL = 22;

// --- Rotary Encoder ---
constexpr uint8_t ENCODER_PIN_A   = 18;
constexpr uint8_t ENCODER_PIN_B   = 19;
constexpr int     ENCODER_PIN_BTN = 5;
constexpr int     ENCODER_PIN_VCC = -1;  // Not used (encoder powered externally)

// Some encoders produce 4 state changes per physical detent instead of 2,
// causing the value to jump by 2 per click. Enable this flag to compensate.
// Set to false if you replace the encoder with one that has 2 states per detent.
constexpr bool    ENCODER_HALF_STEP_CORRECTION = true;
constexpr uint8_t ENCODER_STEPS = ENCODER_HALF_STEP_CORRECTION ? 4 : 2;

// --- FM Band ---
constexpr uint16_t FM_FREQ_MIN     = 8700;   // 87.0 MHz (in 10 kHz units)
constexpr uint16_t FM_FREQ_MAX     = 10800;  // 108.0 MHz
constexpr uint16_t FM_FREQ_DEFAULT = 10240;  // 102.4 MHz
constexpr uint16_t FM_FREQ_STEP    = 10;     // 100 kHz tuning step

// --- Volume ---
constexpr uint8_t DEFAULT_VOLUME = 30;  // Initial volume level
constexpr uint8_t MAX_VOLUME     = 63;  // Si4732 maximum volume

// --- OLED Display ---
constexpr uint8_t SCREEN_WIDTH   = 128;
constexpr uint8_t SCREEN_HEIGHT  = 64;
constexpr uint8_t OLED_I2C_ADDR  = 0x3C;  // Typical address for SSD1315/SSD1306

// --- Timing ---
constexpr unsigned long RSSI_UPDATE_INTERVAL_MS = 500;  // Signal strength poll rate

// ============================================================================
// Section 3: Enums, Global Objects & State
// ============================================================================

/**
 * Adjustment mode determines what the rotary encoder controls.
 * Button press toggles between these modes.
 */
enum AdjustMode {
    MODE_FREQUENCY,  // Encoder adjusts FM frequency
    MODE_VOLUME      // Encoder adjusts audio volume
};

// --- Hardware objects ---
SI4735 radio;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
AiEsp32RotaryEncoder encoder(ENCODER_PIN_A, ENCODER_PIN_B,
                              ENCODER_PIN_BTN, ENCODER_PIN_VCC, ENCODER_STEPS);

// --- Application state ---
AdjustMode currentMode      = MODE_FREQUENCY;
uint16_t currentFrequency   = FM_FREQ_DEFAULT;
uint8_t  currentVolume      = DEFAULT_VOLUME;
uint8_t  currentRSSI        = 0;
bool     displayNeedsUpdate = true;
unsigned long lastRSSIUpdate = 0;

// ============================================================================
// Section 4: Encoder ISR
// ============================================================================

/**
 * Interrupt Service Routine for the rotary encoder.
 * Called on pin state changes for both A and B encoder pins.
 * Must be a free function marked IRAM_ATTR for ESP32 ISR placement.
 */
void IRAM_ATTR readEncoderISR() {
    encoder.readEncoder_ISR();
}

// ============================================================================
// Section 5: Forward Declarations
// ============================================================================

void initRadio();
void initDisplay();
void initEncoder();
void handleEncoder();
void handleButton();
void updateDisplay();
void updateRSSI();
void setEncoderBoundsForMode();

// Display drawing helpers
void drawModeIndicator();
void drawRSSIBar();
void drawFrequency();
void drawVolumeBar();

// ============================================================================
// Section 6: setup()
// ============================================================================

void setup() {
    Serial.begin(115200);
    Serial.println(F("Digital Radio — starting up..."));

    // Initialize the shared I2C bus before any peripherals
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    // Wait for the Si4732 hardware RC reset circuit to complete.
    // The RC time constant determines how long the chip needs after power-on.
    delay(500);

    // Show splash screen while the radio initializes
    initDisplay();

    // Power up the Si4732 (bypassing library's GPIO reset)
    initRadio();

    // Configure the rotary encoder for frequency mode
    initEncoder();

    // Read initial state from the radio
    currentFrequency = radio.getFrequency();
    currentVolume = radio.getVolume();

    displayNeedsUpdate = true;
    Serial.println(F("Digital Radio — ready."));
}

// ============================================================================
// Section 7: loop()
// ============================================================================

void loop() {
    handleEncoder();   // Process encoder rotation (frequency or volume change)
    handleButton();    // Process encoder button press (toggle mode)
    updateRSSI();      // Periodically refresh signal strength
    updateDisplay();   // Redraw OLED only when state has changed
}

// ============================================================================
// Section 8: Radio Initialization
// ============================================================================

/**
 * Initialize the Si4732 radio WITHOUT using a reset pin.
 *
 * The standard library flow is: setup() -> reset() -> radioPowerUp().
 * Since our hardware uses an RC circuit for reset (no GPIO connected),
 * we manually replicate the setup() sequence while skipping reset().
 *
 * Reference: SI4735.cpp lines 579-605 (setup), 302-310 (reset)
 */
void initRadio() {
    Serial.println(F("Initializing Si4732 (no reset pin)..."));

    // Set I2C address: SEN pin is wired to GND -> address 0x11
    radio.setDeviceI2CAddress(0);

    // Configure power-up parameters:
    //   CTSIEN=0    (CTS interrupt disabled)
    //   GPO2OEN=0   (GPO2 output disabled)
    //   PATCH=0     (normal boot, no firmware patch)
    //   XOSCEN=1    (use crystal oscillator)
    //   FUNC=0      (FM receive mode)
    //   OPMODE=0x05 (analog audio output)
    radio.setPowerUp(0, 0, 0, XOSCEN_CRYSTAL, POWER_UP_FM, SI473X_ANALOG_AUDIO);

    // Send POWER_UP command over I2C (the chip is already out of reset
    // thanks to the hardware RC circuit)
    radio.radioPowerUp();

    // Allow crystal oscillator to stabilize after power-up
    delay(250);

    radio.setVolume(DEFAULT_VOLUME);
    radio.getFirmware();

    // Tune to the FM band. Internally this calls powerDown() + radioPowerUp()
    // which is safe — those methods only use I2C, never GPIO reset.
    radio.setFM(FM_FREQ_MIN, FM_FREQ_MAX, FM_FREQ_DEFAULT, FM_FREQ_STEP);

    Serial.print(F("Si4732 ready. Firmware PN: "));
    Serial.println(radio.getFirmwarePN());
    Serial.print(F("Tuned to: "));
    Serial.print(radio.getFrequency() / 100.0, 1);
    Serial.println(F(" MHz"));
}

// ============================================================================
// Section 9: Display Functions
// ============================================================================

/**
 * Initialize the SSD1315 OLED display and show a splash screen.
 * Halts execution if the display is not found on the I2C bus.
 */
void initDisplay() {
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        Serial.println(F("ERROR: SSD1306/SSD1315 display not found!"));
        while (true) { delay(1000); }  // Halt — cannot proceed without display
    }

    // Splash screen
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(8, 4);
    display.print(F("Digital"));
    display.setCursor(20, 28);
    display.print(F("Radio"));
    display.setTextSize(1);
    display.setCursor(16, 52);
    display.print(F("Initializing..."));
    display.display();
}

/**
 * Redraw the entire OLED display if any state has changed.
 * Uses a single display() flush to avoid flicker — all drawing
 * happens in the RAM buffer before being pushed to the screen.
 *
 * Display layout (SSD1315: top ~16px yellow, bottom ~48px blue):
 *
 *   Y=0  | FM          [RSSI bar]     |  <- Yellow zone
 *   Y=16 |----------------------------|
 *        | > 102.4 MHz                |  <- Blue zone (frequency)
 *        |                            |
 *        | > Vol [========--] 30      |  <- Blue zone (volume)
 */
void updateDisplay() {
    if (!displayNeedsUpdate) return;
    displayNeedsUpdate = false;

    display.clearDisplay();

    // Yellow zone (y = 0..15): mode label and signal strength
    drawModeIndicator();
    drawRSSIBar();

    // Blue zone (y = 16..63): frequency and volume
    drawFrequency();
    drawVolumeBar();

    // Push the completed frame buffer to the OLED in one transfer
    display.display();
}

/**
 * Draw the current radio mode label ("FM") in the yellow zone (top-left).
 */
void drawModeIndicator() {
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print(F("FM"));
}

/**
 * Draw a signal strength (RSSI) bar in the yellow zone (top-right).
 * RSSI range is 0-127 dBuV, but typical FM signals fall within 0-60.
 */
void drawRSSIBar() {
    constexpr int BAR_MAX_W = 50;  // Maximum bar width in pixels
    constexpr int BAR_H     = 10;
    constexpr int BAR_X     = SCREEN_WIDTH - BAR_MAX_W - 4;
    constexpr int BAR_Y     = 3;

    // Map RSSI (clamped to 0-60 range) to bar width
    int barWidth = map(constrain(currentRSSI, 0, 60), 0, 60, 0, BAR_MAX_W);

    // Filled portion (signal level) + outline (max range)
    display.fillRect(BAR_X, BAR_Y, barWidth, BAR_H, SSD1306_WHITE);
    display.drawRect(BAR_X, BAR_Y, BAR_MAX_W, BAR_H, SSD1306_WHITE);
}

/**
 * Draw the current frequency in large text.
 * A ">" marker indicates this parameter is currently encoder-controlled.
 */
void drawFrequency() {
    constexpr int Y = 22;

    display.setTextColor(SSD1306_WHITE);

    // Active mode selector arrow
    if (currentMode == MODE_FREQUENCY) {
        display.setTextSize(1);
        display.setCursor(0, Y + 4);
        display.print(F(">"));
    }

    // Frequency in large text: e.g. "102.4 MHz"
    display.setTextSize(2);
    display.setCursor(10, Y);

    uint16_t mhzWhole = currentFrequency / 100;
    uint8_t  mhzFrac  = (currentFrequency % 100) / 10;
    display.print(mhzWhole);
    display.print(F("."));
    display.print(mhzFrac);

    // "MHz" label in smaller text, aligned to the right of the frequency
    display.setTextSize(1);
    display.setCursor(display.getCursorX() + 2, Y + 8);
    display.print(F("MHz"));
}

/**
 * Draw the volume bar with numeric value.
 * A ">" marker indicates this parameter is currently encoder-controlled.
 */
void drawVolumeBar() {
    constexpr int Y          = 52;
    constexpr int BAR_X      = 32;
    constexpr int BAR_MAX_W  = 68;
    constexpr int BAR_H      = 8;

    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    // Active mode selector arrow
    if (currentMode == MODE_VOLUME) {
        display.setCursor(0, Y);
        display.print(F(">"));
    }

    // "Vol" label
    display.setCursor(8, Y);
    display.print(F("Vol"));

    // Volume bar: filled portion + outline
    int barWidth = map(currentVolume, 0, MAX_VOLUME, 0, BAR_MAX_W);
    display.fillRect(BAR_X, Y, barWidth, BAR_H, SSD1306_WHITE);
    display.drawRect(BAR_X, Y, BAR_MAX_W, BAR_H, SSD1306_WHITE);

    // Numeric value to the right of the bar
    display.setCursor(BAR_X + BAR_MAX_W + 4, Y);
    display.print(currentVolume);
}

// ============================================================================
// Section 10: Encoder Functions
// ============================================================================

/**
 * Initialize the rotary encoder with ISR and set boundaries
 * for the default adjustment mode (frequency).
 */
void initEncoder() {
    encoder.begin();
    encoder.setup(readEncoderISR);
    encoder.setAcceleration(100);
    setEncoderBoundsForMode();
}

/**
 * Configure encoder value boundaries based on the current adjustment mode.
 * Called on startup and whenever the mode changes.
 *
 * For frequency mode: values represent frequency/step (e.g. 870..1080),
 *   with wrapping enabled so tuning past the band edge wraps around.
 * For volume mode: values are 0..63 with clamping (no wrap).
 */
void setEncoderBoundsForMode() {
    if (currentMode == MODE_FREQUENCY) {
        encoder.setBoundaries(FM_FREQ_MIN / FM_FREQ_STEP,
                              FM_FREQ_MAX / FM_FREQ_STEP, true);
        encoder.reset(currentFrequency / FM_FREQ_STEP);
        encoder.setAcceleration(100);
    } else {
        encoder.setBoundaries(0, MAX_VOLUME, false);
        encoder.reset(currentVolume);
        encoder.setAcceleration(50);
    }
}

/**
 * Check if the encoder has been rotated and apply the change
 * to either frequency or volume depending on the current mode.
 */
void handleEncoder() {
    if (encoder.encoderChanged() == 0) return;

    long encoderValue = encoder.readEncoder();

    if (currentMode == MODE_FREQUENCY) {
        uint16_t newFreq = (uint16_t)(encoderValue * FM_FREQ_STEP);

        // Clamp to valid FM range as a safety measure
        if (newFreq < FM_FREQ_MIN) newFreq = FM_FREQ_MIN;
        if (newFreq > FM_FREQ_MAX) newFreq = FM_FREQ_MAX;

        if (newFreq != currentFrequency) {
            radio.setFrequency(newFreq);
            currentFrequency = newFreq;
            displayNeedsUpdate = true;
        }
    } else {
        uint8_t newVol = (uint8_t)encoderValue;
        if (newVol > MAX_VOLUME) newVol = MAX_VOLUME;

        if (newVol != currentVolume) {
            radio.setVolume(newVol);
            currentVolume = newVol;
            displayNeedsUpdate = true;
        }
    }
}

/**
 * Check if the encoder button was pressed and toggle between
 * frequency and volume adjustment modes.
 *
 * Note: isEncoderButtonClicked() blocks up to 300ms for debounce.
 * This is acceptable since we have no time-critical processing in the loop.
 */
void handleButton() {
    if (!encoder.isEncoderButtonClicked()) return;

    // Toggle between frequency and volume modes
    currentMode = (currentMode == MODE_FREQUENCY) ? MODE_VOLUME : MODE_FREQUENCY;
    setEncoderBoundsForMode();
    displayNeedsUpdate = true;

    Serial.print(F("Mode: "));
    Serial.println((currentMode == MODE_FREQUENCY) ? F("FREQUENCY") : F("VOLUME"));
}

// ============================================================================
// Section 11: Utility Functions
// ============================================================================

/**
 * Periodically read the received signal quality from the Si4732.
 * getCurrentReceivedSignalQuality() must be called first to populate
 * the internal status struct; getCurrentRSSI() is just an inline getter.
 */
void updateRSSI() {
    unsigned long now = millis();
    if (now - lastRSSIUpdate < RSSI_UPDATE_INTERVAL_MS) return;
    lastRSSIUpdate = now;

    radio.getCurrentReceivedSignalQuality();
    uint8_t newRSSI = radio.getCurrentRSSI();

    if (newRSSI != currentRSSI) {
        currentRSSI = newRSSI;
        displayNeedsUpdate = true;
    }
}
