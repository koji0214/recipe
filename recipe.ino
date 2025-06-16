#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h> // 日付表示用
#include <Fonts/FreeSansBold9pt7b.h>  // 日付一覧用
#include "wifi_config.h"

const char* ssid = WIFI_SSID;           // Wi-FiのSSID
const char* password = WIFI_PASSWORD;   // Wi-Fiパスワード

WebServer server(80);  // ポート80で待き受け

// --- E-Paperディスプレイのピン定義 ---
#define EPD_CS    7  // Chip Select (SS)      -> XIAO D5 / SCL (GPIO7) に接続
#define EPD_DC    5  // Data/Command          -> XIAO D3 / A3 (GPIO5) に接続
#define EPD_RST   2  // Reset                 -> XIAO D0 / A0 (GPIO2) に接続
#define EPD_BUSY  3  // Busy signal from E-Paper -> XIAO D1 / A1 (GPIO3) に接続

// ディスプレイオブジェクトの初期化
GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(
  GxEPD2_420_GDEY042T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// --- ボタン制御用の変数 ---
#define BUTTON_PIN_1 8  // 1つ目のボタンを接続するGPIOピン (XIAO D8 / SCK)
#define BUTTON_PIN_2 10 // 2つ目のボタンを接続するGPIOピン (XIAO D10 / MOSI)

long lastButton1PressTime = 0; // 1つ目のボタン用デバウンス変数
long lastButton2PressTime = 0; // 2つ目のボタン用デバウンス変数
long lastSimultaneousPressTime = 0; // 同時押し判定のための最後のチェック時間
const long debounceDelay = 200; // デバウンス時間 (ミリ秒)

// --- グローバル変数: JSONコンテンツを保持 ---
// StaticJsonDocument<XXXX> doc; のようにサイズを明示的に指定することを強く推奨
// 今回のJSON例ではおおよそ500B程度。余裕を見て1024B (1KB) 程度あれば安全でしょう。
StaticJsonDocument<1024> doc; 
JsonArray dataArray; // JSONの最上位配列（日付ごとのデータ）

// --- 現在表示中のコンテンツのインデックスと表示モード ---
int currentDayIndex = 0; // ハイライトされている日付のインデックス
bool showContentDetails = false; // true: コンテンツ詳細表示, false: 日付一覧表示

const char* CONTENTS_JSON_FILE = "/contents.json"; // ファイル名を変更
const char* INDEX_HTML_FILE = "/index.html"; // index.htmlのパスを定義

// --- ファイルからJSONを読み込むヘルパー関数 ---
bool loadContentsJson() {
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed!");
    return false;
  }

  File file = LittleFS.open(CONTENTS_JSON_FILE, "r");
  if (!file) {
    Serial.println("Failed to open contents.json for reading!");
    LittleFS.end();
    return false;
  }

  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    file.close();
    LittleFS.end();
    return false;
  }

  dataArray = doc["recipes"];
  file.close();
  LittleFS.end(); // 読み込みが終わったらアンマウント
  Serial.println("contents.json loaded successfully.");
  return true;
}

