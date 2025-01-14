/* Avoidance.ino 2024/12/12 ArduinoIDE 2.3.3
============================================
Arduino+LCD+アナログスティックで弾避けゲーム
--------------- A V O I D ------------------
                                     Name
============================================

■接続機器
  出力機器:LCDディスプレイ 128x64ドット (制御IC: SSD1306)
    SDAピンをArduinoのSDA端子へ
    SCLピンをArduinoのSCL端子へ
    VCC端子にArduinoの5V端子から電源を
    GNDはGNDへ
  入力機器:2軸アナログスティック
    X軸方向の2番ピンをArduinoのA0端子へ
    Y軸方向の2番ピンをArduinoのA1端子へ
    1番ピンにArduinoの5V端子から電源を
    GNDはGNDへ

■あそびかた
  ディスプレイとスティックを接続したら、Arduinoに電源を繋いで起動
  タイトル画面、リザルト画面は、スティックを右に入力すると次にすすみます
  ゲーム中はひたすらスティックで自機を操作して弾を避け続けます
  体力が0になるとゲームオーバーですs
  ハイスコアを目指せ！

*/


#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <stdio.h>
#include <EEPROM.h>

// Adafruit_SSD1306 displayクラスのインスタンス化
#define SCREEN_WIDTH 128    // OLED display width, in pixels
#define SCREEN_HEIGHT 64    // OLED display height, in pixels
#define OLED_RESET -1       // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C //< See datasheet for Address; 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define pinX A0                          // スティックのX軸信号の入力ピンを指定
#define pinY A1                          // スティックのY軸信号の入力ピンを指定
int stickX, stickY;                      // スティックの信号の生の値を格納する変数
int8_t xStickState = 0, yStickState = 0; //-2のとき負方向大、-1のとき負方向小、0のとき中心、1のとき正方向小、2のとき正方向大

#define DefaultCursorY 9 // 画面上のゲーム領域の上端のY座標()
//

//
//========設定=====================================
#define MaxBullets 24  // 弾の最大生成数
#define MaxHealth 3    // 最大体力
#define scoreketasuu 5 // スコア表示の桁数(オーバーすると後ろに被る)
//=================================================
//

//
// 周期処理のタイマー初期設定 (参考: https://qiita.com/Ninagawa123/items/f8585c5c711bcf065656)
// https://qiita.com/Ninagawa123/items/f8585c5c711bcf065656
const uint8_t frame_ms = 50; // 1フレームあたりの単位時間(ms)
unsigned long sframe;        // フレーム管理時計の時刻 schaduledなflame数

unsigned long frameCounter; // アニメーション用のフレーム数カウンター
//

//
//=====スコア関連===================================
uint32_t score = 0;
uint32_t bestScore = 0;

// スコアを加算する関数
void addScore(int8_t value)
{
  if (score < 4294967295 - value) // オーバーフロー対策
    score += value;
}
//=================================================
//

//
//===ゲームの状態状態関連==========================
#define inTitle 0
#define inGameStart 1
#define inGameNow 2
#define inGameEnd 3
#define inResult 4

uint8_t gameState = inTitle; // 起動時のGameStateを指定
//=================================================
//

//
// 弾関連の変数・クラスなど=========================
uint8_t bulletNumber = 0; // 弾番号のメモリを確保

// 弾のクラス
class BulletClass
{
private:
  // 座標の変数宣言
  int8_t posX;
  float posY; // Y方向は遅く動かすのでfloat型に
  // 弾の移動速度の変数宣言
  int8_t speedX;
  int8_t speedY;

  // 弾を動かす関数
  void move()
  {
    if (isActive) // 弾が有効なとき
    {
      posX = posX - speedX;                    // x座標の移動
      posY = posY + ((float)(speedY) / 100.0); // y座標の移動(floatで計算)
      if (posX < 0 || posY < 0 || posY > 54)
      {
        isActive = false; // 画面の範囲外に出たら弾を無効に

        if (gameState == inGameNow)
        { // ゲーム中にのみスコア加算
          addScore(10);
        }
      }
    }
  }

  // 弾の描画処理
  void write()
  {
    display.drawPixel(posX, posY + DefaultCursorY, WHITE); // ディスプレイバッファに書き込み
  }

public:
  bool isActive = false; // 表示されている弾かどうか

