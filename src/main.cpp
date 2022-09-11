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
#include "EEPROM.h"
#include <Ticker.h>
// #include "../../include/wifiinfo.h" // 自分環境の定義ファイル。無ければこの行はコメントに。
#include "messages.h"

M5GFX disp;
Ticker tickButtonBlink;
Ticker tickBeep;

//---- HW dependent
const uint16_t adcPin[2] = {35, 36}; // 0(pin35):Right, 1(pin36):Left
//---- Display positions
const uint16_t posBtn1X = 65;
const uint16_t posBtn2X = 160;
const uint16_t posBtn3X = 255;
const uint16_t posBtnY = 216;
const uint16_t posDispSettingY = 194;
const uint16_t modeStartScreen = 1;
const uint16_t modeRunningScreen = 2;
const uint16_t modeSettingScreen = 3;
//---- Color definitions
const int colorStart = TFT_YELLOW;
const int colorSetRep = disp.color565(255, 140, 0);
const int colorRest = TFT_GREEN;
const int colorComp = colorSetRep;
const int colorSetting = TFT_WHITE;
const int colorBack = TFT_BLACK;
//---- Other constants
// const uint16_t setNum[3] = {3, 4, 5};
// const uint16_t repNum[6] = {15, 20, 25, 30, 35, 40};
// const uint16_t restNum[3] = {30, 45, 60};
// const uint16_t volNum[6] = {0, 2, 4, 6, 8, 10};
// dispVaplues[4] : Set, Rep, Rest, Vol
const uint16_t settingItems = 4;
const char *setingDispItemName[settingItems] = {"Sets", "Reps", "Rest", "Volume"};
const uint16_t settingDispValueNum[settingItems] = {3, 6, 3, 6};
const uint16_t settingDispValue[settingItems][6] = {3, 4, 5, 0, 0, 0,
                                                    15, 20, 25, 30, 35, 40,
                                                    30, 45, 60, 0, 0, 0,
                                                    0, 2, 4, 6, 8, 10};

const int eepromSize = 8;                 // 8 Bytes
const uint32_t swReadInterval = 50;       // Detection switch (Sensor) read interval: 50ms
const uint32_t buttonBlinkInterval = 750; // Button blink intercal: 750ms

//---- Default of user changeable values
uint16_t setMax = 0;     // 3 sets
uint16_t repMax = 1;     // 20 reps
uint16_t restTime = 1;   // 45 seconds
uint16_t beepVolume = 2; // level 4

//---- Others
boolean isButtonPositive = true;   // Start button color mode, positive or negative
int currentMode = modeStartScreen; // 1: Start Screen, 2: Running Screen, 3: Setting Screen
char btnText[3][10];

//---- EEPROM mapping
byte eeprom[] = {0, 0, 0, 0, 0, 0, 0, 0};
/**************************************************
    Type Name              Addr
    byte checkSum;      // 0x00
    byte setMax;        // 0x01 default = 3, 3 <= n <= 5
    byte repMax;        // 0x02 default = 20, 15 <= n <= 40, 5 step
    byte restTime;      // 0x03 default = 45, 30 <= n < 60, 15 step
    byte beepVolume;    // 0x04 default = 4, 0 <= n <= 10, 1 step
    byte rsvd0;         // 0x05
    byte rsvd1;         // 0x06
    byte rsvd2;         // 0x07
**************************************************/

//======================================

void readEeprom(byte *rom)
{
    for (int i = 0; i < eepromSize; i++)
        rom[i] = EEPROM.read(i);
}

void writeEeprom(byte *rom)
{
    rom[0] = 0;
    for (int i = 1; i < eepromSize; i++)
        rom[0] += rom[i];

    for (int i = 0; i < eepromSize; i++)
        EEPROM.write(i, rom[i]);
    EEPROM.commit();
}

void setEepromDefault(byte *rom)
{
    rom[1] = setMax;
    rom[2] = repMax;
    rom[3] = restTime;
    rom[4] = beepVolume;
    writeEeprom(rom);
}

