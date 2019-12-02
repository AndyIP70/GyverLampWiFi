// ************************ НАСТРОЙКИ ************************

/*
   Эффекты:
    sparklesRoutine();    // случайные цветные гаснущие вспышки
    snowRoutine();        // снег
    matrixRoutine();      // "матрица"
    starfallRoutine();    // звездопад (кометы)
    ballRoutine();        // квадратик
    ballsRoutine();       // шарики
    rainbowRoutine();     // радуга во всю матрицу горизонтальная
    rainbowDiagonalRoutine();   // радуга во всю матрицу диагональная
    fireRoutine();        // огонь

  Крутые эффекты "шума":
    madnessNoise();       // цветной шум во всю матрицу
    cloudNoise();         // облака
    lavaNoise();          // лава
    plasmaNoise();        // плазма
    rainbowNoise();       // радужные переливы
    rainbowStripeNoise(); // полосатые радужные переливы
    zebraNoise();         // зебра
    forestNoise();        // шумящий лес
    oceanNoise();         // морская вода
*/

byte lastOverlayMode;

// ************************* СВОЙ СПИСОК РЕЖИМОВ ************************
// список можно менять, соблюдая его структуру. Можно удалять и добавлять эффекты, ставить их в
// любой последовательности или вообще оставить ОДИН. Удалив остальные case и break. Cтруктура оч простая:
// case <номер>: <эффект>;
//  break;

void customRoutine(byte aMode) {
  doEffectWithOverlay(aMode); 
}

void doEffectWithOverlay(byte aMode) {

  bool effectReady = effectTimer.isReady();
  bool clockReady = clockTimer.isReady();

  if (!(effectReady || clockReady)) return;

  // Оверлей нужен для всех эффектов, иначе при малой скорости эффекта и большой скорости часов поверэ эффекта
  // цифры "смазываются"
  bool needOverlay = aMode == MC_CLOCK || (aMode != MC_TEXT && overlayEnabled);

  if (needOverlay && lastOverlayMode == thisMode) {
    if (!loadingFlag) {
      if (CLOCK_ORIENT == 0)
        clockOverlayUnwrapH(CLOCK_XC, CLOCK_Y);
      else
        clockOverlayUnwrapV(CLOCK_XC, CLOCK_Y);
    }
  }  

  if (effectReady) processEffect(aMode);

  if (clockReady) {
    byte clock_width = CLOCK_ORIENT == 0 ? 15 : 7;  // Горизонтальные часы занимают 15 колонок, вертикальные - 7
    CLOCK_MOVE_CNT--;
    if (CLOCK_MOVE_CNT <= 0) {
       CLOCK_MOVE_CNT = CLOCK_MOVE_DIVIDER;
       CLOCK_XC--;
       if (CLOCK_XC < -clock_width) {
          CLOCK_XC = WIDTH - clock_width - 1;
       }     
    }
  }
  
  if (needOverlay) {
    setOverlayColors();
    if (CLOCK_ORIENT == 0)
      clockOverlayWrapH(CLOCK_XC, CLOCK_Y);
    else  
      clockOverlayWrapV(CLOCK_XC, CLOCK_Y);       
    lastOverlayMode = thisMode;
  }  

  if (effectReady) loadingFlag = false;
  FastLED.show();
}

void processEffect(byte aMode) {
  switch (aMode) {
    case MC_NOISE_MADNESS:       madnessNoise(); break;
    case MC_NOISE_CLOUD:         cloudNoise(); break;
    case MC_NOISE_LAVA:          lavaNoise(); break;
    case MC_NOISE_PLASMA:        plasmaNoise(); break;
    case MC_NOISE_RAINBOW:       rainbowNoise(); break;
    case MC_PAINTBALL:           lightBallsRoutine(); break;
    case MC_NOISE_RAINBOW_STRIP: rainbowStripeNoise(); break;
    case MC_NOISE_ZEBRA:         zebraNoise(); break;
    case MC_NOISE_FOREST:        forestNoise(); break;
    case MC_NOISE_OCEAN:         oceanNoise(); break;
    case MC_SNOW:                snowRoutine(); break;
    case MC_SPARKLES:            sparklesRoutine(); break;
    case MC_MATRIX:              matrixRoutine(); break;
    case MC_STARFALL:            starfallRoutine(); break;
    case MC_BALL:                ballRoutine(); break;
    case MC_BALLS:               ballsRoutine(); break;
    case MC_RAINBOW_HORIZ:       rainbowHorizontal(); break;
    case MC_RAINBOW_VERT:        rainbowVertical(); break;
    case MC_RAINBOW_DIAG:        rainbowDiagonalRoutine(); break;
    case MC_FIRE:                fireRoutine(); break;
    case MC_FILL_COLOR:          fillColorProcedure(); break;
    case MC_COLORS:              colorsRoutine(); break;
    case MC_LIGHTERS:            lightersRoutine(); break;
    case MC_SWIRL:               swirlRoutine(); break;
    case MC_TEXT:                runningText(); break;
    case MC_CLOCK:               clockRoutine(); break;
    case MC_DAWN_ALARM:          dawnProcedure(); break;
    
    // Спец.режимы так же как и обычные вызываются в customModes (MC_DAWN_ALARM_SPIRAL и MC_DAWN_ALARM_SQUARE)
    case MC_DAWN_ALARM_SPIRAL:   dawnLampSpiral(); break;
    case MC_DAWN_ALARM_SQUARE:   dawnLampSquare(); break;
  }
}

