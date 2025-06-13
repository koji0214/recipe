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

// --- 初期画面表示用の関数 ---
void displayInitialScreen() {
  display.setRotation(1); // ディスプレイの向きを調整
  display.setTextColor(GxEPD_BLACK); // テキスト色を黒に設定

  display.setFullWindow(); // 全画面更新
  display.firstPage(); // 描画開始
  do {
    display.fillScreen(GxEPD_WHITE); // 画面を白でクリア

    // 1行目のメッセージ
    display.setFont(&FreeSansBold12pt7b); // 大きめのフォントを使用
    const char* message1 = "Welcome to E-Paper!";
    int16_t tbx1, tby1;
    uint16_t tbw1, tbh1;
    display.getTextBounds(message1, 0, 0, &tbx1, &tby1, &tbw1, &tbh1);
    uint16_t x1 = (display.width() - tbw1) / 2 - tbx1;
    uint16_t y1 = display.height() / 2 - tbh1 - 10; // 中央より少し上に表示
    display.setCursor(x1, y1);
    display.print(message1);

    // 2行目のメッセージ
    display.setFont(&FreeMonoBold9pt7b); // 小さめのフォントを使用
    const char* message2 = "Initial Screen Displayed.";
    int16_t tbx2, tby2;
    uint16_t tbw2, tbh2;
    display.getTextBounds(message2, 0, 0, &tbx2, &tby2, &tbw2, &tbh2);
    uint16_t x2 = (display.width() - tbw2) / 2 - tbx2;
    uint16_t y2 = display.height() / 2 + tbh2 + 10; // 中央より少し下に表示
    display.setCursor(x2, y2);
    display.print(message2);

  } while (display.nextPage()); // 次のページがある場合は描画を続ける
  Serial.println("Initial screen displayed.");
}

// --- setup() 関数: プログラムの初期設定と一度だけ実行される処理 ---
void setup() {
  Serial.begin(115200);
  Serial.println("ESP32-C3 E-Paper Initial Screen Demo");
  Serial.println("Starting E-Paper initialization...");

  // E-Paperディスプレイの初期化
  display.init(115200, true, 50, false);
  Serial.println("E-Paper initialization complete.");

  // 初期画面表示
  displayInitialScreen();
}

// --- loop() 関数: 何も実行しない ---
void loop() {
  // put your main code here, to run repeatedly:

}