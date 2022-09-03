//
//  M5Stack training supporter
//  2022
//
//  HW configurations:
//    - M5Stack
//    - Pressure sensor FSR406
//      - 10kohms pull up (3.3V)
//      - Directly connected to ESP32 ADC input
//
//  TODOs
//    - Add settings menu (store to EEPROM? microSD?)
//    - Tweet workout result
//    - Wireless (BT) connection with the knee sensor
//

#include "M5Stack.h"
#include "M5GFX.h"
#include <Ticker.h>
// #include "../../include/wifiinfo.h" // 自分環境の定義ファイル。無ければこの行はコメントに。
#include "messages.h"

M5GFX disp;
Ticker tickButtonBlink;
Ticker tickBeep;

//---- HW dependent
const uint16_t adcPin[2] = {35, 36};      // 0(pin35):Right, 1(pin36):Left
//---- Display positions
const uint16_t posBtn1X = 65;
const uint16_t posBtn2X = 160;
const uint16_t posBtn3X = 255;
const uint16_t posBtnY = 216;
const uint16_t posDispSettingY = 180;
const uint16_t modeStartScreen = 1;
const uint16_t modeRunningScreen = 2;
const uint16_t modeSettingScreen = 3;
//---- Color definitions
const int colorStart = TFT_YELLOW;
const int colorSetRep = disp.color565(255, 140, 0);
const int colorRest = TFT_GREEN;
const int colorComp = colorSetRep;
const int colorBack = TFT_BLACK;
//---- Other constants
const uint32_t swReadInterval = 50;       // Detection switch (Sensor) read interval: 50ms
const uint32_t buttonBlinkInterval = 750; // Button blink intercal: 750ms

//---- Default of user changeable values
uint16_t setMax = 3;                // 3 sets
uint16_t repMax = 20;               // 20 reps
uint16_t restTime = 30;             // 30 seconds
uint16_t beepVolume = 4;
//---- 
boolean isButtonPositive = true;   // Start button color mode, positive or negative
int currentMode = modeStartScreen; // 1: Start Screen, 2: Running Screen, 3: Setting Screen
char btnText[3][10];

// Check if the sensor is pressed. (Make detection in case of using switch)
int isSwitchPressed(int swNum)
{
    int adcVal;
    int adcThreshold = 2000; // Pressure sensor threshold (0 ~ 4097)
    adcVal = analogRead(adcPin[swNum]);

    if (adcVal < adcThreshold)
        return 1;
    else
        return 0;
}

void checkSwichStatus(int &statR, int &statL)
{
    int swStatOrg[2];
    int swStat[2] = {0, 0};
    int chkTimes = 3;

    swStatOrg[0] = statR;
    swStatOrg[1] = statL;
    for (int i = 0; i < chkTimes; i++)
    {
        for (int rl = 0; rl < 2; rl++)
        {
            swStat[rl] += isSwitchPressed(rl);
            delay(1); // wait for next ADC read
        }
        delay(10);    // wait to remove chattering
    }
    if (swStat[0] == chkTimes)
        statR = 1;
    else if (swStat[0] == 0)
        statR = 0;
    else
        statR = swStatOrg[0];

    if (swStat[1] == chkTimes)
        statL = 1;
    else if (swStat[1] == 0)
        statL = 0;
    else
        statL = swStatOrg[1];
}

void muteBeep()
{
    M5.Speaker.mute();
}

void drawButtons(const char *text1, uint16_t fcolor1, uint16_t bcolor1,
                 const char *text2, uint16_t fcolor2, uint16_t bcolor2,
                 const char *text3, uint16_t fcolor3, uint16_t bcolor3)
{
    disp.fillRect(0, posBtnY - 2, 320, 240, colorBack);
    //    disp.loadFont(euro_b_20, SD);     // 動作が不安定だったので外部フォントの利用は一旦保留
    disp.setTextFont(4);
    disp.setTextSize(1);
    disp.fillRect(posBtn1X - 42, posBtnY - 3, 84, 240, bcolor1);
    disp.setTextColor(fcolor1);
    disp.drawCenterString(text1, posBtn1X, posBtnY);
    disp.fillRect(posBtn2X - 42, posBtnY - 3, 84, 240, bcolor2);
    disp.setTextColor(fcolor2);
    disp.drawCenterString(text2, posBtn2X, posBtnY);
    disp.fillRect(posBtn3X - 42, posBtnY - 3, 84, 240, bcolor3);
    disp.setTextColor(fcolor3);
    disp.drawCenterString(text3, posBtn3X, posBtnY);
    //    disp.unloadFont();
}