  // 毎フレーム行う処理
  void update()
  {
    if (isActive == true)
    {
      // 弾が有効のときのみ実行
      move();
      write();
    }
  }

  // 弾の有効化時の処理
  void activateBullet()
  {
    // 生成時の座標を指定
    posX = 127; // 画面右端に生成
    posY = (float)random(5, 50);
    // 弾の速度を指定
    speedX = random(2, 4);
    speedY = (int8_t)random(-50, 50);
    isActive = true; // 弾を有効に
  }

  //(当たり判定用)
  int8_t returnPosX()
  {
    return posX; // 弾のx座標を返す
  }
  int8_t returnPosY()
  {
    // 弾のy座標を整数型にキャストして返す
    return (int8_t)posY;
  }
};

// 弾のインスタンス化(メモリ確保のため)
BulletClass bullet[MaxBullets];
// 最大弾数分のメモリをコンピュータ起動時に確保する
//=====================================================
//

//
// プレイヤー関連======================================
class PlayerClass
{
private:
  // 座標
  int8_t posX;
  int8_t posY;

  uint8_t onDamagedCounter = 0; // ダメージを受けた後の無敵時間+アニメーション用のカウンター

  bool isVisible = false; // プレイヤースプライトを表示するかどうか

public:
  int8_t health; // プレイヤー体力

  // ゲーム開始時の処理
  void start()
  {
    // プレイヤー座標と体力初期化
    posX = 8;
    posY = 27;
    health = MaxHealth;

    isVisible = true; // プレイヤーを表示
  }

  // プレイヤーの移動処理
  void move()
  {
    // x座標
    posX = posX + xStickState; // StickStateの値分(2か1)移動
    // 画面外に出ようとしたとき、座標を戻す
    if (posX < 2 || posX > 124)
    {
      posX = posX - xStickState;
    }

    // y座標
    posY = posY - yStickState; // ディスプレイはy軸正方向が下向きなため、マイナスの入力を正に変換して計算
    // 画面外に出ようとしたとき座標を戻す
    if (posY < 1 || posY > 53)
    {
      posY = posY + yStickState;
    }
  }

  // 当たり判定
  void hit()
  {
    // すべての弾に対して検索
    for (uint8_t cnt = 0; cnt < MaxBullets; cnt++)
    {
      // 自機スプライトの範囲(posX±2,posY±1)に弾がいるとき
      if (posX - 2 <= bullet[cnt].returnPosX() && bullet[cnt].returnPosX() <= posX + 2 && posY - 1 <= bullet[cnt].returnPosY() && bullet[cnt].returnPosY() <= posY + 1)
      {
        bullet[cnt].isActive = false; // 当たり判定に引っかかった弾を消す

        // ダメージを受けてから一定フレーム経っているとき
        if (onDamagedCounter == 0)
        {
          health--;              // 体力を1減らす
          onDamagedCounter = 40; // 指定フレーム数が経過するまでダメージを受けない

          // 体力が0になったとき
          if (health == 0)
          {
            onDamagedCounter = 60; // 終了時アニメーション用にカウンターを60に
            gameState = inGameEnd; // ゲームの終了処理へ
          }
        }
      }
    }
  }

  // ダメージ時のアニメーション処理
  void damageAnimator()
  {
    if (onDamagedCounter > 18)
    {
      if (((onDamagedCounter + 2) % 6) == 0)
      {
        isVisible = false; // カウンタが42,36,30,24のとき非表示
      }
      else if (((onDamagedCounter + 2) % 3) == 0)
      {
        isVisible = true; // カウンタが39,33,27,21のとき表示
      }
    }
  }

  // 描画処理
  void write()
  {
    // 表示が有効のとき
    if (isVisible == true)
    {
      // 自機のグラフィックを左上から順にドット打ち
      display.drawPixel(posX - 2, posY + 1 + DefaultCursorY, WHITE);
      display.drawPixel(posX - 2, posY + DefaultCursorY, WHITE);
      display.drawPixel(posX - 2, posY - 1 + DefaultCursorY, WHITE);
      display.drawPixel(posX - 1, posY + 1 + DefaultCursorY, WHITE);
      display.drawPixel(posX - 1, posY - 1 + DefaultCursorY, WHITE);
      display.drawPixel(posX, posY + 1 + DefaultCursorY, WHITE);
      display.drawPixel(posX, posY - 1 + DefaultCursorY, WHITE);
      display.drawPixel(posX + 1, posY + 1 + DefaultCursorY, WHITE);
      display.drawPixel(posX + 1, posY - 1 + DefaultCursorY, WHITE);
      display.drawPixel(posX + 2, posY + DefaultCursorY, WHITE);
    }
  }

