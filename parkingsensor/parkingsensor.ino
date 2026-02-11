// ESP32 Parking Sensor
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>  // AGGIUNTO!

// Pin definitions
#define TRIG_PIN 5
#define ECHO_PIN 6
#define LED_PIN 7
#define NUM_LEDS 12

// Distance thresholds (in cm)
// Carefull! Most ultrasonic sensors don't go below 20cm, so keep a safe margin
#define DIST_DETECTION 250  // 2.5 meters - wake system from standby
#define DIST_GREEN 100      // > 100cm = green
#define DIST_YELLOW 75      // 75-100cm = yellow
#define DIST_ORANGE 55      // 55-75cm = orange
#define DIST_RED 35         // 35-55cm = red
                            // < 35cm = blinking red

// Timing
#define INACTIVITY_TIMEOUT 30000    // After how much should the system be put in standby
#define IDLE_POLL_RATE 2000         // Slow polling when system is standby (2 seconds)
#define ACTIVE_POLL_RATE 50         // Fast polling when the system is active (50ms)
#define MOVEMENT_THRESHOLD 10       // Sensor sensibility to movement (cm)
#define REACTIVATION_THRESHOLD 20   // After how much variation should the system wake up (cm)

// LED strip setup
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// State variables
unsigned long lastMovementTime = 0;
unsigned long previousMillis = 0;
unsigned long lastPollTime = 0;
bool systemActive = false;
bool ledState = false;
int currentBlinkDelay = 1000;
int currentPollRate = IDLE_POLL_RATE;
long lastDistance = 999;
long sleepDistance = 999;  
uint32_t currentColor = 0;

void setup() {
  Serial.begin(115200);
  
  // Ultrasound sensor initialization
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  // LED strip initialization
  strip.begin();
  strip.setBrightness(50);
  strip.show();  // All LEDs should be turned off

  // Turn off WiFi and Bluetooth
  WiFi.mode(WIFI_OFF);
  btStop();
  
  Serial.println("ESP32 Parking Sensor");
  Serial.println("System in standby - Waiting for movement...");
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Adaptive polling for the sensor
  if (currentMillis - lastPollTime >= currentPollRate) {
    lastPollTime = currentMillis;
    
    long distance = measureDistance();
    
    // Debug output
    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.print(" cm - System: ");
    Serial.print(systemActive ? "Active" : "Standby");
    if (!systemActive && sleepDistance < 999) {
      Serial.print(" - Ignore: ");
      Serial.print(sleepDistance);
      Serial.print(" cm");
    }
    Serial.println();
    
    // Check if there's an object withing the detection range
    if (distance < DIST_DETECTION && distance > 0) {
      // System in standby
      if (!systemActive) {
        
        // This check if the found object was already there of it is "new"
        if (sleepDistance == 999 || abs(distance - sleepDistance) > REACTIVATION_THRESHOLD) {
          // New object found or above treshold - Activate System
          systemActive = true;
          currentPollRate = ACTIVE_POLL_RATE;
          lastMovementTime = currentMillis;
          sleepDistance = 999;  // Reset memory
          Serial.println(">>> System Active - New object found or movement <<<");
        } else {
          // Same object as before
          Serial.println(">>> Still object - Ignore <<<");
        }
        
      } else {
        // System already active - Scan for movement
        
        // Movement is superior to the treshold
        if (abs(distance - lastDistance) > MOVEMENT_THRESHOLD) {
          lastMovementTime = currentMillis;  // Aggiorna tempo ultimo movimento
          Serial.println(">>> Movement found <<<");
        }
        
        // Update LEDs color
        determineColorAndBlink(distance);
        
        // Check for inactivity (car is still)
        if (currentMillis - lastMovementTime >= INACTIVITY_TIMEOUT) {
          // If there's inactivity, the system should enter standby mode
          systemActive = false;
          currentPollRate = IDLE_POLL_RATE;
          sleepDistance = distance;  // Save the distance of the current object
          turnOffLEDs();
          Serial.print(">>> System Standby - Car is still, saving current distance: ");
          Serial.print(sleepDistance);
          Serial.println(" cm <<<");
        }
      }
      
      lastDistance = distance;
      
    } else {
      // There's no object within 2 meters range
      
      if (systemActive) {
        // If no car is found, deactivate the system
        systemActive = false;
        currentPollRate = IDLE_POLL_RATE;
        sleepDistance = 999;  // Reset memory - Object has left the zone
        turnOffLEDs();
        Serial.println(">>> System Standby - Object out of zone <<<");
      }
      
      // If the system was already standby, we just reset it's memory
      if (sleepDistance < 999) {
        sleepDistance = 999;
        Serial.println(">>> Reset Memory - Zone is currently clear <<<");
      }
      
      lastDistance = 999;
    }
  }
  
  // Blink only if system is active
  if (systemActive && currentMillis - previousMillis >= currentBlinkDelay) {
    previousMillis = currentMillis;
    
    if (lastDistance < DIST_RED) {
      // Critical zone, blink red
      ledState = !ledState;
      if (ledState) {
        setStripColor(strip.Color(255, 0, 0));  // Red
      } else {
        setStripColor(strip.Color(0, 0, 0));    // Turned off
      }
    }
  }
  
  // Small delay to not overload the loop
  delay(10);
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
    currentBlinkDelay = 200;
    currentColor = strip.Color(255, 0, 0);
  }
  else if (distance < DIST_ORANGE) {
    currentBlinkDelay = 10000;
    setStripColor(strip.Color(255, 0, 0));  // Red
    currentColor = strip.Color(255, 0, 0);
  }
  else if (distance < DIST_YELLOW) {
    currentBlinkDelay = 10000;
    setStripColor(strip.Color(255, 165, 0));  // Orange
    currentColor = strip.Color(255, 165, 0);
  }
  else if (distance < DIST_GREEN) {
    currentBlinkDelay = 10000;
    setStripColor(strip.Color(255, 255, 0));  // Yellow
    currentColor = strip.Color(255, 255, 0);
  }
  else {
    currentBlinkDelay = 10000;
    setStripColor(strip.Color(0, 255, 0));  // Green
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
  Serial.println("LED spenti");
}