void startButtonBlinker()
{
    if (isButtonPositive)
    {
        drawButtons(msgBtnSetting, TFT_WHITE, colorBack,
                    msgBtnBlank, TFT_WHITE, colorBack,
                    msgBtnStart, colorBack, colorStart);
    }
    else
    {
        drawButtons(msgBtnSetting, TFT_WHITE, colorBack,
                    msgBtnBlank, TFT_WHITE, colorBack,
                    msgBtnStart, colorStart, colorBack);
    }
    isButtonPositive = !isButtonPositive;
}

void okButtonBlinker()
{
    if (isButtonPositive)
    {
        drawButtons(msgBtnBlank, TFT_WHITE, colorBack,
                    msgBtnBlank, TFT_WHITE, colorBack,
                    msgBtnOk, colorBack, colorComp);
    }
    else
    {
        drawButtons(msgBtnBlank, TFT_WHITE, colorBack,
                    msgBtnBlank, TFT_WHITE, colorBack,
                    msgBtnOk, colorComp, colorBack);
    }
    isButtonPositive = !isButtonPositive;
}

void showStartScreen()
{
    int fgColor = colorStart;
    int bgColor = colorBack;

    disp.fillRect(0, 0, 320, 190, bgColor);
    for (int i = 0; i < 4; i++)
    {
        disp.drawRect(0 + i, 0 + i, 320 - i * 2, 190 - i * 2, fgColor);
    }
    disp.setTextColor(fgColor);
    disp.setTextFont(4);
    disp.setTextSize(2);
    disp.drawCentreString("Ready to Go!", 155, 70);

    tickButtonBlink.attach_ms(buttonBlinkInterval, startButtonBlinker);

    while (1)
    {
        M5.update();
        if (M5.BtnC.wasReleased())      // To go to start training
        {
            currentMode = modeRunningScreen;
            tickButtonBlink.detach(); // Disable button blink
            break;
        }
        /*
        if (M5.BtnA.wasReleased())      // To go to setting memu
        {
            currentMode = modeSettingScreen;
            tickButtonBlink.detach();       // Disable button blink
            break;
        }
        */
        delay(30);
    }
}

void drawFrame(int frameColor, int bgColor)
{
    int yposSet = 25;
    int yposRep = 90;

    disp.fillRect(0, 0, 320, 190, bgColor);
    for (int i = 0; i < 4; i++)
    {
        disp.drawRect(0 + i, 0 + i, 320 - i * 2, 190 - i * 2, frameColor);
        disp.drawLine(280 + i, 0 + i, 280 + i, 190 - i * 2, frameColor);
    }
}

void drawFrameRunning(int currentSet, int frameColor, int bgColor)
{
    int yposSet = 25;
    int yposRep = 90;

    disp.fillRect(0, 0, 320, 190, bgColor);
    for (int i = 0; i < 4; i++)
    {
        disp.drawRect(0 + i, 0 + i, 320 - i * 2, 190 - i * 2, frameColor);
        disp.drawLine(280 + i, 0 + i, 280 + i, 190 - i * 2, frameColor);
    }
    disp.setTextColor(frameColor);
    disp.setTextSize(2);
    disp.setTextFont(2);
    disp.drawString("SET:", 10, yposSet);
    disp.drawRightString(String(currentSet), 95, yposSet); // Current Set count
    disp.drawString("/", 100, yposSet);
    disp.drawString(String(setMax), 120, yposSet); // Total Set number
    disp.setTextFont(4);
    disp.drawString("REP:", 10, yposRep);
    disp.drawRightString("0", 185, yposRep); // Initial rep count = 0
    disp.drawString("/", 190, yposRep);
    disp.drawString(String(repMax), 210, yposRep); // Total rep number
}

void showRestScreen()
{
    int fgColor = colorRest;
    int bgColor = colorBack;
    int yposRest = 75;
    int progressY;

    disp.fillRect(0, 0, 320, 240, bgColor);
    drawFrame(fgColor, bgColor);
    disp.fillRect(284, 4, 32, 183, fgColor); // Update progress bar
    disp.setTextColor(fgColor);
    disp.setTextSize(2);
    disp.setTextFont(4);
    disp.drawString("REST:", 10, yposRest);
    disp.drawString("25", 160, yposRest); // Current rep count

    // Beep to notify start of rest time
    for (int i = 0; i < 1; i++)
    {
        delay(80);
        M5.Speaker.beep();
        delay(50);
        M5.Speaker.mute();
    }

    for (int i = restTime * 8; i > 0; i--)
    {
        if (i % 8 == 0)
        {
            disp.fillRect(160, yposRest, 60, 50, colorBack);
            disp.drawRightString(String(i / 8), 220, yposRest);
        }
        progressY = 182 * (restTime * 8 - i) / restTime / 8;
        disp.fillRect(284, 4, 32, progressY, bgColor); // Update progress bar
        delay(125);
    }
}