  // 毎フレーム行う処理
  void update()
  {
    move();
    hit();
    damageAnimator();

    // ダメージを受けてからカウンタが0になるまで
    if (onDamagedCounter > 0)
    {
      onDamagedCounter--; // カウンタをデクリメント
    }

    write();
  }

  // ゲームオーバー時のアニメーションを1ピクセル描画
  void particle(int x, int y)
  {
    if ((x < 0 || x > 125 || y < 0 || y > 54) == false)
    {
      display.drawPixel(x, y + DefaultCursorY, WHITE); // バッファに書き込み
    }
  }

  // ゲームオーバー時のアニメーション
  //  gameStateがisGameEndのときに呼び出される
  void endAnimator()
  {
    // パーティクルの拡散の描画と点滅
    if (onDamagedCounter % 3 || onDamagedCounter % 4)
    {
      particle((int)posX + 2 * (60 - onDamagedCounter), (int)posY);
      particle((int)posX - 2 * (60 - onDamagedCounter), (int)posY);
      particle((int)posX, (int)posY + 2 * (60 - onDamagedCounter));
      particle((int)posX, (int)posY - 2 * (60 - onDamagedCounter));
      particle((int)((float)posX + 1.41 * (60 - onDamagedCounter)), (int)((float)posY + 1.41 * (60 - onDamagedCounter)));
      particle((int)((float)posX + 1.41 * (60 - onDamagedCounter)), (int)((float)posY - 1.41 * (60 - onDamagedCounter)));
      particle((int)((float)posX - 1.41 * (60 - onDamagedCounter)), (int)((float)posY + 1.41 * (60 - onDamagedCounter)));
      particle((int)((float)posX - 1.41 * (60 - onDamagedCounter)), (int)((float)posY - 1.41 * (60 - onDamagedCounter)));
    }

    // 画面全体を点滅させる
    if (onDamagedCounter == 57 || onDamagedCounter == 53)
    {
      display.invertDisplay(false);
    }
    else if (onDamagedCounter == 59 || onDamagedCounter == 55)
    {
      display.invertDisplay(true);
    }

    // アニメーションが始まってからの経過時間のカウンタをデクリメント
    onDamagedCounter--;

    // カウンタがになったときに1度だけ走らせる処理
    if (onDamagedCounter == 0)
    {
      gameState = inResult; // リザルト画面に移行
      frameCounter = 20;    // リザルト画面で1秒待つためのカウンタ

      // スコアが現在のベストコアより高ければベストスコアを更新
      if (score > bestScore)
      {
        bestScore = score;           // ベストスコア更新
        EEPROM.put(0x34, bestScore); // EEPROMにベストスコアを書き込み
      }
    }
  }
};

// プレイヤーをインスタンス化(メモリの確保が目的)
PlayerClass player;
//===========================================================
//

