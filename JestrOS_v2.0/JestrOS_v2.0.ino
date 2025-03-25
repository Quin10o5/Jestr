#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <SparkFun_APDS9960.h>
#include <math.h>
#include <JestrOSbitmaps.h>  // Your bitmaps, if used
#include <Base64.h>
#include <Preferences.h>
#include <string.h>
#include <Adafruit_NeoPixel.h>

#define APDS9960_INT 2 // Needs to be an interrupt pin
#define LED_PIN 5
#define NUM_LEDS 5
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
#define LEDDelay 30
#define LEDBrightness 40

// ----- Hardware configuration -----
#define BUTTON1_PIN 14
#define BUTTON2_PIN 27
#define BUTTON3_PIN 26

#define bitmap_Jestr_WIDTH   31
#define bitmap_Jestr_HEIGHT   32
#define bitmap_Splash_WIDTH   92
#define bitmap_Splash_HEIGHT   17
#define bitmap_Ver_WIDTH   21
#define bitmap_Ver_HEIGHT   8

// ----- OLED display configuration -----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET   -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ----- Gesture sensor -----
SparkFun_APDS9960 apds;

// ----- Global variables: WiFi scan & selection -----
bool wifiSelectionMode = false;    // set true if no saved WiFi or connection fails
String wifiNetworks[20];           // holds up to 20 SSIDs
int wifiCount = 0;                 // number of networks found
int currentPage = 0;               // current page (6 items per page)
int selectedIndex = 0;             // index (0-5) within current page

// ----- Global variables: Direct Connect Keyboard UI -----
String ssidInput = "";
String passwordInput = "";
int editingField = 0; // 0 = editing SSID, 1 = editing Password
bool shifted = false;
int keyboardIndex = 0;
const char* keyboardChars = "abcdefghijklmnopqrstuvwxyz0123456789 .,!?@#";
int keyboardLength = 0;

// ----- Spotify API & OAuth variables -----
const char* spotifyClientId     = "ecfa74e65105479b95a7d8070a8f8bd7";
const char* spotifyClientSecret = "";
const char* redirectURI         = "http://172.20.10.9/callback";  // adjust as needed

String spotifyAuthURL = String("https://accounts.spotify.com/authorize?client_id=") +
                        spotifyClientId +
                        "&response_type=code&redirect_uri=" +
                        redirectURI +
                        "&scope=user-read-playback-state%20user-read-currently-playing";

const char* tokenURL  = "https://accounts.spotify.com/api/token";
const char* spotifyURL = "https://api.spotify.com/v1/me/player/currently-playing";

// ----- Preferences for storing tokens and WiFi credentials -----
Preferences preferences;
String accessToken  = "";
String refreshToken = "";
unsigned long tokenExpiryMillis = 0;  // When the access token expires (millis)

// ----- Web Server for Spotify configuration -----
WebServer server(80);
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
  <head>
    <title>Spotify Setup</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
  </head>
  <body>
    <h2>Configure Spotify</h2>
    <p>Click the button below to log into Spotify.</p>
    <a href="%SPOTIFY_AUTH_URL%">
      <button style="font-size:20px;">Login with Spotify</button>
    </a>
  </body>
</html>
)rawliteral";

// ----- Global variables for Playlist Selection -----
// Now store up to 15 playlists (or fewer if not available)
const int MAX_PLAYLISTS = 15;
String playlistNames[MAX_PLAYLISTS];
String playlistURIs[MAX_PLAYLISTS];
int playlistCount = 0;

// ----- Global variable for Shuffle State -----
bool isShuffled = true;  // current shuffle state

// ----- Function Prototypes -----
void handleRoot();
void handleCallback();
void setupWebServer();
bool exchangeAuthCodeForToken(const String& code);
bool refreshAccessToken();
void fetchSpotifyData();

void scanWifiNetworks();
void displayWifiList();
String handleGesture();
void processWifiSelection();
bool attemptWifiConnect(const String &ssid, const String &password);
void storeWifiCredentials(const String &ssid, const String &password);
void directConnectScreen(String preFilledSSID);  // preFilledSSID may be empty
void startupAnimation();

// Playlist & Transition functions:
bool fetchPlaylists();
void processPlaylistSelection();
void showLoadingTransition(String playlistName);
void spotifyPlayPlaylist(String playlistUri);

// Shuffle toggle function:
void toggleShuffle();

