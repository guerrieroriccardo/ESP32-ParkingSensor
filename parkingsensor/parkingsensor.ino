// ESP32-S3 Parking Sensor with WS2812B - DEEP SLEEP MODE
// Ultra-low power consumption: ~10-20mA in standby instead of 80mA

#include <Adafruit_NeoPixel.h>
#include <WiFi.h>

// Pin definitions
#define TRIG_PIN 5
#define ECHO_PIN 6
#define LED_PIN 7
#define NUM_LEDS 8

// Distance thresholds (in cm)
// Careful! Most ultrasonic sensors don't go below 20cm, so keep a safe margin
#define DIST_DETECTION 250  // 2.5 meters - wake system from standby
#define DIST_GREEN 100      // > 100cm = green
#define DIST_YELLOW 75      // 75-100cm = yellow
#define DIST_ORANGE 55      // 55-75cm = orange
#define DIST_RED 35         // 35-55cm = red
                            // < 35cm = blinking red

// Timing
#define INACTIVITY_TIMEOUT 60000        // 1 minute without movement = shutdown
#define ACTIVE_POLL_RATE 50             // Fast polling when active (50ms)
#define MOVEMENT_THRESHOLD 10            // Distance change to detect movement (cm)
#define REACTIVATION_THRESHOLD 20       // Change needed to reactivate after shutdown (cm)
#define DEEP_SLEEP_DURATION 5000000     // 5 seconds in microseconds (deep sleep)

// RTC Memory - survives deep sleep
RTC_DATA_ATTR long sleepDistance = 999;
RTC_DATA_ATTR bool wasActive = false;

// Initialize LED strip
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// State variables
unsigned long lastMovementTime = 0;
unsigned long previousMillis = 0;
unsigned long lastPollTime = 0;
bool systemActive = false;
bool ledState = false;
int currentBlinkDelay = 1000;
long lastDistance = 999;
uint32_t currentColor = 0;

void setup() {
  Serial.begin(115200);
  
  // Disable WiFi and Bluetooth for power saving (first thing)
  WiFi.mode(WIFI_OFF);
  btStop();
  
  // Initialize ultrasonic sensor
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  // Initialize LED strip
  strip.begin();
  strip.setBrightness(50);  // Reduced to 50 for power saving
  strip.show();
  
  Serial.println("ESP32-S3 Parking Sensor - DEEP SLEEP MODE");
  
  // First measurement after wakeup
  delay(100);
  long distance = measureDistance();
  
  Serial.print("Wakeup - Distance: ");
  Serial.print(distance);
  Serial.print(" cm, Memory: ");
  Serial.println(sleepDistance);
  
  // Decide whether to enter active mode or go back to sleep
  if (distance < DIST_DETECTION && distance > 0) {
    // Object detected
    
    if (sleepDistance == 999 || abs(distance - sleepDistance) > REACTIVATION_THRESHOLD) {
      // New object or significant movement
      systemActive = true;
      sleepDistance = 999;
      lastMovementTime = millis();
      Serial.println(">>> SYSTEM ACTIVATED <<<");
    } else {
      // Same stationary object - go back to sleep immediately
      Serial.println(">>> Stationary object - Going back to sleep <<<");
      goToDeepSleep();
    }
  } else {
    // No object - reset memory and sleep
    sleepDistance = 999;
    Serial.println(">>> No object - Going back to sleep <<<");
    goToDeepSleep();
  }
}

void loop() {
  if (!systemActive) {
    // If not active, go to deep sleep
    goToDeepSleep();
  }
  
  unsigned long currentMillis = millis();
  
  // Fast polling when active
  if (currentMillis - lastPollTime >= ACTIVE_POLL_RATE) {
    lastPollTime = currentMillis;
    
    long distance = measureDistance();
    
    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.println(" cm - ACTIVE");
    
    if (distance < DIST_DETECTION && distance > 0) {
      // Object still present
      
      // Detect movement
      if (abs(distance - lastDistance) > MOVEMENT_THRESHOLD) {
        lastMovementTime = currentMillis;
        Serial.println(">>> MOVEMENT DETECTED <<<");
      }
      
      // Update LEDs
      determineColorAndBlink(distance);
      
      lastDistance = distance;
      
      // Check inactivity timeout
      if (currentMillis - lastMovementTime >= INACTIVITY_TIMEOUT) {
        // Car stopped - memorize and go to deep sleep
        systemActive = false;
        sleepDistance = distance;
        turnOffLEDs();
        Serial.print(">>> Car stopped - Memorizing ");
        Serial.print(sleepDistance);
        Serial.println(" cm and going to deep sleep <<<");
        delay(100);
        goToDeepSleep();
      }
      
    } else {
      // Object left - go to deep sleep
      systemActive = false;
      sleepDistance = 999;
      turnOffLEDs();
      Serial.println(">>> Object left - Deep sleep <<<");
      delay(100);
      goToDeepSleep();
    }
  }
  
  // Handle blinking
  if (currentMillis - previousMillis >= currentBlinkDelay) {
    previousMillis = currentMillis;
    
    if (lastDistance < DIST_RED) {
      ledState = !ledState;
      if (ledState) {
        setStripColor(strip.Color(255, 0, 0));
      } else {
        setStripColor(strip.Color(0, 0, 0));
      }
    }
  }
  
  delay(10);
}

void goToDeepSleep() {
  Serial.println("Entering deep sleep for 2 seconds...");
  Serial.flush();  // Wait for print to finish
  
  // Turn off everything
  turnOffLEDs();
  
  // Configure timer wakeup (2 seconds)
  esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION);
  
  // Enter deep sleep
  esp_deep_sleep_start();
  
  // Code never reaches here - ESP restarts after sleep
}

long measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  
  if (duration == 0) {
    return 999;
  }
  
  long distance = duration * 0.01715;
  return distance;
}

void determineColorAndBlink(long distance) {
  if (distance < DIST_RED) {
    // Critical zone - blinking red (< 35cm)
    currentBlinkDelay = 200;
    currentColor = strip.Color(255, 0, 0);
  }
  else if (distance < DIST_ORANGE) {
    // Danger zone - solid red (35-55cm)
    currentBlinkDelay = 10000;
    setStripColor(strip.Color(255, 0, 0));
    currentColor = strip.Color(255, 0, 0);
  }
  else if (distance < DIST_YELLOW) {
    // Warning zone - orange (55-75cm)
    currentBlinkDelay = 10000;
    setStripColor(strip.Color(255, 165, 0));
    currentColor = strip.Color(255, 165, 0);
  }
  else if (distance < DIST_GREEN) {
    // Caution zone - yellow (75-100cm)
    currentBlinkDelay = 10000;
    setStripColor(strip.Color(255, 255, 0));
    currentColor = strip.Color(255, 255, 0);
  }
  else {
    // Safe zone - green (> 100cm but < 250cm)
    currentBlinkDelay = 10000;
    setStripColor(strip.Color(0, 255, 0));
    currentColor = strip.Color(0, 255, 0);
  }
}

void setStripColor(uint32_t color) {
  for(int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}

void turnOffLEDs() {
  for(int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, 0);
  }
  strip.show();
}