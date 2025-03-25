#include <Wire.h> 
#include <SparkFun_APDS9960.h>
#include <BluetoothSerial.h>
#include <BleKeyboard.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <JestrOSbitmaps.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>

// ----------------------------
// Pin Definitions and Globals
// ----------------------------

// Gesture sensor and LED pins
#define APDS9960_INT 2 // Needs to be an interrupt pin
#define LED_PIN 5
#define NUM_LEDS 5
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// OLED display settings
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3C for 128x64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Bitmaps dimensions (from JestrOSbitmaps.h)
#define bitmap_Jestr_WIDTH   31
#define bitmap_Jestr_HEIGHT   32
#define bitmap_Splash_WIDTH   92
#define bitmap_Splash_HEIGHT   17
#define bitmap_Ver_WIDTH   21
#define bitmap_Ver_HEIGHT   8

#define LEDDelay 70
#define LEDBrightness 40

// ----------------------------
// Button pin definitions
// (Change these variables to assign the desired pin numbers)
#define BUTTON1_PIN 14
#define BUTTON2_PIN 27
#define BUTTON3_PIN 26

// ----------------------------
// Other Global Variables
SparkFun_APDS9960 apds = SparkFun_APDS9960();
String lastGesture = "0";

BluetoothSerial SerialBT;
BleKeyboard bleKeyboard("Jestr v1.0", "Automaton Creations", 100);

// ----------------------------
// Function Prototypes
String handleGesture();
void listenForBluetoothCommands();
void sendMessage(String msg);
void blinkLED();
String checkButtons();
void parseSongPackage(String input);

struct SongPackage {
  String currentSong;      // Current Song
  String currentAlbum;     // Current Playlist
  String currentArtist;    // Current Artist
  String currentSongTime;  // Current Song Time
  String currentSongLength;// Current Song Length
  int currentSongPercentage;
};

SongPackage CurrentSong;

// ----------------------------
// Setup Function
// ----------------------------
void setup() {
  // Initialize Serial port
  Serial.begin(115200);
  Serial.println(F("--------------------------------"));
  Serial.println(F("Jestr OS V1.0"));
  Serial.println(F("--------------------------------"));
  
  // Initialize Bluetooth and Keyboard
  bleKeyboard.begin();
  SerialBT.begin("Jestr V1.0");

  // Initialize button pins as inputs with internal pull-ups
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(BUTTON3_PIN, INPUT_PULLUP);
  
  // Initialize the gesture sensor
  if (apds.init()) {
    Serial.println(F("APDS-9960 initialization complete"));
  } else {
    Serial.println(F("Something went wrong during APDS-9960 init!"));
  }
  if (apds.enableGestureSensor(true)) {
    Serial.println(F("Gesture sensor is now running"));
  } else {
    Serial.println(F("Something went wrong during gesture sensor init!"));
  }

  // Initialize the OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.display();
  delay(500);

  // Run a simple intro animation (splash screen)
  int jPos = 0 - bitmap_Jestr_WIDTH;
  display.clearDisplay();
  while(jPos < 128 - bitmap_Jestr_WIDTH){
    display.clearDisplay();
    display.drawBitmap(jPos, 0, epd_bitmap_Jestr, bitmap_Jestr_WIDTH, bitmap_Jestr_HEIGHT, SSD1306_WHITE);
    jPos++;
    delay(2);
    display.display();
  }
  delay(1000);

  String selectedJest = jests[random(0, numJests)];
  display.setTextSize(1);             
  display.setTextColor(SSD1306_WHITE);       
  display.setCursor(3, 5);
  for (int i = 0; i < selectedJest.length(); i++) {
    char c = selectedJest.charAt(i);
    if (c == '~') {
      display.println();  // Newline on '~'
    } else {
      display.print(c);
    }
    delay(70); // wait 70 milliseconds between characters
    display.display();
  }
  delay(1000);

  jPos--;
  while(jPos > -5){
    display.drawBitmap(jPos, 0, epd_bitmap_Jestr, bitmap_Jestr_WIDTH, bitmap_Jestr_HEIGHT, SSD1306_WHITE);
    if(jPos > 80)
      jPos--;
    else if (jPos > 30)
      jPos -= 2;
    else 
      jPos -= 3;
    delay(1);
    display.display();
  }
  jPos = 0;
  
  for(int i = 15; i > 0; i--) {
    display.invertDisplay(i % 2 == 1);
    if(i == 4){
      display.clearDisplay();
      display.drawBitmap(jPos, 0, epd_bitmap_Jestr, bitmap_Jestr_WIDTH, bitmap_Jestr_HEIGHT, SSD1306_WHITE);
      display.drawBitmap(32, 2, epd_bitmap_splash_title, bitmap_Splash_WIDTH, bitmap_Splash_HEIGHT, SSD1306_WHITE);
      display.drawBitmap(100, 23, epd_bitmap_splash_version, bitmap_Ver_WIDTH, bitmap_Ver_HEIGHT, SSD1306_WHITE);
    }
    display.display();
    delay(random(i * 1.5, pow(i * 1.5, 2)));
  }
  display.invertDisplay(false);
  display.display();
  delay(2500);
  display.clearDisplay();
  display.drawBitmap(100, 23, epd_bitmap_splash_version, bitmap_Ver_WIDTH, bitmap_Ver_HEIGHT, SSD1306_WHITE);
  display.display();
  delay(600);
  display.clearDisplay();
  display.display();
}