//
// タイトル/リザルト画面の背景ビットマップ
//  'background', 128x64px
const unsigned char titlebgbackground[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0x83, 0xff, 0xff, 0xff, 0xff,
    0x07, 0xf8, 0x30, 0x0c, 0x1f, 0xe0, 0xc1, 0xfe, 0x00, 0x0f, 0xf0, 0x00, 0x1f, 0xff, 0xff, 0xff,
    0x0f, 0xfc, 0x38, 0x1c, 0x3f, 0xf0, 0xe1, 0xff, 0x80, 0x1f, 0x80, 0x00, 0x07, 0xff, 0xff, 0xff,
    0x1b, 0xfe, 0x6c, 0x3e, 0x6f, 0xf9, 0x73, 0x7f, 0xc0, 0x3f, 0x00, 0x38, 0x03, 0xff, 0xff, 0xff,
    0x17, 0xfe, 0x5c, 0x3e, 0x5f, 0xf9, 0x72, 0xff, 0xc0, 0x7f, 0x07, 0xff, 0xc3, 0xff, 0xff, 0xff,
    0x17, 0xfe, 0x5c, 0x3e, 0x5c, 0xf9, 0x72, 0xcf, 0xc0, 0xff, 0x1f, 0xff, 0xf3, 0xff, 0xff, 0xff,
    0x1f, 0x3e, 0x7c, 0x3e, 0x78, 0x79, 0xf3, 0xc3, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x1e, 0x1e, 0x7c, 0x3e, 0x78, 0x79, 0xf3, 0xc3, 0xc1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x1e, 0x1e, 0x7c, 0x3e, 0x78, 0x79, 0xf3, 0xc3, 0xc3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x1f, 0x3e, 0x7c, 0x3e, 0x78, 0x79, 0xf3, 0xc3, 0xc3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x1f, 0xfe, 0x7e, 0x7e, 0x78, 0x79, 0xf3, 0xc3, 0xc7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x1f, 0xfe, 0x7f, 0xfe, 0x78, 0x79, 0xf3, 0xc3, 0xcf, 0xff, 0xff, 0x87, 0xff, 0xff, 0x80, 0x0f,
    0x1f, 0xfe, 0x7f, 0xfe, 0x78, 0x79, 0xf3, 0xc3, 0xcf, 0xff, 0xff, 0x03, 0xff, 0xfc, 0x00, 0x01,
    0x1f, 0xfe, 0x3f, 0xfc, 0x78, 0x79, 0xf3, 0xc3, 0xdf, 0xff, 0xfe, 0x01, 0xff, 0xf8, 0x00, 0x00,
    0x1f, 0xfe, 0x1f, 0xd8, 0x7c, 0xf9, 0xf3, 0xcf, 0xdf, 0xff, 0xfc, 0x01, 0xff, 0xf8, 0x18, 0x30,
    0x1f, 0x3a, 0x0f, 0xb0, 0x7f, 0xe9, 0xd3, 0xff, 0x7f, 0xff, 0xfc, 0x00, 0xff, 0xf8, 0xf0, 0x1c,
    0x1e, 0x16, 0x07, 0xe0, 0x7f, 0xd9, 0xb3, 0xfe, 0xff, 0xff, 0xfc, 0x00, 0xff, 0xff, 0xe0, 0x1e,
    0x0e, 0x1c, 0x03, 0xc0, 0x3f, 0xf0, 0xe1, 0xff, 0xff, 0xff, 0xfc, 0x00, 0xff, 0xff, 0xe0, 0x1f,
    0x06, 0x18, 0x01, 0x80, 0x1f, 0xe0, 0xc1, 0xfe, 0x7f, 0xff, 0xfc, 0x00, 0xff, 0xff, 0xc0, 0x0f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xfc, 0x00, 0xff, 0xff, 0xc0, 0x0f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xfe, 0x01, 0xff, 0xff, 0xc0, 0x0f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x03, 0xff, 0xff, 0xc0, 0x0f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x87, 0xff, 0xff, 0xe0, 0x1f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe0, 0x1f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x3f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xc7, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x7e, 0xfe, 0x00, 0x3f, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x7f, 0x7c, 0x00, 0x07, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x7f, 0xfe, 0x00, 0x01, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xbf, 0xff, 0x00, 0x7f, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x3f, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xbf, 0xdf, 0xff, 0xfc, 0x1f, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xbf, 0xdf, 0xff, 0xff, 0x0f, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xbf, 0xef, 0xff, 0xff, 0x9f, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0xbf, 0xef, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0xff, 0xef, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xbf, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xff, 0xdf, 0xff, 0xff, 0xf8, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0x7f, 0xdf, 0xff, 0xff, 0x8f, 0x7f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xdf, 0xff, 0xf0, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfe, 0xff, 0xbf, 0xff, 0x1f, 0xff, 0xbf,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfd, 0xff, 0x7f, 0xf1, 0xff, 0xff, 0xbf,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xfb, 0xff, 0x7f, 0x1f, 0xff, 0xff, 0xbf,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xf7, 0xfe, 0xf1, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xef, 0xff, 0x3f, 0xff, 0xff, 0xff, 0x7f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xdf, 0xff, 0xff, 0xff, 0xff, 0xf1, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0xff, 0xff, 0xff, 0x1f, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0xff, 0xff, 0xf1, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xff, 0xff, 0x1f, 0xff, 0xff};