// --- E-Paperにエラーメッセージを描画する専用関数 ---
void drawErrorMessage(const String& title, const String& message) {
  display.setRotation(0); // ディスプレイの向きを調整
  display.setTextColor(GxEPD_BLACK); // テキスト色を黒に設定

  display.setFullWindow(); // 全画面更新
  display.firstPage(); // 描画開始
  do {
    display.fillScreen(GxEPD_WHITE); // 画面を白でクリア
    display.setFont(&FreeSansBold12pt7b); // タイトル用フォント
    int16_t tbx, tby;
    uint16_t tbw, tbh;

    display.getTextBounds(title, 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor((display.width() - tbw) / 2 - tbx, display.height() / 2 - 30);
    display.print(title);

    display.setFont(&FreeMonoBold9pt7b); // メッセージ用フォント
    display.getTextBounds(message, 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor((display.width() - tbw) / 2 - tbx, display.height() / 2 + 10);
    display.print(message);

  } while (display.nextPage());
  Serial.printf("Error displayed: Title='%s', Message='%s'\n", title.c_str(), message.c_str());
}

// --- E-Paperに日付一覧を描画する関数 ---
void drawDateList(int highlightIndex) {
  display.setRotation(0); // 回転なし
  display.setTextColor(GxEPD_BLACK);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.setFont(&FreeSansBold12pt7b);
    uint16_t lineHeight = 36;
    uint16_t startY = 25; // 画面上部からの開始位置
    uint16_t currentY = startY;

    // ヘッダー
    String header = "Weekly Menu:";
    int16_t h_tbx, h_tby;
    uint16_t h_tbw, h_tbh;
    display.getTextBounds(header, 0, 0, &h_tbx, &h_tby, &h_tbw, &h_tbh);
    display.setCursor((display.width() - h_tbw) / 2 - h_tbx, currentY);
    display.print(header);
    currentY += lineHeight - 10;

    // 昼・夜のヘッダー
    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(100, currentY);
    display.print("lunch");
    display.setCursor(250, currentY);
    display.print("dinner");
    currentY += 20;

    // 最大7件まで表示
    int displayCount = min((size_t)7, dataArray.size());

    for (int i = 0; i < displayCount; ++i) {
      JsonObject dayData = dataArray[i];
      String fullDate = dayData["date"].as<String>();

      String displayDate = fullDate.substring(5); // インデックス5から末尾まで (MM-DD)
      
      JsonArray contents = dayData["contents"].as<JsonArray>();
      
      String lunchTitle = "";
      String dinnerTitle = "";
      
      // 昼食と夕食のタイトルを取得
      for (JsonObject contentItem : contents) {
        String mealType = contentItem["lunchOrDinner"].as<String>();
        String title = contentItem["title"].as<String>();
        if (mealType == "lunch") {
          lunchTitle = title;
        } else if (mealType == "dinner") {
          dinnerTitle = title;
        }
      }

      if (i == highlightIndex) {
        // ハイライト表示
        display.fillRect(0, currentY - 18, display.width(), lineHeight, GxEPD_BLACK); // 背景を黒く塗る
        display.setTextColor(GxEPD_WHITE); // テキストを白くする
      } else {
        display.setTextColor(GxEPD_BLACK); // 通常のテキスト色
      }

      // 日付を表示
      display.setCursor(10, currentY);
      display.print(displayDate); // 修正した日付を表示

      // 昼食タイトルを表示
      display.setCursor(100, currentY);
      display.print(lunchTitle);

      // 夕食タイトルを表示
      display.setCursor(250, currentY);
      display.print(dinnerTitle);

      currentY += lineHeight;
    }

  } while (display.nextPage());
  Serial.printf("Displayed date list with highlight on index: %d\n", highlightIndex);
}

// テキストを指定文字数で改行して表示する関数
void printWrappedText(const String& text, uint16_t x, uint16_t y, uint16_t maxWidth, uint16_t lineHeight) {
  uint16_t currentX = x;
  uint16_t currentY = y;
  String currentLine = "";
  
  for (int i = 0; i < text.length(); i++) {
    currentLine += text[i];
    
    // 16文字に達したら改行
    if (currentLine.length() >= 16) {
      display.setCursor(currentX, currentY);
      display.print(currentLine);
      currentY += lineHeight;
      currentLine = "";
    }
  }
  
  // 残りのテキストを表示
  if (currentLine.length() > 0) {
    display.setCursor(currentX, currentY);
    display.print(currentLine);
  }
}

// --- E-Paperに日付とコンテンツの詳細を描画する関数 ---
void drawDayContentDetails(const String& date, JsonArray contents) {
  display.setRotation(0); // 回転なし
  display.setTextColor(GxEPD_BLACK);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // 日付の表示 (大きめのフォント)
    display.setFont(&FreeSansBold12pt7b);
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    
    String displayDate = date.substring(5); // インデックス5から末尾まで (MM-DD)

    display.getTextBounds(displayDate, 0, 0, &tbx, &tby, &tbw, &tbh);
    uint16_t x_date = (display.width() - tbw) / 2 - tbx;
    uint16_t y_date = 25; // 画面上部から表示
    display.setCursor(x_date, y_date);
    display.print(displayDate); // 修正した日付を表示

    display.setFont(&FreeMonoBold9pt7b); // コンテンツ項目用フォント
    uint16_t lineHeight = 22; // コンテンツ項目の1行の高さ
    uint16_t currentY = y_date + 40; // 日付の下からコンテンツを開始

    // 左右の区切り線を描画
    uint16_t centerX = display.width() / 2;
    display.drawLine(centerX, currentY - 10, centerX, display.height() - 20, GxEPD_BLACK);

    // 左右のヘッダーを描画
    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(centerX - 120, currentY);
    display.print("Lunch");
    display.setCursor(centerX + 20, currentY);
    display.print("Dinner");
    currentY += lineHeight + 10;

    // 左右のコンテンツを準備
    String lunchTitle = "";
    String lunchBody = "";
    String dinnerTitle = "";
    String dinnerBody = "";

    // 各コンテンツ項目を分類
    for (JsonObject contentItem : contents) {
      String mealType = contentItem["lunchOrDinner"].as<String>();
      String title = contentItem["title"].as<String>();
      String body = contentItem["body"].as<String>();

      if (mealType == "lunch") {
        lunchTitle = title;
        lunchBody = body;
      } else if (mealType == "dinner") {
        dinnerTitle = title;
        dinnerBody = body;
      }
    }

    // 左側（Lunch）の表示
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(10, currentY);
    display.print(lunchTitle);
    currentY += lineHeight;
    
    // 昼食の本文を改行して表示
    printWrappedText(lunchBody, 10, currentY, centerX - 20, lineHeight);
    
    // 右側（Dinner）の表示
    currentY = y_date + 40 + lineHeight + 10; // 右側の開始位置をリセット
    display.setCursor(centerX + 20, currentY);
    display.print(dinnerTitle);
    currentY += lineHeight;

    // 夕食の本文を改行して表示
    printWrappedText(dinnerBody, centerX + 20, currentY, display.width() - centerX - 30, lineHeight);

  } while (display.nextPage());
  Serial.printf("Displayed date and details: %s\n", date.c_str());
}