// ----------------------------
// Main Loop
// ----------------------------
void loop() {
  // Process Bluetooth commands and gestures
  listenForBluetoothCommands();
  String gesture = handleGesture();
  if (gesture != "NONE"){
      blinkLED();
  }
  
  // Check for button presses and display result
  String buttonPressed = checkButtons();
  if (buttonPressed != "NONE") {
      // Clear a specific area on the OLED (adjust as needed)
      display.fillRect(0, 50, 128, 14, SSD1306_BLACK);
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 50);
      display.println(buttonPressed + " pressed");
      display.display();
      sendMessage(buttonPressed + " pressed");
      delay(200); // Debounce delay
  }
  
  delay(50); // Small delay for readability
}

// ----------------------------
// New Function: checkButtons()
// Checks the state of BUTTON1, BUTTON2, and BUTTON3.
// Returns a string indicating which button is pressed,
// or "NONE" if no button is pressed.
String checkButtons() {
  if (digitalRead(BUTTON1_PIN) == LOW) {
      return "BUTTON 1";
  } else if (digitalRead(BUTTON2_PIN) == LOW) {
      return "BUTTON 2";
  } else if (digitalRead(BUTTON3_PIN) == LOW) {
      return "BUTTON 3";
  }
  return "NONE";
}

// ----------------------------
// Existing Functions
// ----------------------------
void blinkLED(){
  digitalWrite(LED_PIN, HIGH);
  delay(300);
  digitalWrite(LED_PIN, LOW);
}