// ----- Web Server Handlers -----
void handleRoot() {
  String page(index_html);
  page.replace("%SPOTIFY_AUTH_URL%", spotifyAuthURL);
  server.send(200, "text/html", page);
}

void handleCallback() {
  if (server.hasArg("code")) {
    String authCode = server.arg("code");
    Serial.print("Received auth code: ");
    Serial.println(authCode);
    if (exchangeAuthCodeForToken(authCode)) {
      server.send(200, "text/html", "<h2>Authorization Successful!</h2><p>You can now use the device.</p>");
    } else {
      server.send(500, "text/html", "Token exchange failed.");
    }
  } else {
    server.send(400, "text/html", "Authorization failed");
  }
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/callback", handleCallback);
  server.begin();
  Serial.println("Web server started on port 80");
}

// ----- Spotify Token Persistence -----
void storeSpotifyTokens() {
  preferences.putString("accessToken", accessToken);
  preferences.putString("refreshToken", refreshToken);
  preferences.putULong("tokenExpiry", tokenExpiryMillis);
  Serial.println("Spotify tokens saved.");
}

void loadSpotifyTokens() {
  accessToken  = preferences.getString("accessToken", "");
  refreshToken = preferences.getString("refreshToken", "");
  tokenExpiryMillis = preferences.getULong("tokenExpiry", 0);
  if (accessToken.length() > 0) {
    Serial.println("Loaded stored Spotify tokens.");
  } else {
    Serial.println("No stored Spotify tokens found.");
  }
}

// ----- WiFi Credentials Persistence -----
void storeWifiCredentials(const String &ssid, const String &password) {
  preferences.putString("favSSID", ssid);
  preferences.putString("favPassword", password);
  Serial.println("WiFi credentials saved.");
}

bool loadWifiCredentials(String &ssid, String &password) {
  ssid = preferences.getString("favSSID", "");
  password = preferences.getString("favPassword", "");
  return (ssid != "");
}

// ----- OAuth Functions (Spotify) -----
bool exchangeAuthCodeForToken(const String& code) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, tokenURL);

  String postData = "grant_type=authorization_code&code=" + code + "&redirect_uri=" + String(redirectURI);
  String credentials = String(spotifyClientId) + ":" + String(spotifyClientSecret);
  String authHeader = "Basic " + base64::encode(credentials);

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Authorization", authHeader);

  int httpResponseCode = http.POST(postData);
  String payload = http.getString();
  Serial.print("Exchange response code: ");
  Serial.println(httpResponseCode);
  Serial.println(payload);

  if (httpResponseCode == 200) {
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, payload)) {
      Serial.println("JSON parse error during token exchange.");
      http.end();
      return false;
    }
    accessToken  = doc["access_token"]  | "";
    refreshToken = doc["refresh_token"] | "";
    int expiresIn = doc["expires_in"]   | 0;
    tokenExpiryMillis = millis() + (expiresIn * 1000);
    Serial.println("Access token: " + accessToken);
    Serial.println("Refresh token: " + refreshToken);
    http.end();
    storeSpotifyTokens();
    return true;
  } else {
    Serial.print("Token exchange failed. Code: ");
    Serial.println(httpResponseCode);
    http.end();
    return false;
  }
}

bool refreshAccessToken() {
  if (refreshToken.isEmpty()) {
    Serial.println("No refresh token available.");
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, tokenURL);

  String postData = "grant_type=refresh_token&refresh_token=" + refreshToken;
  String credentials = String(spotifyClientId) + ":" + String(spotifyClientSecret);
  String authHeader = "Basic " + base64::encode(credentials);

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Authorization", authHeader);

  int httpResponseCode = http.POST(postData);
  String payload = http.getString();
  Serial.print("Refresh response code: ");
  Serial.println(httpResponseCode);
  Serial.println(payload);

  if (httpResponseCode == 200) {
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, payload)) {
      Serial.println("JSON parse error during token refresh.");
      http.end();
      return false;
    }
    accessToken = doc["access_token"] | "";
    int expiresIn = doc["expires_in"] | 0;
    tokenExpiryMillis = millis() + (expiresIn * 1000);
    Serial.println("New access token: " + accessToken);
    http.end();
    storeSpotifyTokens();
    return true;
  } else {
    Serial.print("Token refresh failed. Code: ");
    Serial.println(httpResponseCode);
    http.end();
    return false;
  }
}

