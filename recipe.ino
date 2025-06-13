#include <LittleFS.h> // LittleFSライブラリ
#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

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

// --- 現在表示中の画面の状態を管理する変数 ---
String currentDisplayFile = ""; // 現在表示しているファイル名 (例: "/initial.txt")

// --- ファイルからテキストを読み込むヘルパー関数 ---
String readFileContent(const char* filename) {
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed!");
    return "FS Error!"; // エラーメッセージを返す
  }

  File file = LittleFS.open(filename, "r");
  if (!file) {
    Serial.printf("Failed to open file: %s\n", filename);
    LittleFS.end();
    return "File Not Found!"; // エラーメッセージを返す
  }

  String content = "";
  while (file.available()) {
    content += (char)file.read();
  }
  file.close();
  LittleFS.end(); // 読み込みが終わったらアンマウント

  return content;
}

// --- E-Paperにテキストを描画する関数 ---
void drawTextOnEpaper(const String& textToDisplay) {
  display.setRotation(0); // ディスプレイの向きを調整
  display.setTextColor(GxEPD_BLACK); // テキスト色を黒に設定

  display.setFullWindow(); // 全画面更新
  display.firstPage(); // 描画開始
  do {
    display.fillScreen(GxEPD_WHITE); // 画面を白でクリア

    // テキストを改行で分割し、中央に表示
    int lineCount = 0;
    int prevIndex = 0;
    int currentIndex = 0;
    String lines[5]; // 最大5行まで対応（必要に応じて調整）

    // 改行コード '\n' でテキストを分割
    while (currentIndex != -1 && lineCount < 5) {
      currentIndex = textToDisplay.indexOf('\n', prevIndex);
      if (currentIndex == -1) {
        lines[lineCount++] = textToDisplay.substring(prevIndex);
      } else {
        lines[lineCount++] = textToDisplay.substring(prevIndex, currentIndex);
        prevIndex = currentIndex + 1;
      }
    }

    // 各行を画面に描画
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    int totalTextHeight = 0;
    
    // フォント設定の準備 (FreeSansBold12pt7bをメイン、FreeMonoBold9pt7bをサブとして使う)
    display.setFont(&FreeSansBold12pt7b); // 1行目用のフォント
    display.getTextBounds(" ", 0, 0, &tbx, &tby, &tbw, &tbh); // 1行分の高さの目安
    totalTextHeight += tbh * (lineCount > 0 ? 1 : 0); // 1行目
    
    if (lineCount > 1) {
      display.setFont(&FreeMonoBold9pt7b); // 2行目以降のフォント
      display.getTextBounds(" ", 0, 0, &tbx, &tby, &tbw, &tbh); // 1行分の高さの目安
      totalTextHeight += tbh * (lineCount - 1); // 2行目以降
    }

    // 全体の高さを考慮して開始Y座標を計算
    uint16_t startY = (display.height() - totalTextHeight) / 2;
    if (startY < 0) startY = 0; // 画面内に収まらない場合の上限

    uint16_t currentY = startY;

    for (int i = 0; i < lineCount; ++i) {
      if (i == 0) {
        display.setFont(&FreeSansBold12pt7b); // 1行目のフォント
      } else {
        display.setFont(&FreeMonoBold9pt7b); // 2行目以降のフォント
      }

      display.getTextBounds(lines[i], 0, 0, &tbx, &tby, &tbw, &tbh);
      uint16_t x = (display.width() - tbw) / 2 - tbx;
      
      display.setCursor(x, currentY - tby); // テキストの上端にカーソルを合わせる

      display.print(lines[i]);
      currentY += tbh + 2; // 行の高さ＋行間
    }

  } while (display.nextPage());
  Serial.printf("Displayed content:\n%s\n", textToDisplay.c_str());
}

// --- 画面切り替え関数 ---
void changeScreen(const char* filename) {
  String content = readFileContent(filename);
  if (content != currentDisplayFile) { // 同じ内容を二度表示しない
    drawTextOnEpaper(content);
    currentDisplayFile = content; // 表示したファイルの内容を記録
  }
}

// --- setup() 関数: プログラムの初期設定と一度だけ実行される処理 ---
void setup() {
  Serial.begin(115200);
  Serial.println("ESP32-C3 E-Paper LittleFS Button Control Demo");
  Serial.println("Starting E-Paper initialization...");

  // ボタンピンをプルアップ入力として設定
  pinMode(BUTTON_PIN_1, INPUT_PULLUP);
  pinMode(BUTTON_PIN_2, INPUT_PULLUP);

  // E-Paperディスプレイの初期化
  display.init(115200, true, 50, false);
  Serial.println("E-Paper initialization complete.");

  // 初期画面を表示
  changeScreen("/initial.txt");
}

void loop() {
  int button1State = digitalRead(BUTTON_PIN_1);
  int button2State = digitalRead(BUTTON_PIN_2);
  unsigned long currentTime = millis();

  // --- 同時押し検出ロジック ---
  if (button1State == LOW && button2State == LOW) {
    if (currentTime - lastSimultaneousPressTime > debounceDelay) {
      lastSimultaneousPressTime = currentTime; 
      changeScreen("/initial.txt"); // 初期画面に戻る
    }
  } 
  // --- 単独押し検出ロジック ---
  else if (button1State == LOW) {
    if (currentTime - lastButton1PressTime > debounceDelay) {
      lastButton1PressTime = currentTime;
      changeScreen("/weekly.txt");
    }
  } 
  else if (button2State == LOW) {
    if (currentTime - lastButton2PressTime > debounceDelay) {
      lastButton2PressTime = currentTime;
      changeScreen("/daily.txt");
    }
  }

  // 短い遅延を入れてCPUの負荷を軽減
  delay(10); 
}