String handleGesture() {
  String gesture = "NONE";  // Default to NONE
  if (apds.isGestureAvailable()) {
    switch (apds.readGesture()) {
      case DIR_UP:
        gesture = "LEFT";
        break;
      case DIR_DOWN:
        gesture = "RIGHT";
        break;
      case DIR_LEFT:
        gesture = "DOWN";
        break;
      case DIR_RIGHT:
        gesture = "UP";
        break;
      case DIR_NEAR:
        gesture = "NEAR";
        break;
      case DIR_FAR:
        gesture = "FAR";
        break;
    }
    // Display gesture text on OLED
    display.fillRect(95, 32, 33, 11, SSD1306_BLACK);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(95, 32);
    if(gesture == "RIGHT"){
      display.println("SKIP");
    }
    if(gesture == "LEFT"){
      display.println("REW");
    }
    display.display();
    sendMessage(gesture);
    
    // LED animation based on gesture (example animation)
    strip.setBrightness(128);
    strip.clear();
    strip.show();
    if (gesture == "LEFT") {
      strip.setPixelColor(4, strip.Color(LEDBrightness, LEDBrightness, LEDBrightness));
      strip.show();
      delay(LEDDelay);
      strip.setPixelColor(1, strip.Color(LEDBrightness, LEDBrightness, LEDBrightness));
      strip.show();
      delay(LEDDelay);
      strip.setPixelColor(2, strip.Color(LEDBrightness, LEDBrightness, LEDBrightness));
      strip.show();
      delay(LEDDelay);
      strip.clear();
      strip.show();
    }
    else if (gesture == "RIGHT") {
      strip.setPixelColor(2, strip.Color(LEDBrightness, LEDBrightness, LEDBrightness));
      strip.show();
      delay(LEDDelay);
      strip.setPixelColor(1, strip.Color(LEDBrightness, LEDBrightness, LEDBrightness));
      strip.show();
      delay(LEDDelay);
      strip.setPixelColor(4, strip.Color(LEDBrightness, LEDBrightness, LEDBrightness));
      strip.show();
      delay(LEDDelay);
      strip.clear();
      strip.show();
    }
    else if (gesture == "UP") {
      strip.setPixelColor(0, strip.Color(LEDBrightness, LEDBrightness, LEDBrightness));
      strip.show();
      delay(LEDDelay);
      strip.setPixelColor(1, strip.Color(LEDBrightness, LEDBrightness, LEDBrightness));
      strip.show();
      delay(LEDDelay);
      strip.setPixelColor(3, strip.Color(LEDBrightness, LEDBrightness, LEDBrightness));
      strip.show();
      delay(LEDDelay);
      strip.clear();
      strip.show();
    }
    else if (gesture == "DOWN") {
      strip.setPixelColor(3, strip.Color(LEDBrightness, LEDBrightness, LEDBrightness));
      strip.show();
      delay(LEDDelay);
      strip.setPixelColor(1, strip.Color(LEDBrightness, LEDBrightness, LEDBrightness));
      strip.show();
      delay(LEDDelay);
      strip.setPixelColor(0, strip.Color(LEDBrightness, LEDBrightness, LEDBrightness));
      strip.show();
      delay(LEDDelay);
      strip.clear();
      strip.show();
    }
    else if (gesture == "NEAR" || gesture == "FAR" ) {
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, strip.Color(LEDBrightness, LEDBrightness, LEDBrightness));
      }
      strip.show();
      delay(LEDDelay * 2.5);
      strip.clear();
      strip.show();
    }
  }
  strip.clear();
  strip.show();
  return gesture;
}

void listenForBluetoothCommands() {
  if (SerialBT.available()) {
    String received = SerialBT.readStringUntil('\n');
    received.trim();
    sendMessage("Received command: ");
    Serial.println(received);
    sendMessage(received);
    String identifier = received.substring(0, 4);
    String pckg = received.substring(4);
    if(identifier == "SPKG"){
      parseSongPackage(pckg);
    }
    if(identifier == "PING"){
      sendMessage("PONG");
    }
  }
}

void sendMessage(String msg){
  SerialBT.println(msg);
  SerialBT.flush();
}

void parseSongPackage(String input){
  int startIndex = 0;
  int delimIndex = 0;
  while ((delimIndex = input.indexOf('*', startIndex)) != -1) {
    String token = input.substring(startIndex, delimIndex);
    String identifier = input.substring(startIndex, startIndex+4);
    String pckg = input.substring(startIndex+4, delimIndex);
    if(identifier == "CSNG"){
      CurrentSong.currentSong = pckg;
      Serial.println("Song = " + pckg);
    }
    if(identifier == "CART"){
      CurrentSong.currentArtist = pckg;
      Serial.println("Artist = " + pckg);
    }
    if(identifier == "CABM"){
      CurrentSong.currentAlbum = pckg;
      Serial.println("Album = " + pckg);
    }
    if(identifier == "CSTM"){
      CurrentSong.currentSongTime = pckg;
      Serial.println("Song Time = " + pckg);
    }
    if(identifier == "CSLN"){
      CurrentSong.currentSongLength = pckg;
      Serial.println("Song Length = " + pckg);
    }
    if(identifier == "CPRG"){
      CurrentSong.currentSongPercentage = pckg.toInt();
      Serial.println("Song Progress = " + pckg);
    }
    startIndex = delimIndex + 1;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);    
  display.println(CurrentSong.currentSong);
  display.setCursor(0,25); 
  display.println(CurrentSong.currentArtist);
  display.setCursor(100,45);  
  display.println(CurrentSong.currentSongLength);
  display.setCursor(0,45);    
  display.println(CurrentSong.currentSongTime);
  display.drawLine(2, 60, 2 + CurrentSong.currentSongPercentage, 60, SSD1306_WHITE);
  display.drawLine(2, 61, 2 + CurrentSong.currentSongPercentage, 61, SSD1306_WHITE);
  display.display();
}
