#include <FastLED.h>
 
constexpr int kOnboardLed0Pin = 32;
constexpr int kOnboardLed1Pin = 33;
 
// These are enable lines for the matrix power switch.
// If only Pin0 is high, the matrix is current-limited to 1.5A.
// If both are high, the matrix is current-limited to 3A.
// The switch resistance is lower with both enabled, so we always
// enable Pin0 first to charge up the capacitances (since we may
// only be allowed 1.5A), then enable Pin1 even if we only have
// 1.5A available to improve switch efficiency, and apply current
// limit in firmware instead (not setting LEDs too bright).
constexpr int kLedMatrixPowerPin0 = 12;
constexpr int kLedMatrixPowerPin1 = 26;
 
constexpr int kLedMatrixDataPin = 27;
constexpr int kUsbCc1Pin = 36;
constexpr int kUsbCc2Pin = 39;
constexpr int kTouch0Pin = 15;
constexpr int kTouch1Pin = 2;
constexpr int kTouch2Pin = 4;
 
template <int kIo>
class TouchButtonReader {
 public:
  TouchButtonReader()
      : filtered_value(static_cast<float>(touchRead(kIo))) {}
  
  void Update() {
    filtered_value *= kFilterStrength;
    filtered_value += (1.0f - kFilterStrength) * touchRead(kIo);
  }
 
  int Value() { return static_cast<int>(filtered_value); }
 
 private:
  static constexpr float kFilterStrength = 0.7f;
  float filtered_value;
};
 
enum class UsbCurrentAvailable {
  // USB-C host and we can draw 3A.
  k3A,
 
  // USB-C host and we can draw 1.5A.
  k1_5A,
 
  // We have either a USB-C host with no high current
  // capability or a legacy host / 1.5A (BCS) power supply. We do
  // not have the hardware to tell them apart, so we
  // can't make any assumptions on current available
  // beyond 100mA (though most hosts will be fine with 500mA).
  kUsbStd,
};
 
float AnalogReadV(int pin) {
  // By default we have 11db (1/3.6) attenuation with 1.1V reference,
  // so full scale voltage range is 1.1*3.6 = 3.96. In reality it's
  // clamped to Vdd (3.3V) so we will never get a reading above that,
  // but we need to the calculations using 3.96.
  return analogRead(pin) * 3.96f / 4096;
}
 
UsbCurrentAvailable DetermineMaxCurrent() {
  // This function implements USB-C current advertisement detection.
  // Universal Serial Bus Type-C Cable and Connector Specification
  // Depending on cable orientation, either CC1 or CC2 will be >0V,
  // and that tells us how much current we can draw.
  // Voltage thresholds from Table 4-36:
  // >0.2V = connected, >0.66V = 1.5A, >1.23V = 3A.
  float cc1 = AnalogReadV(kUsbCc1Pin);
  float cc2 = AnalogReadV(kUsbCc2Pin);
  float cc = max(cc1, cc2);
  if (cc > 1.23f) {
    return UsbCurrentAvailable::k3A;
  } else if (cc > 0.66f) {
    return UsbCurrentAvailable::k1_5A;
  } else {
    return UsbCurrentAvailable::kUsbStd;
  }
}
 
void EnableLEDPower() {
  // Make sure data pin is low so we don't latch up the LEDs.
  digitalWrite(kLedMatrixDataPin, LOW);
 
  // Enable 1.5A current to charge up the capacitances.
  digitalWrite(kLedMatrixPowerPin0, HIGH);
 
  delay(50 /* milliseconds */);
 
  // Enable the second 1.5A switch to reduce switch resistance
  // even if we only have 1.5A total, because we can limit it in
  // firmware instead.
  digitalWrite(kLedMatrixPowerPin1, HIGH);
}
 
void DisableLEDPower() {
  digitalWrite(kLedMatrixDataPin, LOW);
  digitalWrite(kLedMatrixPowerPin1, LOW);
  digitalWrite(kLedMatrixPowerPin0, LOW);
}
 
constexpr int kMatrixSize = 256;
constexpr float kMatrixMaxCurrent = kMatrixSize * 0.06f; // WS2812B: 60mA/LED
constexpr float kMaxIdleCurrent = 0.5f; // Matrix + ESP32 idle
CRGB leds[kMatrixSize];
 
void setup() {
  Serial.begin(115200);
  pinMode(kOnboardLed0Pin, OUTPUT);
  pinMode(kOnboardLed1Pin, OUTPUT);
  pinMode(kLedMatrixDataPin, OUTPUT);
  pinMode(kLedMatrixPowerPin0, OUTPUT);
  pinMode(kLedMatrixPowerPin1, OUTPUT);
  pinMode(kTouch0Pin, INPUT);
  pinMode(kTouch1Pin, INPUT);
  pinMode(kTouch2Pin, INPUT);
  FastLED.addLeds<NEOPIXEL, kLedMatrixDataPin>(leds, kMatrixSize);
}
 
void loop() {
  static UsbCurrentAvailable current_available =
      UsbCurrentAvailable::kUsbStd;
  UsbCurrentAvailable current_advertisement = DetermineMaxCurrent();
  if (current_available != current_advertisement) {
    // Wait 15ms and read again to make sure it's stable (not a PD message). See comment below
    // for more details.
    delay(15);
    if (DetermineMaxCurrent() == current_advertisement) {
      switch (current_advertisement) {
        case UsbCurrentAvailable::k3A:
          digitalWrite(kOnboardLed0Pin, HIGH);
          digitalWrite(kOnboardLed1Pin, HIGH);
          FastLED.setBrightness(255 * (3.0f - kMaxIdleCurrent) / kMatrixMaxCurrent);
          EnableLEDPower();
          break;
        case UsbCurrentAvailable::k1_5A:
          digitalWrite(kOnboardLed0Pin, LOW);
          digitalWrite(kOnboardLed1Pin, HIGH);
          FastLED.setBrightness(255 * (1.5f - kMaxIdleCurrent) / kMatrixMaxCurrent);
          EnableLEDPower();
          break;
        default:
          digitalWrite(kOnboardLed0Pin, LOW);
          digitalWrite(kOnboardLed1Pin, LOW);
          DisableLEDPower();
      }
      current_available = current_advertisement;
    }
  }
 
  static CRGB colour = CRGB::White;
 
  static TouchButtonReader<kTouch0Pin> touch0;
  static TouchButtonReader<kTouch1Pin> touch1;
  static TouchButtonReader<kTouch2Pin> touch2;
 
  touch0.Update();
  touch1.Update();
  touch2.Update();
 
  if (touch0.Value() < 20) {
    colour = CRGB::White;
  } else if (touch1.Value() < 20) {
    colour = CRGB::Blue;
  } else if (touch2.Value() < 20) {
    colour = CRGB::Black;
  }
 
  Serial.printf("%d %d %d\n", touch0.Value(), touch1.Value(), touch2.Value());
 
  FastLED.showColor(colour);
 
  // USB-C spec says the host can change the advertised current limit at any time,
  // and we have tSinkAdj(max) (60ms) to comply.
  // When we detect a CC value change, we also have to wait tRpValueChange(min) (10ms)
  // to make sure this is not a PD message (which cause, for our purposes, noise on the
  // CC line).
  // So we implement that by having the loop run at 30ms, and if we see a CC change,
  // we sample again in 15ms to make sure the value is stable, satisfying both timing
  // requirements.
  delay(30);
}