// Array of all bitmaps for convenience. (Total bytes used to store images in PROGMEM = 1040)
const int titlebgallArray_LEN = 1;
const unsigned char *titlebgallArray[1] = {
    titlebgbackground};
//

//
// setup関数==============================================
void setup()
{
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  display.display();           // バッファの初期値（adafruitのロゴ）を表示
  EEPROM.get(0x34, bestScore); // EEPROMからベストスコアを読み出してメモリに格納
  delay(2000);

  // 乱数のシード設定
  randomSeed(500);
  sframe = millis(); // 現在時刻を取得
  frameCounter = 20; // タイトル画面で1秒待つためのカウンタ
}
//======================================================
//

//
// loop関数=============================================
void loop()
{

  display.clearDisplay(); // ディスプレイのバッファを初期化
  getStickStateXY();      // スティックの状態を取得

  // gameStateの値によって処理を分岐
  switch (gameState)
  {
  case inGameStart:     // ゲーム開始時
    gameStartProcess(); // ゲームの開始処理
    break;
  case inGameNow:       // ゲーム中
    inGameProcess();    // ゲームの処理
    inGameScoreBoard(); // スコアボードの表示
    break;
  case inGameEnd:       // ゲーム終了からリザルト画面まで
    endGameProcess();   // ゲームの終了処理
    inGameScoreBoard(); // スコアボードの表示
    break;
  case inResult: // リザルト画面
    resultScreen();
    break;

  default: // タイトル画面
    titleScreen();
    break;
  }

  FrameTimer();      // 1フレーム分の時間が経過していなければこの関数内で処理が止まる
  display.display(); // バッファを画面に描画
}
//================================================================
//

//
// ゲーム中の処理
void inGameProcess()
{
  // 表示されていない弾があったら初期化ののち表示する
  generateBullets();

  // 弾の毎フレーム行う処理をすべての弾で実行
  for (uint8_t cnt = 0; cnt < MaxBullets; cnt++)
  {
    bullet[cnt].update();
  }
  // プレイヤーの毎フレーム行う処理を実行
  player.update();
}

// ゲーム開始時の処理 (開始時に1回だけ走る)
void gameStartProcess()
{
  score = 0;             // スコア初期化
  player.start();        // プレイヤーの開始処理を実行
  gameState = inGameNow; // ゲームの状態をゲーム中に変更
}
//

//
// ゲーム終了時の処理
void endGameProcess()
{
  // 表示中の弾はゲーム中と同様に動かし続ける
  for (uint8_t cnt = 0; cnt < MaxBullets; cnt++)
  {
    bullet[cnt].update();
  }

  player.endAnimator(); // プレイヤーの終了アニメーションを実行
}
//

//
// スコアボード表示の処理
void inGameScoreBoard()
{
  display.setTextSize(1);                                            // テキストの大きさ
  display.setTextColor(SSD1306_WHITE);                               // テキストの色
  display.setCursor(8, 0);                                           // テキストの位置
  display.println("SCORE:");                                         // テキストをバッファに書き込み
  display.setCursor(42 + (6 * scoreketasuu) - (6 * keta(score)), 0); // 5桁ぶんの領域を確保し、スコアを右揃えに
  display.print(score);                                              // スコア表示をバッファに書き込み
  display.setCursor(42 + (6 * scoreketasuu) + 8, 0);                 // カーソル移動
  display.print("HEALTH:");                                          // テキストをバッファに書き込み
  display.print((int)player.health);                                 // 体力表示
  display.drawLine(0, 8, 128, 8, WHITE);                             // スコアボードとゲーム領域の境界線をバッファに書き込み
}
//

//
// タイトル画面の処理
void titleScreen()
{
  // 背景の表示
  display.drawBitmap(0, 0, titlebgbackground, (uint8_t)SCREEN_WIDTH, (int16_t)SCREEN_HEIGHT, WHITE);

  // ベストスコアの表示
  display.setCursor(4, 28);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.print("BESTSCORE");
  display.setCursor(15, 40);
  display.print(bestScore);

  if (frameCounter > 0)
  {
    frameCounter--; // タイトル画面の表示からプレイボタンの表示まで1秒待つためのカウンタ
  }
  else if (xStickState > 1)
  {
    // タイトルボタンが選択されたときの処理
    display.setCursor(10, 52);
    display.print("-> PLAY"); // バッファに書き込み
    display.fillRect(8, 50, 46, 12, INVERSE);
    display.display();
    delay(1000); // 1秒待つ
    gameState = inGameStart;
  }
  else
  {
    // プレイボタンの描画(frameCounterが0になってから)
    display.setCursor(10, 52);
    display.print("-> PLAY"); // バッファに書き込み
  }
}
//