// --- 現在の表示モードとインデックスに基づいて画面を更新する関数 ---
void updateDisplay() {
  if (dataArray.size() == 0) {
    drawErrorMessage("No Data!", "contents.json is empty.");
    return;
  }

  if (showContentDetails) {
    // コンテンツ詳細表示モード
    if (currentDayIndex >= 0 && currentDayIndex < dataArray.size()) {
      JsonObject dayData = dataArray[currentDayIndex];
      String date = dayData["date"].as<String>();
      JsonArray contents = dayData["contents"].as<JsonArray>();
      drawDayContentDetails(date, contents);
    } else {
      drawErrorMessage("Error!", "Selected day content not found.");
    }
  } else {
    // 日付一覧表示モード
    drawDateList(currentDayIndex);
  }
}

// --- ルートパス ('/') ハンドラ (index.htmlを返す) ---
void handleRoot() {
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed for root handler!");
    server.send(500, "text/plain", "Internal Server Error: LittleFS mount failed.");
    return;
  }

  File file = LittleFS.open(INDEX_HTML_FILE, "r");
  if (!file) {
    Serial.printf("Failed to open %s!\n", INDEX_HTML_FILE);
    server.send(404, "text/plain", "File not found: index.html. Please upload it to LittleFS.");
    LittleFS.end();
    return;
  }

  Serial.printf("Serving %s\n", INDEX_HTML_FILE);
  server.streamFile(file, "text/html");
  file.close();
  LittleFS.end();
}

// --- JSONデータ提供エンドポイントハンドラ (/api/contents) ---
void handleApiContents() {
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed for /api/contents!");
    server.send(500, "text/plain", "Internal Server Error: LittleFS mount failed.");
    return;
  }

  File file = LittleFS.open(CONTENTS_JSON_FILE, "r");
  if (!file) {
    Serial.printf("Failed to open %s for API!\n", CONTENTS_JSON_FILE);
    server.send(404, "text/plain", "File not found: contents.json");
    LittleFS.end();
    return;
  }

  String jsonString = file.readString();
  file.close();
  LittleFS.end();
  Serial.printf("Serving %s via API.\n", CONTENTS_JSON_FILE);
  server.send(200, "application/json", jsonString);
}