boolean isEepromOk(byte *rom)
{
    byte sum = 0;

    if (rom[1] > 3) // Sets: 3 <= n <= 5
        return false;
    if (rom[2] > 6) // Reps: 15 <= n <= 45, step 5
        return false;
    if (rom[3] > 3) // Rest: 30 <= n <= 60, step 15
        return false;
    if (rom[4] > 6) // Volume: 0 <= n <= 10
        return false;

    for (int i = 1; i < eepromSize; i++)
        sum += rom[i];

    if (sum == rom[0])
        return true;
    else
        return false;
}

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
        delay(10); // wait to remove chattering
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
    String dispString;

    disp.fillRect(0, 0, 320, 190, bgColor);
    for (int i = 0; i < 4; i++)
    {
        disp.drawRect(0 + i, 0 + i, 320 - i * 2, 190 - i * 2, fgColor);
    }
    disp.setTextColor(fgColor);
    disp.setTextFont(4);
    disp.setTextSize(2);
    disp.drawCentreString("Ready to Go!", 155, 66);
    disp.setTextColor(TFT_ORANGE);
    disp.setTextFont(4);
    disp.setTextSize(1);
    dispString = "SET: " + String(settingDispValue[0][setMax]) + ", REP: " +
                 String(settingDispValue[1][repMax]);
    dispString += ", REST: " + String(settingDispValue[2][restTime]);
    disp.drawString(dispString, 10, 158);

    drawButtons(msgBtnSetting, TFT_WHITE, colorBack,
                msgBtnBlank, TFT_WHITE, colorBack,
                msgBtnStart, colorBack, colorStart);
    tickButtonBlink.attach_ms(buttonBlinkInterval, startButtonBlinker);

    while (1)
    {
        M5.update();
        if (M5.BtnC.wasReleased()) // To go to start training
        {
            currentMode = modeRunningScreen;
            tickButtonBlink.detach(); // Disable button blink
            break;
        }
        if (M5.BtnA.wasReleased()) // To go to setting memu
        {
            currentMode = modeSettingScreen;
            tickButtonBlink.detach(); // Disable button blink
            break;
        }
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
    disp.drawString(String(settingDispValue[0][setMax]), 120, yposSet); // Total Set number
    disp.setTextFont(4);
    disp.drawString("REP:", 10, yposRep);
    disp.drawRightString("0", 185, yposRep); // Initial rep count = 0
    disp.drawString("/", 190, yposRep);
    disp.drawString(String(settingDispValue[1][repMax]), 210, yposRep); // Total rep number
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
    disp.drawRightString(String(settingDispValue[2][restTime]), 220, yposRest); // Current rep count

    // Beep to notify start of rest time
    for (int i = 0; i < 1; i++)
    {
        delay(80);
        M5.Speaker.beep();
        delay(50);
        M5.Speaker.mute();
    }

    for (int i = settingDispValue[2][restTime] * 8; i > 0; i--)
    {
        if (i % 8 == 0)
        {
            disp.fillRect(160, yposRest, 60, 50, colorBack);
            disp.drawRightString(String(i / 8), 220, yposRest);
        }
        progressY = 182 * (settingDispValue[2][restTime] * 8 - i) / settingDispValue[2][restTime] / 8;
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
    disp.drawString(String(settingDispValue[0][setMax]), 120, yposSet); // Total Set number
    disp.setTextFont(4);
    disp.drawString("REP:", 10, yposRep);
    disp.drawRightString("0", 185, yposRep); // Initial rep count = 0
    disp.drawString("/", 190, yposRep);
    disp.drawString(String(settingDispValue[1][repMax]), 210, yposRep); // Total rep number

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
            if (currentRep == settingDispValue[1][repMax])
                break;
            disp.setTextSize(2);
            disp.setTextFont(4);
            disp.fillRect(130, yposRep, 55, 50, colorBack);
            disp.drawRightString(String(currentRep), 185, yposRep); // Update rep number
            progressY = 182 * currentRep / settingDispValue[1][repMax];
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
    for (int sets = 1; sets <= settingDispValue[0][setMax]; sets++)
    {
        showSetRepScreen(sets);
        if (sets < settingDispValue[0][setMax])
            showRestScreen();
    }
    showFinishedScreen();
    currentMode = 1;
}

void drawSettingItems(int itemNum)
{
    int fgColor1 = TFT_WHITE;
    int bgColor1 = TFT_BLACK;
    int fgColor2 = TFT_WHITE;
    int bgColor2 = TFT_BLACK;
    int posItemsX = 10;
    int posItemsY = 0;
    int posItemsH = 34;
    int posItemsW = 120;

    disp.fillRect(posItemsX + posItemsW, posItemsY, 150, posItemsH * 6, bgColor1);
    disp.setTextFont(4);
    disp.setTextSize(1);

    disp.setTextColor(fgColor1);
    for (int i = 0; i < settingItems; i++)
    {
        disp.fillRect(posItemsX, posItemsY + posItemsH * i, posItemsW, posItemsH, bgColor1);
        disp.drawString(setingDispItemName[i], posItemsX + 12, posItemsY + 4 + posItemsH * i);
    }
    disp.drawRect(posItemsX, posItemsY + posItemsH * itemNum, posItemsW, posItemsH, fgColor1);
    disp.drawRect(posItemsX + 1, posItemsY + 1 + posItemsH * itemNum, posItemsW - 2, posItemsH - 2,
                  fgColor1);
    disp.setTextColor(fgColor1);
    disp.drawString(setingDispItemName[itemNum], posItemsX + 12, posItemsY + 4 + posItemsH * itemNum);
}

void drawSettingValues(int itemNum, int valueNum)
{
    int fgColor;
    int bgColor;
    int fgColor1 = TFT_WHITE;
    int bgColor1 = TFT_BLACK;
    int fgColor2 = TFT_YELLOW;
    int bgColor2 = TFT_BLACK;
    int posValueX = 160;
    int posValueY = 0;
    int posValueH = 34;
    int posValueW = 100;
    String valueDisp;

    disp.setTextFont(4);
    disp.setTextSize(1);

    for (int i = 0; i < settingDispValueNum[itemNum]; i++)
    {
        valueDisp = String(settingDispValue[itemNum][i]);
        if (i == eeprom[itemNum + 1])
        {
            fgColor = fgColor2;
            bgColor = bgColor2;
            valueDisp += "*";
        }
        else
        {
            fgColor = fgColor1;
            bgColor = bgColor1;
        }
        disp.fillRect(posValueX, posValueY + posValueH * i, posValueW, posValueH, bgColor1);
        disp.setTextColor(fgColor);
        disp.drawCenterString(valueDisp, posValueX + posValueW / 2,
                              posValueY + 6 + posValueH * i);
    }
    disp.drawRect(posValueX, posValueY + posValueH * valueNum, posValueW, posValueH, fgColor1);
    disp.drawRect(posValueX + 1, posValueY + 1 + posValueH * valueNum, posValueW - 2, posValueH - 2,
                  fgColor1);
}

void showSettingScreen()
{
    int fgColor = colorSetting;
    int bgColor = colorBack;
    int currentSettingMode = 0; // 0:item, 1:value
    int currentSettingItem = 0;
    int currentSettingValue = 0;

    disp.fillRect(0, 0, 320, 240, bgColor);

    drawButtons(msgBtnReturn, fgColor, bgColor,
                msgBtnNext, fgColor, bgColor,
                msgBtnSelect, fgColor, bgColor);

    drawSettingItems(currentSettingItem);

    while (1)
    {
        M5.update();
        if (M5.BtnA.wasReleased()) // Retrn
        {
            if (currentSettingMode == 0) // If item selection, exit setting mode
            {
                currentMode = modeStartScreen;
                break;
            }
            else if (currentSettingMode == 1)
            {
                currentSettingMode = 0; // If value selection, return to item selection
                drawSettingItems(currentSettingItem);
            }
        }
        if (M5.BtnB.wasReleased()) // Move to next item
        {
            if (currentSettingMode == 0)
            {
                currentSettingItem++;
                if (currentSettingItem > settingItems - 1)
                    currentSettingItem = 0;
                drawSettingItems(currentSettingItem);
            }
            else if (currentSettingMode == 1)
            {
                currentSettingValue++;
                if (currentSettingValue > settingDispValueNum[currentSettingItem] - 1)
                    currentSettingValue = 0;
                drawSettingValues(currentSettingItem, currentSettingValue);
            }
        }
        if (M5.BtnC.wasReleased()) // Go to value selection
        {
            if (currentSettingMode == 0) // If item selection, exit setting mode
            {
                currentSettingMode = 1;
                currentSettingValue = eeprom[currentSettingItem + 1];
                drawSettingValues(currentSettingItem, currentSettingValue);
            }
            else if (currentSettingMode == 1)
            {
                eeprom[currentSettingItem + 1] = byte(currentSettingValue);
                writeEeprom(eeprom);
                setMax = eeprom[1];
                repMax = eeprom[2];
                restTime = eeprom[3];
                beepVolume = eeprom[4];
                drawSettingValues(currentSettingItem, currentSettingValue);
            }
        }
        delay(30);
    }
}

void setup(void)
{
    Serial.begin(115200);
    M5.begin();
    EEPROM.begin(eepromSize);
    M5.Speaker.begin();
    M5.Speaker.setVolume(beepVolume);
    disp.begin();

    readEeprom(eeprom);
    if (!isEepromOk(eeprom))
        setEepromDefault(eeprom);

    setMax = eeprom[1];
    repMax = eeprom[2];
    restTime = eeprom[3];
    beepVolume = eeprom[4];

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