// ----- Removed Volume Control Global and Functions -----
// (Volume control functions have been removed per the new requirements)

// --- Fix in fetchSpotifyData() ---
void fetchSpotifyData() {
  // Refresh token if nearly expired
  if (!accessToken.isEmpty() && (millis() > tokenExpiryMillis - 20000)) {
    Serial.println("Token near expiry, refreshing...");
    if (!refreshAccessToken()) {
      Serial.println("Failed to refresh token!");
      return;
    }
  }
  if (accessToken.isEmpty()) {
    Serial.println("No access token. Please complete OAuth via configuration page.");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0);
    display.println("Visit http://");
    display.println(WiFi.localIP());
    display.println(" to configure Spotify.");
    display.display();
    return;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, spotifyURL);
  http.addHeader("Authorization", String("Bearer ") + accessToken);
  int httpResponseCode = http.GET();
  if (httpResponseCode == 401) {
    Serial.println("Received 401, refreshing token and retrying...");
    http.end();
    if (refreshAccessToken()) {
      client.setInsecure();
      http.begin(client, spotifyURL);
      http.addHeader("Authorization", String("Bearer ") + accessToken);
      httpResponseCode = http.GET();
    }
  }
  if (httpResponseCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, payload)) {
      Serial.println("JSON parse error in fetchSpotifyData.");
      http.end();
      return;
    }
    const char* songName   = doc["item"]["name"]   | "N/A";
    const char* artistName = doc["item"]["artists"][0]["name"] | "N/A";
    long progress_ms = doc["progress_ms"] | 0;
    long duration_ms = doc["item"]["duration_ms"] | 0;
    int totalSecs = duration_ms / 1000;
    int totalMin  = totalSecs / 60;
    int totalSec  = totalSecs % 60;
    
    int currentSecs = progress_ms / 1000;
    int currentMin = currentSecs / 60;
    int currentSec = currentSecs % 60;  // <-- Added missing semicolon correction
    float fraction = (duration_ms > 0) ? (float)progress_ms / duration_ms : 0;
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0);    
    display.println(songName);
    display.setCursor(0,25); 
    display.println(artistName);
    display.setCursor(100,45);  
    display.println(String(totalMin) + ":" + padSingleDigit(String(totalSec)) + "  ");
    display.setCursor(0,45);    
    display.println(String(currentMin) + ":" + padSingleDigit(String(currentSec)));
    int prog = fraction * 124;
    display.drawLine(2, 60, 2 + prog, 60, SSD1306_WHITE);
    display.drawLine(2, 61, 2 + prog, 61, SSD1306_WHITE);

    if(isShuffled){
      display.drawBitmap(30, 45, epd_bitmap_shuffle, 9, 7, SSD1306_WHITE);
    }
    else{
      display.fillRect(30, 45, 9, 7, SSD1306_BLACK);
    }


    display.display();
    
    Serial.println("Spotify Data:");
    Serial.print("Song: "); Serial.println(songName);
    Serial.print("Artist: "); Serial.println(artistName);
    Serial.print("Time: ");
    Serial.print(progress_ms / 1000);
    Serial.print(" / ");
    Serial.println(totalSecs);
  } else {
    Serial.print("Spotify API error code: ");
    Serial.println(httpResponseCode);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Spotify error:");
    display.println(httpResponseCode);
    display.display();
  }
  http.end();
}

String padSingleDigit(String input) {
  if (input.length() == 1 && isDigit(input.charAt(0))) {
    return "0" + input;
  }
  return input;
}