// --- setup() 関数: プログラムの初期設定と一度だけ実行される処理 ---
void setup() {
  Serial.begin(115200);
  Serial.println("ESP32-C3 E-Paper Date List & Details Demo");
  Serial.println("Starting E-Paper initialization...");

  // ボタンピンをプルアップ入力として設定
  pinMode(BUTTON_PIN_1, INPUT_PULLUP);
  pinMode(BUTTON_PIN_2, INPUT_PULLUP);

  // E-Paperディスプレイの初期化
  display.init(115200, true, 50, false);
  Serial.println("E-Paper initialization complete.");

  // JSONファイルを読み込む
  // ディスプレイ表示用なので、Webサーバーのハンドラとは別にここで読み込む
  if (!loadContentsJson()) {
    Serial.println("Failed to load contents.json. Please check file and LittleFS upload.");
    drawErrorMessage("Fatal Error!", "Failed to load contents.json. Check Serial for details.");
    while(true); // 致命的なエラーなので停止
  }

  // --- Wi-Fi接続 ---
  Serial.printf("Connecting to WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) { // 20回までリトライ (約10秒)
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected.");
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());

    // --- Webサーバーの設定 ---
    // ルートパス ('/') へのリクエストは index.html を返す
    server.on("/", handleRoot);
    // JSONデータ提供のためのAPIエンドポイント
    server.on("/api/contents", handleApiContents);

    server.begin(); // Webサーバーを開始
    Serial.println("HTTP server started.");
  } else {
    Serial.println("\nFailed to connect to WiFi. Web server will not be available.");
    drawErrorMessage("WiFi Error!", "Failed to connect to WiFi. Check credentials.");
  }

  // 初期画面は日付一覧表示
  currentDayIndex = 0; // 最初の項目をハイライト
  showContentDetails = false; // 日付一覧モード
  updateDisplay(); // 画面を更新
}

void loop() {
  // Webサーバーのリクエストを処理
  server.handleClient();

  int button1State = digitalRead(BUTTON_PIN_1);
  int button2State = digitalRead(BUTTON_PIN_2);
  unsigned long currentTime = millis();

  // --- 同時押し検出ロジック (常に日付一覧に戻る) ---
  if (button1State == LOW && button2State == LOW) {
    if (currentTime - lastSimultaneousPressTime > debounceDelay) {
      lastSimultaneousPressTime = currentTime; 
      if (showContentDetails || currentDayIndex != 0) { // すでに一覧画面で最初の項目がハイライトされていなければ更新
        showContentDetails = false; // 日付一覧モードに設定
        currentDayIndex = 0; // 最初の項目をハイライト
        updateDisplay();
      }
    }
  }
  // --- 単独押し検出ロジック ---
  else if (button1State == LOW) { // BUTTON 1: 詳細表示 or 次の詳細
    if (currentTime - lastButton1PressTime > debounceDelay) {
      lastButton1PressTime = currentTime;
      
      if (!showContentDetails) {
        // 日付一覧表示の場合、ハイライトされた日付の詳細コンテンツ表示に切り替える
        showContentDetails = true;
      } else {
        // コンテンツ詳細表示の場合、次の日付の詳細表示に切り替える
        currentDayIndex++; // 次の日に進む
        if (currentDayIndex >= dataArray.size()) {
          currentDayIndex = 0; // 最後のコンテンツなら最初に戻る
        }
        // showContentDetails は true のまま維持
      }
      updateDisplay(); // 画面を更新
    }
  }
  else if (button2State == LOW) { // BUTTON 2: 前の日付選択 or 日付一覧に戻る
    if (currentTime - lastButton2PressTime > debounceDelay) {
      lastButton2PressTime = currentTime;
      
      if (showContentDetails) {
        // 詳細表示の場合、日付一覧に戻る
        showContentDetails = false; 
      } else {
        // 日付一覧表示の場合、ハイライトを前の日付に移動
        currentDayIndex--; 
        if (currentDayIndex < 0) {
          currentDayIndex = dataArray.size() - 1; // 最初のコンテンツなら最後に戻る
        }
      }
      updateDisplay(); // 画面を更新
    }
  }

  // 短い遅延を入れてCPUの負荷を軽減
  delay(10);
}