void showSetRepScreen(int currentSet)
{
    int fgColor = colorSetRep;
    int bgColor = colorBack;
    int yposSet = 25;
    int yposRep = 90;
    int currentRep = 0;
    int swStatus[2] = {0, 0};
    int swStatusPrev[2] = {0, 0};
    boolean swPressed[2] = {false, false};
    int lastStepSide = 0; // Last leg side, 1:Right, 2:Left
    int thisStepSide = 0;
    int progressY = 0;

    // Draw initial screen
    disp.fillRect(0, 0, 320, 240, bgColor);
    drawFrame(fgColor, bgColor);

    disp.setTextColor(fgColor);
    disp.setTextSize(2);
    disp.setTextFont(2);
    disp.drawString("SET:", 10, yposSet);
    disp.drawRightString(String(currentSet), 95, yposSet); // Current Set count
    disp.drawString("/", 100, yposSet);
    disp.drawString(String(setMax), 120, yposSet); // Total Set number
    disp.setTextFont(4);
    disp.drawString("REP:", 10, yposRep);
    disp.drawRightString("0", 185, yposRep); // Initial rep count = 0
    disp.drawString("/", 190, yposRep);
    disp.drawString(String(repMax), 210, yposRep); // Total rep number

    // Beep to notify finish
    for (int i = 0; i < 2; i++)
    {
        M5.Speaker.beep();
        delay(50);
        M5.Speaker.mute();
        delay(80);
    }

    while (1)
    {
        checkSwichStatus(swStatus[0], swStatus[1]);
        thisStepSide = 0;
        for (int i = 0; i < 2; i++)
        {
            if (swStatus[i] == 1 && swStatusPrev[i] == 0)
            {
                swPressed[i] = true;
                thisStepSide |= 0x01 << 1;
            }
            swStatusPrev[i] = swStatus[i];
        }
        if (thisStepSide != 0)
        {
            // Short beep
            M5.Speaker.beep();
            tickBeep.once_ms(10, muteBeep);
            // Count up
            currentRep++;
            if (currentRep == repMax)
                break;
            disp.setTextSize(2);
            disp.setTextFont(4);
            disp.fillRect(130, yposRep, 55, 50, colorBack);
            disp.drawRightString(String(currentRep), 185, yposRep); // Update rep number
            progressY = 182 * currentRep / repMax;
            disp.fillRect(284, 186 - progressY, 32, progressY, fgColor); // Update progress bar

            for (int i = 0; i < 2; i++)
                swPressed[i] = 0;
            lastStepSide = thisStepSide;
        }
        delay(swReadInterval);
    }
}

void showFinishedScreen()
{
    int fgColor = colorSetRep;
    int bgColor = colorBack;

    disp.fillRect(0, 0, 320, 240, bgColor);
    for (int i = 0; i < 4; i++)
    {
        disp.drawRect(0 + i, 0 + i, 320 - i * 2, 190 - i * 2, fgColor);
    }
    disp.setTextColor(fgColor);
    disp.setTextFont(4);
    disp.setTextSize(2);
    disp.drawCentreString("Finished!", 155, 40);
    disp.drawCentreString("Good Job!", 155, 100);

    // Beep for finish
    for (int i = 0; i < 5; i++)
    {
        M5.Speaker.beep();
        delay(20);
        M5.Speaker.mute();
        delay(80);
    }

    tickButtonBlink.attach_ms(buttonBlinkInterval, okButtonBlinker);
    while (1)
    {
        M5.update();
        if (M5.BtnC.wasReleased())
        {
            tickButtonBlink.detach();
            break;
        }
        delay(30);
    }
}

void showRunningScreen()
{
    for (int sets = 1; sets <= setMax; sets++)
    {
        showSetRepScreen(sets);
        if (sets < setMax)
            showRestScreen();
    }
    showFinishedScreen();
    currentMode = 1;
}

void showSettingScreen()
{
}

void setup(void)
{
    Serial.begin(115200);
    M5.begin();
    M5.Speaker.begin();
    M5.Speaker.setVolume(beepVolume);

    disp.begin();

    //    showStartScreen();
    Serial.println("Start...");

    for (int i = 0; i < 2; i++)
    {
        pinMode(adcPin[i], INPUT);
    }
}

void loop()
{
    switch (currentMode)
    {
    case modeStartScreen: // Start Screen
        showStartScreen();
        break;

    case modeRunningScreen: // Running Screen
        showRunningScreen();
        break;

    case modeSettingScreen: // Setting Screen
        showSettingScreen();
        break;

    default:
        currentMode = modeStartScreen;
        break;
    }
}