// --- Spotify Control Functions for Skipping/Previous ---
void spotifySkipTrack() {
  if (accessToken == "") {
    Serial.println("No access token. Cannot skip track.");
    return;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, "https://api.spotify.com/v1/me/player/next");
  http.addHeader("Authorization", String("Bearer ") + accessToken);
  http.addHeader("Content-Length", "0");
  int httpResponseCode = http.POST("");
  if (httpResponseCode == 204) {
    Serial.println("Track skipped.");
  } else {
    Serial.print("Error skipping track. Code: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void spotifyPreviousTrack() {
  if (accessToken == "") {
    Serial.println("No access token. Cannot go to previous track.");
    return;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, "https://api.spotify.com/v1/me/player/previous");
  http.addHeader("Authorization", String("Bearer ") + accessToken);
  http.addHeader("Content-Length", "0");
  int httpResponseCode = http.POST("");
  if (httpResponseCode == 204) {
    Serial.println("Went to previous track.");
  } else {
    Serial.print("Error going to previous track. Code: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

// --- New Playlist Functions ---
// Fetch the most recent playlists (up to 15)
bool fetchPlaylists() {
  if (accessToken == "") {
    Serial.println("No access token. Cannot fetch playlists.");
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.spotify.com/v1/me/playlists?limit=15";
  http.begin(client, url);
  http.addHeader("Authorization", String("Bearer ") + accessToken);
  int httpResponseCode = http.GET();
  if (httpResponseCode != 200) {
    Serial.print("Error fetching playlists. Code: ");
    Serial.println(httpResponseCode);
    http.end();
    return false;
  }
  String payload = http.getString();
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, payload)) {
    Serial.println("JSON parse error in fetchPlaylists.");
    http.end();
    return false;
  }
  playlistCount = 0;
  JsonArray items = doc["items"].as<JsonArray>();
  for (JsonObject item : items) {
    if (playlistCount < MAX_PLAYLISTS) {
      playlistNames[playlistCount] = String(item["name"].as<const char*>());
      playlistURIs[playlistCount] = String(item["uri"].as<const char*>());
      playlistCount++;
    }
  }
  http.end();
  return true;
}

// Display a transition screen before playing a selected playlist.
void showLoadingTransition(String playlistName) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("Loading..");
  display.println(playlistName);
  display.display();
  delay(2000); // pause 2 seconds for the transition
}

// Allow the user to select one of the fetched playlists with paging.
void processPlaylistSelection() {
  if (!fetchPlaylists()) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Failed to fetch");
    display.println("playlists.");
    display.display();
    delay(2000);
    return;
  }
  if (playlistCount == 0) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("No playlists");
    display.println("found.");
    display.display();
    delay(2000);
    return;
  }
  
  const int pageSize = 6;
  int totalPages = (playlistCount + pageSize - 1) / pageSize;
  int page = 0;
  int selectedIndexInPage = 0;
  
  bool selecting = true;
  while (selecting) {
    display.clearDisplay();
    display.setTextSize(1);
    int start = page * pageSize;
    int end = min(start + pageSize, playlistCount);
    for (int i = start; i < end; i++) {
      int y = (i - start) * 10;
      if ((i - start) == selectedIndexInPage) {
        display.fillRect(0, y, SCREEN_WIDTH, 10, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }
      display.setCursor(0, y);
      display.println(playlistNames[i]);
    }
    display.display();
    
    String gesture = handleGesture();
    if (gesture == "UP") {
      if (selectedIndexInPage > 0) {
        selectedIndexInPage--;
      } else if (page > 0) {
        page--;
        int itemsInPage = min(pageSize, playlistCount - page * pageSize);
        selectedIndexInPage = itemsInPage - 1;
      }
      delay(300);
    } else if (gesture == "DOWN") {
      int itemsInPage = min(pageSize, playlistCount - page * pageSize);
      if (selectedIndexInPage < itemsInPage - 1) {
        selectedIndexInPage++;
      } else if (page < totalPages - 1) {
        page++;
        selectedIndexInPage = 0;
      }
      delay(300);
    } else if (gesture == "LEFT") {
      // Cancel selection and return to now playing screen.
      selecting = false;
      delay(300);
    } else if (gesture == "RIGHT") {
      // Confirm selection.
      int absoluteIndex = page * pageSize + selectedIndexInPage;
      String selectedPlaylistName = playlistNames[absoluteIndex];
      // Show transition screen.
      showLoadingTransition(selectedPlaylistName);
      // Start playing the selected playlist.
      spotifyPlayPlaylist(playlistURIs[absoluteIndex]);
      selecting = false;
      delay(300);
    }
    delay(50);
  }
}

// Send a command to Spotify to start playing the given playlist.
void spotifyPlayPlaylist(String playlistUri) {
  if (accessToken == "") {
    Serial.println("No access token. Cannot play playlist.");
    return;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, "https://api.spotify.com/v1/me/player/play");
  http.addHeader("Authorization", String("Bearer ") + accessToken);
  
  DynamicJsonDocument doc(256);
  doc["context_uri"] = playlistUri;
  String jsonPayload;
  serializeJson(doc, jsonPayload);
  
  int httpResponseCode = http.PUT(jsonPayload);
  if (httpResponseCode == 204) {
    Serial.println("Started playing playlist: " + playlistUri);
  } else {
    Serial.print("Error playing playlist. Code: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

// ----- Shuffle Toggle Function -----
// Sends a request to toggle shuffle mode and updates the isShuffled variable.
void toggleShuffle() {
  if (accessToken == "") {
    Serial.println("No access token. Cannot toggle shuffle.");
    return;
  }
  bool newShuffle = !isShuffled;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.spotify.com/v1/me/player/shuffle?state=" + String(newShuffle ? "true" : "false");
  http.begin(client, url);
  http.addHeader("Authorization", String("Bearer ") + accessToken);
  http.addHeader("Content-Length", "0");
  int httpResponseCode = http.PUT("");
  if (httpResponseCode == 204 || httpResponseCode == 200) {
    isShuffled = newShuffle;
    Serial.print("Shuffle toggled to ");
    Serial.println(isShuffled ? "ON" : "OFF");
  } else {
    Serial.print("Failed to toggle shuffle. Code: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}



// ----- WiFi Scanning & Selection Functions -----
void scanWifiNetworks() {
  Serial.println("Scanning for WiFi networks...");
  int n = WiFi.scanNetworks(true);
  startupAnimation();
  WiFi.scanDelete();
  yield();
  if (n <= 0) {
    Serial.println("No WiFi networks found.");
    wifiCount = 0;
    return;
  }
  wifiCount = n;
  Serial.print("Found ");
  Serial.print(wifiCount);
  Serial.println(" networks.");
  for (int i = 0; i < wifiCount && i < 20; i++) {
    wifiNetworks[i] = WiFi.SSID(i);
    Serial.print(i);
    Serial.print(": ");
    Serial.println(wifiNetworks[i]);
  }
}

void displayWifiList() {
  display.clearDisplay();
  display.setTextSize(1);
  if (wifiCount <= 0) {
    display.setCursor(0, 0);
    display.println("No networks found.");
    display.println("Direct Connect");
    display.display();
    return;
  }
  int startIndex = currentPage * 6;
  for (int i = 0; i < 6; i++) {
    int index = startIndex + i;
    if (index >= wifiCount) break;
    int y = i * 10;
    if (i == selectedIndex) {
      display.fillRect(0, y, SCREEN_WIDTH, 10, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(0, y);
    display.println(wifiNetworks[index]);
  }
  display.display();
}

// Updated gesture handling: standard mapping.
String handleGesture() {
  String gesture = "NONE";  
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
    
    // LED animation based on gesture (example animations)
    strip.setBrightness(128);
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

void processWifiSelection() {
  if (wifiCount <= 0) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("No networks found.");
    display.println("Direct Connect");
    display.display();
    String gesture = handleGesture();
    if (gesture == "FAR") {
      directConnectScreen("");
      delay(300);
    }
    return;
  }
  
  String gesture = handleGesture();
  if (gesture == "LEFT") {
    if (selectedIndex > 0) {
      selectedIndex--;
    } else if (currentPage > 0) {
      currentPage--;
      selectedIndex = 5;
    }
    displayWifiList();
    delay(300);
  } else if (gesture == "RIGHT") {
    if ((currentPage * 6 + selectedIndex + 1) < wifiCount && selectedIndex < 5) {
      selectedIndex++;
    } else if (((currentPage + 1) * 6) < wifiCount) {
      currentPage++;
      selectedIndex = 0;
    }
    displayWifiList();
    delay(300);
  } else if (gesture == "FAR") {
    int absoluteIndex = currentPage * 6 + selectedIndex;
    String selectedSSID = wifiNetworks[absoluteIndex];
    Serial.print("Selected WiFi: ");
    Serial.println(selectedSSID);
    directConnectScreen(selectedSSID);
    delay(300);
  }
}

bool attemptWifiConnect(const String &ssid, const String &password) {
  WiFi.disconnect(true);
  delay(100);
  WiFi.begin(ssid.c_str(), password.c_str());
  unsigned long startTime = millis();
  Serial.print("Attempting connection to ");
  Serial.println(ssid);
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  return (WiFi.status() == WL_CONNECTED);
}

void directConnectScreen(String preFilledSSID) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Direct Connect Mode");
  display.println("-------------------");
  display.println("Buttons:");
  display.println("B1: Move Left  B2: Select  B3: Move Right");
  display.println("Gestures:");
  display.println("UP: Edit SSID   DOWN: Edit PWD");
  display.println("LEFT: Delete char");
  display.println("FAR: Toggle Shift");
  display.println("RIGHT: Submit (confirm with RIGHT, cancel with LEFT)");
  display.println();
  display.println("Press any button to start...");
  display.display();
  while(digitalRead(BUTTON1_PIN) == HIGH &&
        digitalRead(BUTTON2_PIN) == HIGH &&
        digitalRead(BUTTON3_PIN) == HIGH) {
    delay(10);
  }
  
  if (preFilledSSID != "") {
    ssidInput = preFilledSSID;
    editingField = 1;
  } else {
    ssidInput = "";
    editingField = 0;
  }
  passwordInput = "";
  shifted = false;
  keyboardIndex = 0;
  keyboardLength = strlen(keyboardChars);
  
  bool submitting = false;
  bool confirming = false;
  unsigned long lastButtonTime = 0;
  const unsigned long buttonDelay = 300;
  
  while (!submitting) {
    if (millis() - lastButtonTime > buttonDelay) {
      if (digitalRead(BUTTON1_PIN) == LOW) {
        keyboardIndex = (keyboardIndex - 1 + keyboardLength) % keyboardLength;
        lastButtonTime = millis();
      }
      if (digitalRead(BUTTON3_PIN) == LOW) {
        keyboardIndex = (keyboardIndex + 1) % keyboardLength;
        lastButtonTime = millis();
      }
      if (digitalRead(BUTTON2_PIN) == LOW) {
        char selectedChar = keyboardChars[keyboardIndex];
        if (shifted && selectedChar >= 'a' && selectedChar <= 'z') {
          selectedChar = selectedChar - 'a' + 'A';
          shifted = false;
        }
        if (editingField == 0) {
          ssidInput += selectedChar;
        } else {
          passwordInput += selectedChar;
        }
        lastButtonTime = millis();
      }
    }
    
    String gesture = handleGesture();
    if (!confirming) {
      if (gesture == "UP") {
        editingField = 0;
        delay(300);
      } else if (gesture == "DOWN") {
        editingField = 1;
        delay(300);
      } else if (gesture == "LEFT") {
        if (editingField == 0 && ssidInput.length() > 0) {
          ssidInput.remove(ssidInput.length() - 1);
        } else if (editingField == 1 && passwordInput.length() > 0) {
          passwordInput.remove(passwordInput.length() - 1);
        }
        delay(300);
      } else if (gesture == "FAR") {
        shifted = !shifted;
        delay(300);
      } else if (gesture == "RIGHT") {
        confirming = true;
        delay(300);
      }
    } else {
      if (gesture == "RIGHT") {
        submitting = true;
        delay(300);
      } else if (gesture == "LEFT") {
        confirming = false;
        delay(300);
      }
    }
    
    display.clearDisplay();
    display.setTextSize(1);
    int x = 0, y = 0;
    String ssidLabel = "SSID: " + ssidInput;
    int16_t bx, by;
    uint16_t bw, bh;
    display.getTextBounds(ssidLabel, 0, y, &bx, &by, &bw, &bh);
    if (editingField == 0) {
      display.fillRect(0, y, bw, bh, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(0, y);
    display.print(ssidLabel);
    
    y += bh + 2;
    String pwdLabel = "PWD: " + passwordInput;
    display.getTextBounds(pwdLabel, 0, y, &bx, &by, &bw, &bh);
    if (editingField == 1) {
      display.fillRect(0, y, bw, bh, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(0, y);
    display.print(pwdLabel);
    
    y += bh + 2;
    if (confirming) {
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, y);
      display.println("Confirm connection?");
      display.println("RIGHT: Yes, LEFT: No");
    }
    
    int kbY = display.height() - 16;
    int posLeft2 = 5, posLeft1 = 30, posCenter = 60, posRight1 = 90, posRight2 = 115;
    int indexLeft2 = (keyboardIndex - 2 + keyboardLength) % keyboardLength;
    int indexLeft1 = (keyboardIndex - 1 + keyboardLength) % keyboardLength;
    int indexRight1 = (keyboardIndex + 1) % keyboardLength;
    int indexRight2 = (keyboardIndex + 2) % keyboardLength;
    
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(posLeft2, kbY);
    display.print(keyboardChars[indexLeft2]);
    display.setCursor(posLeft1, kbY);
    display.print(keyboardChars[indexLeft1]);
    
    String centerKey = String(keyboardChars[keyboardIndex]);
    display.getTextBounds(centerKey, 0, 0, &bx, &by, &bw, &bh);
    display.fillRect(posCenter - 2, kbY - 2, bw + 4, bh + 4, SSD1306_WHITE);
    display.setCursor(posCenter, kbY);
    display.setTextColor(SSD1306_BLACK);
    display.print(centerKey);
    display.setTextColor(SSD1306_WHITE);
    
    if (shifted) {
      display.setCursor(posCenter, kbY - bh - 4);
      display.print("SHIFT");
    }
    
    display.setCursor(posRight1, kbY);
    display.print(keyboardChars[indexRight1]);
    display.setCursor(posRight2, kbY);
    display.print(keyboardChars[indexRight2]);
    
    display.display();
    delay(50);
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connecting to:");
  display.println(ssidInput);
  display.display();
  
  Serial.print("Attempting connection to ");
  Serial.println(ssidInput);
  WiFi.begin(ssidInput.c_str(), passwordInput.c_str());
  
  int timeout = 20;
  while (WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(500);
    timeout--;
  }
  
  display.clearDisplay();
  display.setCursor(0, 0);
  if (WiFi.status() == WL_CONNECTED) {
    display.println("Connected!");
    Serial.println("WiFi connected.");
    storeWifiCredentials(ssidInput, passwordInput);
    wifiSelectionMode = false;
  } else {
    display.println("Failed to connect.");
    Serial.println("WiFi connection failed.");
  }
  display.display();
  delay(3000);
}

// ----- Startup Animation -----
void startupAnimation() {
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
      display.println();
    } else {
      display.print(c);
    }
    delay(70);
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

// ----- Setup & Loop -----
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  preferences.begin("spotify", false);
  loadSpotifyTokens();
  
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(BUTTON3_PIN, INPUT_PULLUP);
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
    while (1);
  }
  display.clearDisplay();
  display.display();
  
  if (apds.init()) {
    Serial.println("APDS-9960 initialized");
    if (apds.enableGestureSensor(true)) {
      Serial.println("Gesture sensor enabled");
    } else {
      Serial.println("Gesture sensor enable failed!");
    }
  } else {
    Serial.println("APDS-9960 init failed!");
  }
  
  String favSSID, favPassword;
  if (loadWifiCredentials(favSSID, favPassword) && favSSID != "") {
    Serial.print("Attempting connection to saved WiFi: ");
    Serial.println(favSSID);
    if (!attemptWifiConnect(favSSID, favPassword)) {
      Serial.println("Failed to connect using saved credentials.");
      wifiSelectionMode = true;
    } else {
      Serial.println("Connected using saved WiFi.");
    }
  } else {
    wifiSelectionMode = true;
  }
  
  if (wifiSelectionMode) {
    scanWifiNetworks();
    currentPage = 0;
    selectedIndex = 0;
    displayWifiList();
  }
  else{
    startupAnimation();
  }
  
  setupWebServer();
}

unsigned long previousSpotifyMillis = 0;
const unsigned long spotifyInterval = 1000; // Query every 1 second

void loop() {
  server.handleClient();
  
  if (wifiSelectionMode) {
    processWifiSelection();
    delay(50);
    return;
  }
  else {
    String gest = handleGesture();
    
    // In normal mode, RIGHT gesture skips and LEFT goes to previous track.
    // The UP gesture now enters playlist selection mode.
    if(gest != "NONE"){
      if (gest == "RIGHT") {
        spotifySkipTrack();
      } else if (gest == "LEFT") {
        spotifyPreviousTrack();
      } else if (gest == "UP") {
        processPlaylistSelection();
      
      } else if (gest == "DOWN") {
        toggleShuffle();
      }
      
      // (DOWN gesture is not used in normal mode.)
      
      display.fillRect(95, 32, 33, 11, SSD1306_BLACK);
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(72, 45);
      if(gest == "RIGHT"){
        display.println("SKIP");
      }
      if(gest == "LEFT"){
        display.println("REW");
      }
    
      if(gest == "DOWN"){
        display.println("SHUF");
      }
      display.display();
    }
  
  
  unsigned long currentMillis = millis();
  if (currentMillis - previousSpotifyMillis >= spotifyInterval) {
    previousSpotifyMillis = currentMillis;
    fetchSpotifyData();
  }
}
}
