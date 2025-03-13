#include <Wire.h>
#include <SparkFun_APDS9960.h>
#include <BluetoothSerial.h>
#include <BleKeyboard.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <JestrOSbitmaps.h>
#include <math.h>
// Initialize the LCD with the correct address and dimensions (16 columns and 2 rows)
BluetoothSerial SerialBT;
BleKeyboard bleKeyboard("Jestr v1.0", "Automaton Creations", 100);
// Pins for gesture sensor
#define APDS9960_INT 2 // Needs to be an interrupt pin
#define LED_PIN 12


#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library. 
// On an arduino UNO:       A4(SDA), A5(SCL)
// On an arduino MEGA 2560: 20(SDA), 21(SCL)
// On an arduino LEONARDO:   2(SDA),  3(SCL), ...
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define NUMFLAKES     10 // Number of snowflakes in the animation example

#define bitmap_Jestr_WIDTH   31
#define bitmap_Jestr_HEIGHT   32

#define bitmap_Splash_WIDTH   92
#define bitmap_Splash_HEIGHT   17

#define bitmap_Ver_WIDTH   21
#define bitmap_Ver_HEIGHT   8


// Rotary encoder pins
// Switch pin of the rotary encoder

// Global Variables
SparkFun_APDS9960 apds = SparkFun_APDS9960();
      // Last state of pin B

String lastGesture = "0";



void setup() {
  // Initialize Serial port
  int i = 0;
  Serial.begin(115200);
  Serial.println(F("--------------------------------"));
  Serial.println(F("Jestr OS V1.0"));
  Serial.println(F("--------------------------------"));
  SerialBT.begin("Jestr V1.0");
  bleKeyboard.begin();
  // Initialize the LCD

  // Initialize the gesture sensor
  if (apds.init()) {
    Serial.println(F("APDS-9960 initialization complete"));
  } else {
    Serial.println(F("Something went wrong during APDS-9960 init!"));
  }

  // Start running the APDS-9960 gesture sensor engine
  if (apds.enableGestureSensor(true)) {
    Serial.println(F("Gesture sensor is now running"));
  } else {
    Serial.println(F("Something went wrong during gesture sensor init!"));
  }

  // Set up encoder pins
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

display.clearDisplay();
display.display();
delay(500);



  int jPos = 0-bitmap_Jestr_WIDTH;

  display.clearDisplay();
  while(jPos < 128 - bitmap_Jestr_WIDTH){
    display.clearDisplay();
    display.drawBitmap(jPos,0,epd_bitmap_Jestr,bitmap_Jestr_WIDTH,bitmap_Jestr_HEIGHT, SSD1306_WHITE);
    jPos++;
    delay(2);
    display.display();
  }
  delay(1000);
  String selectedJest = jests[random(0,numJests)];
  display.setTextSize(1);             // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setCursor(3,5);
  for (int i = 0; i < selectedJest.length(); i++) {
    char c = selectedJest.charAt(i);
    if (c == '~') {
    display.println();  // If the character is '~', do a newline
  } else {
    display.print(c);   // Otherwise print the character
  }
  delay(70); // wait 100 milliseconds between characters (adjust as needed)
  display.display();
}

delay(1000);

  jPos--;
  while(jPos > -5){
    display.drawBitmap(jPos,0,epd_bitmap_Jestr,bitmap_Jestr_WIDTH,bitmap_Jestr_HEIGHT, SSD1306_WHITE);
    if(jPos > 80)jPos--;
    else if (jPos > 30)jPos-=2;
    else jPos-=3;
    
    delay(1);
    display.display();
  }
  jPos = 0;
  
  
  
  for(i=15; i>0; i--) {
    display.invertDisplay(i%2 == 1);
    if(i == 4){
        display.clearDisplay();
      display.drawBitmap(jPos,0,epd_bitmap_Jestr,bitmap_Jestr_WIDTH,bitmap_Jestr_HEIGHT, SSD1306_WHITE);
      display.drawBitmap(32,2,epd_bitmap_splash_title,bitmap_Splash_WIDTH,bitmap_Splash_HEIGHT, SSD1306_WHITE);
      display.drawBitmap(100,23,epd_bitmap_splash_version,bitmap_Ver_WIDTH,bitmap_Ver_HEIGHT, SSD1306_WHITE);
      
    }
    display.display();
    delay(random(i * 1.5,pow(i * 1.5,2)));
  }
  display.invertDisplay(false);

  display.display();
  delay(2500);
  display.clearDisplay();
  
  display.drawBitmap(100,23,epd_bitmap_splash_version,bitmap_Ver_WIDTH,bitmap_Ver_HEIGHT, SSD1306_WHITE);
  display.display();
  delay(600);
  display.clearDisplay();
  display.display();

}

void blinkLED(){

  digitalWrite(LED_PIN, HIGH);
  delay(300);
  digitalWrite(LED_PIN, LOW);

}

void loop() {
  // Check for gestures and display on LCD
  String gesture = handleGesture();
  if (gesture != "NONE"){
      blinkLED();
  }
  listenForBluetoothCommands();
   // Small delay for readability
}

String handleGesture() {
  String gesture = "NONE";  // Default to NONE
  if (apds.isGestureAvailable()) {
    switch (apds.readGesture()) {


      case DIR_UP:
        gesture = "DOWN";
        break;

      case DIR_DOWN:
        gesture = "UP";
        break;

      case DIR_LEFT:
        gesture = "RIGHT";
        break;

      case DIR_RIGHT:
        gesture = "LEFT";
        break;

      case DIR_NEAR:
        gesture = "NEAR";
        break;

      case DIR_FAR:
        gesture = "FAR";     
        break;
    

    }
    
    display.clearDisplay();

  display.setTextSize(2);             // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setCursor(0,0);    // Print gesture to Serial Monitor
  display.println(gesture);
  display.display();
  
  SerialBT.println(gesture);

    }
  
  return gesture; // Return the detected gesture
}

void listenForBluetoothCommands() {
  // Check if data is available on the Bluetooth serial
  if (SerialBT.available()) {
    // Read the incoming message until a newline character
    String received = SerialBT.readStringUntil('\n');
    received.trim(); // Remove any leading/trailing whitespace
    Serial.print("Received command: ");
    Serial.println(received);

    // Respond according to the received command
    if (received == "HELLO") {
      SerialBT.println("Hi Unity!");
    }
    else if (received == "GET_GESTURE") {
      // If you want to return the current gesture,
      // you might call handleGesture() or use a stored value.
      String currentGesture = handleGesture();
      SerialBT.println("Current Gesture: " + currentGesture);
    }
    else if (received == "PING") {
      SerialBT.println("PONG");
    }
    else {
      SerialBT.println("Unknown command: " + received);
    }
  }
}