//
// リザルト画面の処理
void resultScreen()
{
  // 背景の表示
  display.drawBitmap(0, 0, titlebgbackground, (uint8_t)SCREEN_WIDTH, (int16_t)SCREEN_HEIGHT, WHITE);

  // GAME OVER! のテキストとスコアを表示
  display.setTextSize(1);
  display.setCursor(4, 28);
  display.print("GAME OVER!");
  display.setCursor(4, 40);
  display.print(score);

  if (frameCounter > 0)
  {
    frameCounter--; // リザルト画面が表示されてから操作可能になるまでのカウンタ
  }
  else if (xStickState > 1)
  {
    // タイトル画面へ遷移するボタンが押されたときの処理
    display.setCursor(10, 52);
    display.print("-> TITLE");
    display.fillRect(8, 50, 52, 12, INVERSE);
    display.display();
    delay(1000);
    gameState = inTitle;
    frameCounter = 20;
  }
  else
  {
    // プレイボタンの表示(1秒経ってから)
    display.setCursor(10, 52);
    display.print("-> TITLE");
  }
}
//

//
// void loop()の処理が終わったとき、1フレーム分の時間が経過するまでこの関数内で処理が止まる
void FrameTimer()
{                                // 周期処理用に処理を遅延させるメソッド
  unsigned long curr = millis(); // 現在時刻を取得
  if (curr - sframe > frame_ms)
  {
    sframe = millis(); // 現在時刻を取得
  }
  else
  {
    // 余剰時間を消化する処理。時間がオーバーしていたらこの処理は飛ばされる。
    while (curr - sframe < frame_ms)
    {
      curr = millis();
    }
    sframe = curr;
  }
}
//

//
// スティックの状態を取得して変数に代入する処理
void getStickStateXY()
{
  // アナログスティックの電圧の読み取り値を取得して、調整して代入
  stickX = -1 * analogRead(pinX) + 512; // 右方向に-512～511の範囲で動くように調整
  stickY = analogRead(pinY) - 512;      // 右方向に-512～511の範囲で動くように調整
  // スティックの状態を(-2,-1,0,1,2)の5種類に分けて格納
  xStickState = getStickState(stickX);
  yStickState = getStickState(stickY);
}
//

//
// // スティックの状態を(-2,-1,0,1,2)の5種類に分けて格納する関数
int8_t getStickState(int value)
{
  int8_t tmpState = 0; // 中心付近のとき0
  if (abs(value) > 430)
  {
    tmpState = 2; // 絶対値が大きいとき2
  }
  else if (abs(value) > 120)
  {
    tmpState = 1; // 絶対値が小さいと1
  }
  if (value < 0)
  { // 値が負のときマイナスに
    tmpState = tmpState * -1;
  }
  return tmpState; // 戻り値
}
//

//
// 弾生成
void generateBullets()
{
  for (bulletNumber = 0; bulletNumber < MaxBullets; bulletNumber++)
  { // 有効でないbulletを探す
    if (bullet[bulletNumber].isActive == false)
    {                                        // 有効でないbulletを見つけたとき
      bullet[bulletNumber].activateBullet(); // 弾を有効にする
      break;
    }
  }
}
//

//
// スコアの桁数を返す関数
int8_t keta(uint16_t tmpScore)
{
  int8_t cnt = 0; // 桁数カウンタ
  /*if(tmpScore < 0){ //スコアが0以下のときの処理(スコアは非負整数のため使わない)
    cnt++;
    tmpScore = -1 * tmpScore;
  }*/

  // 数える
  do
  {
    cnt++;                    // 桁数を加算
    tmpScore = tmpScore / 10; // tmpScoreが10以上のとき1桁下がる tmpScoreが0～9のときtmpScoreが0になる
    if (tmpScore == 0)
    {
      return cnt; // 桁数を返す
    }
  } while (tmpScore > 0); // tmpScoreが0になるまでループ
}