// ********************* ОСНОВНОЙ ЦИКЛ РЕЖИМОВ *******************

static void nextMode() {
#if (SMOOTH_CHANGE == 1)
  fadeMode = 0;
  modeDir = true;
#else
  nextModeHandler();
#endif
}

static void prevMode() {
#if (SMOOTH_CHANGE == 1)
  fadeMode = 0;
  modeDir = false;
#else
  prevModeHandler();
#endif
}

void nextModeHandler() {

  if (useRandomSequence) {
    setRandomMode2();
    return;
  }

  byte aCnt = 0;
  byte curMode = thisMode;

  while (aCnt < MAX_EFFECT) {
    // Берем следующий режим по циклу режимов
    aCnt++; thisMode++;  
    if (thisMode >= MAX_EFFECT) thisMode = 0;

    // Если новый режим отмечен флагом "использовать" - используем его, иначе берем следующий (и проверяем его)
    if (getEffectUsage(thisMode)) break;
    
    // Если перебрали все и ни у адного нет флага "использовать" - не обращаем внимание на флаг, используем следующий
    if (aCnt >= MAX_EFFECT) {
      thisMode = curMode++;
      if (thisMode >= MAX_EFFECT) thisMode = 0;
      break;
    }
  }
  
  loadingFlag = true;
  autoplayTimer = millis();
  setTimersForMode(thisMode);
  
  FastLED.clear();
  FastLED.show();
  FastLED.setBrightness(globalBrightness);
}

void prevModeHandler() {

  if (useRandomSequence) {
    setRandomMode2();
    return;
  }

  byte aCnt = 0;
  byte curMode = thisMode;

  while (aCnt < MAX_EFFECT) {
    // Берем предыдущий режим по циклу режимов
    aCnt++; thisMode--; // thisMode: byte => при переполнении вернет 255 
    if (thisMode >= MAX_EFFECT) thisMode = MAX_EFFECT - 1;

    // Если новый режим отмечен флагом "использовать" - используем его, иначе берем следующий (и проверяем его)
    if (getEffectUsage(thisMode)) break;
    
    // Если перебрали все и ни у адного нет флага "использовать" - не обращаем внимание на флаг, используем следующий
    if (aCnt >= MAX_EFFECT) {
      thisMode = curMode--; // thisMode: byte => при переполнении вернет 255
      if (thisMode >= MAX_EFFECT) thisMode = MAX_EFFECT - 1;
      break;
    }
  }
  
  loadingFlag = true;
  autoplayTimer = millis();
  setTimersForMode(thisMode);
  
  FastLED.clear();
  FastLED.show();
  FastLED.setBrightness(globalBrightness);
}

void setTimersForMode(byte aMode) {
  effectSpeed = getEffectSpeed(aMode);
  //if (aMode == MC_PAINTBALL)
  //  effectTimer.setInterval(10);   // Режим Пейнтбол смотрится (работает) только на максимальной скорости
  //else
    effectTimer.setInterval(effectSpeed);
  byte clockSpeed = getEffectSpeed(MC_CLOCK);
  if (clockSpeed >= 240) {
    clockTimer.setInterval(4294967295);
    checkClockOrigin();
  } else {
    clockTimer.setInterval(getEffectSpeed(MC_CLOCK));
  }
}

int fadeBrightness;
int fadeStepCount = 10;     // За сколько шагов убирать/добавлять яркость при смене режимов
int fadeStepValue = 5;      // Шаг убавления яркости

#if (SMOOTH_CHANGE == 1)
void modeFader() {
  if (fadeMode == 0) {
    fadeMode = 1;
    fadeStepValue = fadeBrightness / fadeStepCount;
    if (fadeStepValue < 1) fadeStepValue = 1;
  } else if (fadeMode == 1) {
    if (changeTimer.isReady()) {
      fadeBrightness -= fadeStepValue;
      if (fadeBrightness < 0) {
        fadeBrightness = 0;
        fadeMode = 2;
      }
      FastLED.setBrightness(fadeBrightness);
    }
  } else if (fadeMode == 2) {
    fadeMode = 3;
    if (modeDir) nextModeHandler();
    else prevModeHandler();
  } else if (fadeMode == 3) {
    if (changeTimer.isReady()) {
      fadeBrightness += fadeStepValue;
      if (fadeBrightness > globalBrightness) {
        fadeBrightness = globalBrightness;
        fadeMode = 4;
      }
      FastLED.setBrightness(fadeBrightness);
    }
  }
}
#endif

void checkIdleState() {

#if (SMOOTH_CHANGE == 1)
  modeFader();
#endif
  
  if (idleState) {
    if (millis() - autoplayTimer > autoplayTime && AUTOPLAY) {    // таймер смены режима
      autoplayTimer = millis();
      nextMode();
    }
  } else {
    if (idleTimer.isReady()) {      // таймер холостого режима. Если время наступило - включить автосмену режимов 
      idleState = true;                                     
      autoplayTimer = millis();
      loadingFlag = true;
      AUTOPLAY = true;      
      FastLED.clear();
      FastLED.show();
    }
  }  
}
