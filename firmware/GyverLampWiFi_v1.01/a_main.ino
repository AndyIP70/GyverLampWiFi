#define UDP_PACKET_MAX_SIZE  512
#define PARSE_AMOUNT 8          // максимальное количество значений в массиве, который хотим получить
#define header '$'              // стартовый символ
#define divider ' '             // разделительный символ
#define ending ';'              // завершающий символ
 
int16_t intData[PARSE_AMOUNT];  // массив численных значений после парсинга - для WiFi часы время синхр м.б отрицательным + 
                                // период синхронизации м.б больше 255 мин - нужен тип int16_t
uint32_t prevColor;
boolean recievedFlag;
boolean parseStarted;
char incomeBuffer[UDP_PACKET_MAX_SIZE];           // Буфер для приема строки команды из wifi udp сокета
char replyBuffer[7];                              // ответ клиенту - подтверждения получения команды: "ack;/r/n/0"

unsigned long ackCounter = 0;
byte tmpSaveMode = 0;

void process() {  
  
  parsing();                                    // принимаем данные

  if (thisMode >= MAX_EFFECT) {
      setRandomMode2(); 
  }

  if (tmpSaveMode != thisMode) {    
    String s_tmp = String(EFFECT_LIST);    
    uint16_t len1 = s_tmp.length();
    s_tmp = GetToken(s_tmp, thisMode+1, ',');
    uint16_t len2 = s_tmp.length();
    if (len1 > len2) { 
      tmpSaveMode = thisMode;
      Serial.print(F("Включен эффект "));
      Serial.println("'" + s_tmp + "'");
    } else {
      setRandomMode2(); 
    }    
  }

  // на время принятия данных матрицу не обновляем!
  if (!parseStarted) {                          
    if (wifi_connected && useNtp) {
      if (ntp_t > 0 && millis() - ntp_t > 5000) {
        Serial.println(F("Таймаут NTP запроса!"));
        ntp_t = 0;
        ntp_cnt++;
        if (init_time && ntp_cnt >= 10) {
          Serial.println(F("Не удалось установить соединение с NTP сервером."));  
          refresh_time = false;
        }
      }
      bool timeToSync = ntpSyncTimer.isReady();
      if (timeToSync) { ntp_cnt = 0; refresh_time = true; }
      if (timeToSync || (refresh_time && ntp_t == 0 && (ntp_cnt < 10 || !init_time))) {
        getNTP();
        if (ntp_cnt >= 10) {
          if (init_time) {
            udp.flush();
          } else {
            //ESP.restart();
            ntp_cnt = 0;
            connectToNetwork();
          }
        }        
      }
    }

    // Сформировать и вывести на матрицу текущий демо-режим
    // При яркости = 1 остаются гореть только красные светодиоды и все эффекты теряют вид.
    // поэтому отображать эффект "ночные часы"
    byte br = specialMode ? specialBrightness : globalBrightness;
    if (br == 1 && !(loadingFlag || isAlarming)) {
      customRoutine(MC_CLOCK);    
    } else {    
      customRoutine(thisMode);
    }
    
    clockTicker();
    checkAlarmTime();
    checkAutoMode1Time();
    checkAutoMode2Time();

    butt.tick();  // обязательная функция отработки. Должна постоянно опрашиваться
    byte clicks = 0;

    // Один клик
    if (butt.isSingle()) clicks = 1;    
    // Двойной клик
    if (butt.isDouble()) clicks = 2;
    // Тройной клик
    if (butt.isTriple()) clicks = 3;
    // Четверной и более клик
    if (butt.hasClicks()) clicks = butt.getClicks();
    
    if (butt.isPress()) {
      // Состояние - кнопку нажали  
    }
    
    if (butt.isRelease()) {
      // Состояние - кнопку отпустили
      isButtonHold = false;
    }
    
    if (butt.isHolded()) {
      isButtonHold = true;
      if (!isTurnedOff && thisMode != MC_DAWN_ALARM) {
        if (globalBrightness == 255)
          brightDirection = false;
        else if (globalBrightness == 0)  
          brightDirection = true;
        else  
          brightDirection = !brightDirection;
      }
    }

    // Любое нажатие кнопки останавливает будильник
    if ((isAlarming || isPlayAlarmSound) && (isButtonHold || clicks > 0)) {
      stopAlarm();
      clicks = 0;
    }

    // Одинарный клик - включить . выключить лампу
    if (clicks == 1) {
      if (isTurnedOff) {
        // Если выключен - включить лампу, восстановив эффект на котором лампа была выключена
        if (saveSpecialMode && saveSpecialModeId != 0) 
          setSpecialMode(saveSpecialModeId);
        else {
          saveMode = getCurrentManualMode();
          if (saveMode == 0 && globalColor == 0) globalColor = 0xFFFFFF;
          setEffect(saveMode);
        }
      } else {
        // Сохранить текущий эффект
        saveSpecialMode = specialMode;
        saveSpecialModeId = specialModeId;
        saveMode = thisMode;
        // Выключить лампу - черный экран
        setCurrentManualMode(saveMode);
        setSpecialMode(0);
      }
    }
    
    // Прочие клики работают только если не выключено
    if (!isTurnedOff) {

      // Был двойной клик - следующий эффект, сброс автоматического переключения
      if (clicks == 2) {
        bool tmpSaveSpecial = specialMode;
        resetModes();  
        idleTimer.setInterval(4294967295);
        idleTimer.reset();        
        BTcontrol = true;
        AUTOPLAY = false;
        saveAutoplay(AUTOPLAY);        
        if (tmpSaveSpecial) setRandomMode();
        else                nextMode();
      }

      // Тройное нажатие - включить случайный режим с автосменой
      else if (clicks == 3) {
        // Включить демо-режим
        idleTimer.setInterval(idleTime);
        idleTimer.reset();        
        resetModes();  

        BTcontrol = false;
        AUTOPLAY = true;
        saveAutoplay(AUTOPLAY);
        
        setRandomMode();
      }

      // Четырехкратное нажатие включает режим "Лампа" из любого режима, в т.ч. из сна (обработка - ниже)
            
      // Пятикратное нажатие - показать текущий IP WiFi-соединения
      else if (clicks == 5) {
        showCurrentIP();
      }      
      
      // ... и т.д.
      
      // Обработка нажатой и удерживаемой кнопки
      else {
      
        if (butt.isStep() && thisMode != MC_DAWN_ALARM) {
          if (brightDirection) {
            if (globalBrightness < 10) globalBrightness += 1;
            else if (globalBrightness < 250) globalBrightness += 5;
            else {
              globalBrightness = 255;
            }
          } else {
            if (globalBrightness > 15) globalBrightness -= 5;
            else if (globalBrightness > 1) globalBrightness -= 1;
            else {
              globalBrightness = 1;
            }
          }

          specialBrightness = globalBrightness;
        
          FastLED.setBrightness(globalBrightness);
          
          saveMaxBrightness(globalBrightness);
        }
      }            
    }

    else if (isButtonHold) {
        // Включить лампу - белый цвет
        specialBrightness = 255;
        globalBrightness = 255;
        globalColor = 0xFFFFFF;
        isButtonHold = false;
        setSpecialMode(1);
        FastLED.setBrightness(globalBrightness);
    }

    // Четверное нажатие - включить белую лампу независимо от того была она выключена или включен любой другой режим
    if (clicks == 4) {
      // Включить лампу - белый цвет
      specialBrightness = 255;
      globalBrightness = 255;
      globalColor = 0xFFFFFF;
      setSpecialMode(1);
      FastLED.setBrightness(globalBrightness);
    }      

    // Есть ли изменение статуса MP3-плеера?
    if (USE_MP3 == 1 && dfPlayer.available()) {

      // Вывести детали об изменении статуса в лог
      byte msg_type = dfPlayer.readType();      
      printDetail(msg_type, dfPlayer.read());

      // Действия, которые нужно выполнить при изменении некоторых статусов:
      if (msg_type == DFPlayerCardRemoved) {
        // Карточка "отвалилась" - делаем недоступным все что связано с MP3 плеером
        isDfPlayerOk = false;
        alarmSoundsCount = 0;
        dawnSoundsCount = 0;
        Serial.println(F("MP3 плеер недоступен."));
      } else if (msg_type == DFPlayerCardOnline || msg_type == DFPlayerCardInserted) {
        // Плеер распознал карту - переинициализировать стадию 2
        InitializeDfPlayer2();
        if (!isDfPlayerOk) Serial.println(F("MP3 плеер недоступен."));
      }
    }

    // Проверить - если долгое время не было ручного управления - переключиться в автоматический режим
    if (!(isAlarming || isPlayAlarmSound)) checkIdleState();

    // Если есть несохраненные в EEPROM данные - сохранить их
    if (saveSettingsTimer.isReady()) {
      saveSettings();
    }
  }
}

byte parse_index;
String string_convert = "";
String receiveText = "";

enum eModes {NORMAL, COLOR, TEXT} parseMode;

bool haveIncomeData = false;
char incomingByte;

int16_t  bufIdx = 0;         // Могут приниматься пакеты > 255 байт - тип int16_t
int16_t  packetSize = 0;

// ********************* ПРИНИМАЕМ ДАННЫЕ **********************
void parsing() {
// ****************** ОБРАБОТКА *****************
  String str;
  byte b_tmp;
  int8_t tmp_eff;

  byte alarmDay;
  byte alarmHourVal;
  byte alarmMinuteVal;
  
  /*
    Протокол связи, посылка начинается с режима. Режимы:
    4 - яркость - 
      $4 0 value   установить текущий уровень яркости
    6 - текст $6 N|some text, где N - назначение текста;
        0 - текст бегущей строки
        1 - имя сервера NTP
        2 - SSID сети подключения
        3 - пароль для подключения к сети 
        4 - имя точки доступа
        5 - пароль к точке доступа
        6 - настройки будильников
    8 - эффект
      - $8 0 номер эффекта;
      - $8 1 N D; параметр D для эффекта N
      - $8 2 N X; вкл/выкл использовать в демо-режиме; N - номер эффекта, X=0 - не использовать X=1 - использовать 
    14 - быстрая установка ручных режимов с пред-настройками
       - $14 0;  Черный экран (выкл);  
       - $14 1;  Белый экран (освещение);  
       - $14 2;  Цветной экран;  
       - $14 3;  Огонь;  
       - $14 4;  Конфетти;  
       - $14 5;  Радуга;  
       - $14 6;  Матрица;  
       - $14 7;  Светлячки;  
       - $14 8;  Часы ночные;
       - $14 9;  Часы бегущей строкой;
       - $14 10; Часы простые;  
    15 - скорость $15 скорость таймер; 0 - таймер эффектов
    16 - Режим смены эффектов: $16 value; N:  0 - Autoplay on; 1 - Autoplay off; 2 - PrevMode; 3 - NextMode
    17 - Время автосмены эффектов и бездействия: $17 сек сек;
    18 - Запрос текущих параметров программой: $18 page;  page: 1 - настройки; 5 - эффекты; 
    19 - работа с настройками часов
    20 - настройки и управление будильников
       - $20 0;       - отключение будильника (сброс состояния isAlarming)
       - $20 1 DD EF WD DD;
           DD    - установка продолжительности рассвета (рассвет начинается за DD минут до установленного времени будильника)
           EF    - установка эффекта, который будет использован в качестве рассвета
           WD    - установка дней пн-вс как битовая маска
           DD    - продолжительность будильника DD минут после окончания рассвета
       - $20 2 X VV MA MB;
            X    - исп звук будильника X=0 - нет, X=1 - да 
           VV    - максимальная громкость
           MA    - номер файла звука будильника
           MB    - номер файла звука рассвета
       - $20 3 NN VV X; - пример звука будильника
           NN - номер файла звука будильника из папки SD:/01
           VV - уровень громкости
           X  - 1 играть 0 - остановить
       - $20 4 NN VV X; - пример звука рассвета
           NN - номер файла звука рассвета из папки SD:/02
           VV - уровень громкости
           X  - 1 играть 0 - остановить
       - $20 5 VV; - установит уровень громкости проигрывания примеров (когда уже играет)
           VV - уровень громкости
       - $20 6 D HH MM; - установит время будильника для указанного дня недели
           D  - день недели
           HH - час времени будильника
           MM - минуты времени будильника
    21 - настройки подключения к сети / точке доступа
    22 - настройки включения режимов матрицы в указанное время
       - $22 X HH MM NN
           X  - номер режима 1 или 2
           HH - час срабатывания
           MM - минуты срабатывания
           NN - эффект: -2 - выключено; -1 - выключить матрицу; 0 - случайный режим и далее по кругу; 1 и далее - список режимов ALARM_LIST 
    23 - прочие настройки
       - $23 0 VAL  - лимит по потребляемому току
  */  
  // Если прием данных завершен и управляющая команда в intData[0] распознана
  if (recievedFlag && intData[0] > 0 && intData[0] <= 23) {
    recievedFlag = false;

    // Режимы 16,17,18  не сбрасывают idleTimer
    if (intData[0] < 16 || intData[0] > 18) {
      idleTimer.reset();
      idleState = false;      
    }

    // Режимы кроме 4 (яркость), 14 (новый спец-режим) и 18 (запрос параметров страницы),
    // 19 (настройки часов), 20 (настройки будильника), 21 (настройки сети) 
    // 23 (доп.параметры) - сбрасывают спец-режим
    if (intData[0] != 4 && intData[0] != 14 && intData[0] != 18 && intData[0] != 19 &&
        intData[0] != 20 && intData[0] != 21 && intData[0] != 23) {
      if (specialMode) {
        idleTimer.setInterval(idleTime);
        idleTimer.reset();
        specialMode = false;
        isTurnedOff = false;
        isNightClock = false;
        specialModeId = -1;
      }
    }

    // Режимы кроме 18 останавливают будильник, если он работает (идет рассвет)
    if (intData[0] != 18) {
      wifi_print_ip = false;
      stopAlarm();
    }
    
    switch (intData[0]) {
      case 4:
        if (intData[1] == 0) {
          globalBrightness = intData[2];
          if (specialMode) specialBrightness = globalBrightness;
          saveMaxBrightness(globalBrightness);
          FastLED.setBrightness(globalBrightness);
          FastLED.show();
        }
        sendAcknowledge();
        break;
      case 6:
        loadingFlag = true;
        // строка принимается в формате N|text, где N:
        // 1 - имя сервера NTP
        // 2 - имя сети (SSID)
        // 3 - пароль к сети
        // 4 - имя точки доступа
        // 5 - пароль точки доступа
        // 6 - настройки будильника в формате $6 6|DD EF WD HH1 MM1 HH2 MM2 HH3 MM3 HH4 MM4 HH5 MM5 HH6 MM6 HH7 MM7
        tmp_eff = receiveText.indexOf("|");
        if (tmp_eff > 0) {
           b_tmp = receiveText.substring(0, tmp_eff).toInt();
           str = receiveText.substring(tmp_eff+1, receiveText.length()+1);
           switch(b_tmp) {
            case 1:
              str.toCharArray(ntpServerName, 30);
              setNtpServer(str);
              if (wifi_connected) {
                refresh_time = true; ntp_t = 0; ntp_cnt = 0;
              }
              break;
            case 2:
              str.toCharArray(ssid, 24);
              setSsid(str);
              break;
            case 3:
              str.toCharArray(pass, 16);
              setPass(str);
              break;
            case 4:
              str.toCharArray(apName, 10);
              setSoftAPName(str);
              break;
            case 5:
              str.toCharArray(apPass, 16);
              setSoftAPPass(str);
              // Передается в одном пакете - использовать SoftAP, имя точки и пароль
              // После получения пароля - перезапустить создание точки доступа
              if (useSoftAP) startSoftAP();
              break;
            case 6:
              // Настройки будильника в формате $6 6|DD EF WD AD HH1 MM1 HH2 MM2 HH3 MM3 HH4 MM4 HH5 MM5 HH6 MM6 HH7 MM7
              // DD    - установка продолжительности рассвета (рассвет начинается за DD минут до установленного времени будильника)
              // EF    - установка эффекта, который будет использован в качестве рассвета
              // WD    - установка дней пн-вс как битовая маска
              // AD    - продолжительность "звонка" сработавшего будильника
              // HHx   - часы дня недели x (1-пн..7-вс)
              // MMx   - минуты дня недели x (1-пн..7-вс)
              //
              // Остановить будильнтк, если он сработал
              if (isDfPlayerOk) {
                dfPlayer.stop();
              }
              soundFolder = 0;
              soundFile = 0;
              isAlarming = false;
              isAlarmStopped = false;

              // Настройки содержат 18 элеиентов (см. формат выше)
              tmp_eff = CountTokens(str, ' ');
              if (tmp_eff == 18) {
              
                dawnDuration = constrain(GetToken(str, 1, ' ').toInt(),1,59);
                alarmEffect = GetToken(str, 2, ' ').toInt();
                alarmWeekDay = GetToken(str, 3, ' ').toInt();
                alarmDuration = constrain(GetToken(str, 4, ' ').toInt(),1,10);
                saveAlarmParams(alarmWeekDay,dawnDuration,alarmEffect,alarmDuration);
                
                for(byte i=0; i<7; i++) {
                  alarmHourVal = constrain(GetToken(str, i*2+5, ' ').toInt(), 0, 23);
                  alarmMinuteVal = constrain(GetToken(str, i*2+6, ' ').toInt(), 0, 59);
                  alarmHour[i] = alarmHourVal;
                  alarmMinute[i] = alarmMinuteVal;
                  setAlarmTime(i+1, alarmHourVal, alarmMinuteVal);
                }

                // Рассчитать время начала рассвета будильника
                calculateDawnTime();            
              }
              break;
           }
        }
        saveSettings();
        if (b_tmp == 6) 
          sendPageParams(4);
        else
          sendAcknowledge();
        break;
      case 8:      
        tmp_eff = intData[2];        
        // intData[1] : дейстие -> 0 - выбор эффекта;  1 - параметр; 2 - вкл/выкл "использовать в демо-режиме"
        // intData[2] : номер эффекта
        // intData[3] : действие = 1: значение параметра; действие = 2: 0 - выкл; 1 - вкл;
        if (intData[1] == 0) {          
          // Если в приложении выбраны часы, но они недоступны из за размеров матрицы - брать следующий эффект
          if (tmp_eff == MC_CLOCK){
             if (!(allowHorizontal || allowVertical)) tmp_eff++;
          }          
          setEffect(tmp_eff);
          BTcontrol = true;
          loadingFlag = intData[1] == 0;
          if (tmp_eff == MC_FILL_COLOR && globalColor == 0x000000) globalColor = 0xffffff;
        } else if (intData[1] == 1) {
          // Параметр эффекта
          setScaleForEffect(tmp_eff, intData[3]); 
          effectScaleParam[tmp_eff] = intData[3];
          if (tmp_eff == MC_FILL_COLOR) {  
            globalColor = getColorInt(CHSV(getEffectSpeed(MC_FILL_COLOR), effectScaleParam[MC_FILL_COLOR], 255));
            setGlobalColor(globalColor);
          }
        } else if (intData[1] == 2) {
          // Вкл/выкл использование эффекта в демо-режиме
          saveEffectUsage(tmp_eff, intData[3] == 1); 
        }
        // Для "0" и "2" - отправляются параметры, подтверждение отправлять не нужно. Для остальных - нужно
        if (intData[1] == 0 || intData[1] == 2) {
          sendPageParams(2);
        } else { 
          sendAcknowledge();
        }
        break;
      case 14:
        if (intData[1] == 2) {
           // Если в строке цвет - "$14 2 00FFAA;" - цвет лампы, сохраняемый в globalColor
           str = String(incomeBuffer).substring(6,12); // $14 2 00FFAA;
           globalColor = (uint32_t)HEXtoInt(str);
        }
        setSpecialMode(intData[1]);
        sendPageParams(1);
        break;
      case 15: 
        if (intData[2] == 0) {
          if (intData[1] == 255) intData[1] = 254;
          effectSpeed = 255 - intData[1]; 
          saveEffectSpeed(thisMode, effectSpeed);
          if (thisMode == MC_FILL_COLOR) { 
            globalColor = getColorInt(CHSV(effectSpeed, effectScaleParam[MC_FILL_COLOR], 255));
            setGlobalColor(globalColor);
          }
          setTimersForMode(thisMode);           
        }
        sendAcknowledge();
        break;
      case 16:
        BTcontrol = intData[1] == 1;
        if (intData[1] == 0) AUTOPLAY = true;
        else if (intData[1] == 1) AUTOPLAY = false;
        else if (intData[1] == 2) prevMode();
        else if (intData[1] == 3) nextMode();
        else if (intData[1] == 4) AUTOPLAY = intData[2] == 1;
        else if (intData[1] == 5) useRandomSequence = intData[2] == 1;

        idleState = !BTcontrol && AUTOPLAY; 
        if (AUTOPLAY) {
          autoplayTimer = millis(); // При включении автоматического режима сбросить таймер автосмены режимов
        }
        saveAutoplay(AUTOPLAY);
        saveRandomMode(useRandomSequence);
        
        setCurrentManualMode(AUTOPLAY ? -1 : (int8_t)thisMode);
        if (getCurrentManualMode() >= 0) {
          setCurrentSpecMode(-1);
        }

        loadingFlag = true;

        if (!BTcontrol && AUTOPLAY) {
          sendPageParams(1);
        } else {        
          sendAcknowledge();
        }
        break;
      case 17: 
        autoplayTime = ((long)intData[1] * 1000);   // секунды -> миллисек 
        idleTime = ((long)intData[2] * 60 * 1000);  // минуты -> миллисек
        saveAutoplayTime(autoplayTime);
        saveIdleTime(idleTime);
        if (AUTOPLAY) {
          autoplayTimer = millis();
          BTcontrol = false;
        }
        if (idleTime == 0) // тамймер отключен
          idleTimer.setInterval(4294967295);
        else
          idleTimer.setInterval(idleTime);
        idleTimer.reset();
        idleState = !BTcontrol && AUTOPLAY; 
        sendAcknowledge();
        break;
      case 18: 
        if (intData[1] == 0) { // ping
          sendAcknowledge();
        } else {               // запрос параметров страницы приложения
          sendPageParams(intData[1]);
        }
        break;
      case 19: 
         switch (intData[1]) {
           case 1:               // $19 1 X; - сохранить настройку X "Часы в эффектах"
             overlayEnabled = (CLOCK_ORIENT == 0 && allowHorizontal || CLOCK_ORIENT == 1 && allowVertical) ? intData[2] == 1 : false;
             saveClockOverlayEnabled(overlayEnabled);
             if (specialMode) specialClock = overlayEnabled;
             break;
           case 2:               // $19 2 X; - Использовать синхронизацию часов NTP  X: 0 - нет, 1 - да
             useNtp = intData[2] == 1;
             saveUseNtp(useNtp);
             if (wifi_connected) {
               refresh_time = true; ntp_t = 0; ntp_cnt = 0;
             }
             break;
           case 3:               // $19 3 N Z; - Период синхронизации часов NTP и Часовой пояс
             SYNC_TIME_PERIOD = intData[2];
             timeZoneOffset = (int8_t)intData[3];
             saveTimeZone(timeZoneOffset);
             saveNtpSyncTime(SYNC_TIME_PERIOD);
             saveTimeZone(timeZoneOffset);
             ntpSyncTimer.setInterval(1000 * 60 * SYNC_TIME_PERIOD);
             if (wifi_connected) {
               refresh_time = true; ntp_t = 0; ntp_cnt = 0;
             }
             break;
           case 4:
             needTurnOffClock = intData[2] == 1;
             setTurnOffClockOnLampOff(needTurnOffClock);
             break;
           case 5:               // $19 5 X; - Режим цвета часов оверлея X: 0,1,2,3
             COLOR_MODE = intData[2];
             if (COLOR_MODE > 3) COLOR_MODE = 0;
             setScaleForEffect(MC_CLOCK, COLOR_MODE);
             break;
           case 6:               // $19 6 X; - Ориентация часов  X: 0 - горизонтально, 1 - вертикально
             CLOCK_ORIENT = intData[2] == 1 ? 1  : 0;             
             if (allowHorizontal || allowVertical) {
               if (CLOCK_ORIENT == 0 && !allowHorizontal) CLOCK_ORIENT = 1;
               if (CLOCK_ORIENT == 1 && !allowVertical) CLOCK_ORIENT = 0;              
             } else {
               overlayEnabled = false;
               saveClockOverlayEnabled(overlayEnabled);
             }
             // Центрируем часы по горизонтали/вертикали по ширине / высоте матрицы
             checkClockOrigin();
             saveClockOrientation(CLOCK_ORIENT);
             break;
           case 8:               // $19 8 YYYY MM DD HH MM; - Установить текущее время YYYY.MM.DD HH:MM
             setTime(intData[5],intData[6],0,intData[4],intData[3],intData[2]);
             init_time = true; refresh_time = false; ntp_cnt = 0;
             break;
           case 9:               // $19 9 X; - Формат часов бегущей строки: 0 - только часы; 1 - часы и дата кратко; 2 - часы и дата полностью
             formatClock = intData[2];
             if (formatClock > 2) formatClock = 0;
             setFormatClock(formatClock);
             break;
           case 10:               // $19 10 X; - Цвет ночных часов:  0 - R; 1 - G; 2 - B; 3 - C; 3 - M; 5 - Y; 6 - W;
             setNightClockColor(intData[2]);
             nightClockColor = getNightClockColor();
             break;
           case 11:               // $19 11 X; - Режим цвета часов бегущей строкой X: 0,1,2,           
             COLOR_TEXT_MODE = intData[2];
             if (COLOR_TEXT_MODE > 2) COLOR_TEXT_MODE = 0;
             setScaleForEffect(MC_TEXT, COLOR_TEXT_MODE);
             break;
           case 12:               // $19 12 X; - скорость прокрутки часов оверлея или 0, если часы остановлены по центру
             saveEffectSpeed(MC_CLOCK, 255 - intData[2]);
             setTimersForMode(thisMode);
             break;
           case 13:               // $19 13 X; - скорость прокрутки часов бегущей строкой
             saveEffectSpeed(MC_TEXT, 255 - intData[2]);
             if (modeCode == MC_TEXT)
               setTimersForMode(MC_TEXT);
             break;
           case 14:               // $19 14 00FFAA;
             // В строке цвет - "$19 14 00FFAA;" - цвет часов оверлея, сохраняемый в globalClockColor
             str = String(incomeBuffer).substring(7,13);
             globalClockColor = (uint32_t)HEXtoInt(str);
             setGlobalClockColor(globalClockColor);
             break;
           case 15:               // $19 15 00FFAA;
             // В строке цвет - "$19 15 00FFAA;" - цвет часов текстовой строкой для режима "монохромный", сохраняемый в globalTextColor
             str = String(incomeBuffer).substring(7,13);
             globalTextColor = (uint32_t)HEXtoInt(str);
             setGlobalTextColor(globalTextColor);
             break;
        }
        if (intData[1] != 8) {
          sendPageParams(3);
        } else {
          sendAcknowledge();
        }
        break;
      case 20:
        switch (intData[1]) { 
          case 0:  
            if (isAlarming || isPlayAlarmSound) stopAlarm();            
            break;
          case 2:
            if (isDfPlayerOk) {
              // $20 2 X VV MA MB;
              //    X    - исп звук будильника X=0 - нет, X=1 - да 
              //   VV    - максимальная громкость
              //   MA    - номер файла звука будильника
              //   MB    - номер файла звука рассвета
              dfPlayer.stop();
              soundFolder = 0;
              soundFile = 0;
              isAlarming = false;
              isAlarmStopped = false;

              useAlarmSound = intData[2] == 1;
              maxAlarmVolume = constrain(intData[3],0,30);
              alarmSound = intData[4] - 2;  // Индекс от приложения: 0 - нет; 1 - случайно; 2 - 1-й файл; 3 - ... -> -1 - нет; 0 - случайно; 1 - 1-й файл и т.д
              dawnSound = intData[5] - 2;   // Индекс от приложения: 0 - нет; 1 - случайно; 2 - 1-й файл; 3 - ... -> -1 - нет; 0 - случайно; 1 - 1-й файл и т.д
              saveAlarmSounds(useAlarmSound, maxAlarmVolume, alarmSound, dawnSound);
            }
            break;
          case 3:
            if (isDfPlayerOk) {
              // $20 3 X NN VV; - пример звука будильника
              //  X  - 1 играть 0 - остановить
              //  NN - номер файла звука будильника из папки SD:/01
              //  VV - уровень громкости
              if (intData[2] == 0) {
                StopSound(0);
                soundFolder = 0;
                soundFile = 0;
              } else {
                b_tmp = intData[3] - 2;  // Знач: -1 - нет; 0 - случайно; 1 и далее - файлы; -> В списке индексы: 1 - нет; 2 - случайно; 3 и далее - файлы
                if (b_tmp > 0 && b_tmp <= alarmSoundsCount) {
                  dfPlayer.stop();
                  soundFolder = 1;
                  soundFile = b_tmp;
                  dfPlayer.volume(constrain(intData[4],0,30));
                  dfPlayer.playFolder(soundFolder, soundFile);
                  dfPlayer.enableLoop();
                } else {
                  soundFolder = 0;
                  soundFile = 0;
                }
              }
            }  
            break;
          case 4:
            if (isDfPlayerOk) {
              // $20 4 X NN VV; - пример звука рассвета
              //    X  - 1 играть 0 - остановить
              //    NN - номер файла звука рассвета из папки SD:/02
              //    VV - уровень громкости
              if (intData[2] == 0) {
                StopSound(0);
                soundFolder = 0;
                soundFile = 0;
              } else {
                dfPlayer.stop();
                b_tmp = intData[3] - 2; // Знач: -1 - нет; 0 - случайно; 1 и далее - файлы; -> В списке индексы: 1 - нет; 2 - случайно; 3 и далее - файлы
                if (b_tmp > 0 && b_tmp <= dawnSoundsCount) {
                  soundFolder = 2;
                  soundFile = b_tmp;
                  dfPlayer.volume(constrain(intData[4],0,30));
                  dfPlayer.playFolder(soundFolder, soundFile);
                  dfPlayer.enableLoop();
                } else {
                  soundFolder = 0;
                  soundFile = 0;
                }
              }
            }
            break;
          case 5:
            if (isDfPlayerOk && soundFolder > 0) {
             // $20 5 VV; - установит уровень громкости проигрывания примеров (когда уже играет)
             //    VV - уровень громкости
             maxAlarmVolume = constrain(intData[2],0,30);
             dfPlayer.volume(maxAlarmVolume);
            }
            break;
        }
        if (intData[1] == 0) {
          sendPageParams(4);
        } else if (intData[1] == 1 || intData[1] == 2) { // Режимы установки параметров - сохранить
          // saveSettings();
          sendPageParams(4);
        } else {
          sendPageParams(96);
        }        
        break;
      case 21:
        // Настройки подключения к сети
        switch (intData[1]) { 
          // $21 0 0 - не использовать точку доступа $21 0 1 - использовать точку доступа
          case 0:  
            useSoftAP = intData[2] == 1;
            setUseSoftAP(useSoftAP);
            if (useSoftAP && !ap_connected) 
              startSoftAP();
            else if (!useSoftAP && ap_connected) {
              if (wifi_connected) { 
                ap_connected = false;              
                WiFi.softAPdisconnect(true);
                Serial.println(F("Точка доступа отключена."));
              }
            }      
            break;
          case 1:  
            // $21 1 IP1 IP2 IP3 IP4 - установить статический IP адрес подключения к локальной WiFi сети, пример: $21 1 192 168 0 106
            // Локальная сеть - 10.х.х.х или 172.16.х.х - 172.31.х.х или 192.168.х.х
            // Если задан адрес не локальной сети - сбросить его в 0.0.0.0, что означает получение динамического адреса 
            if (!(intData[2] == 10 || intData[2] == 172 && intData[3] >= 16 && intData[3] <= 31 || intData[2] == 192 && intData[3] == 168)) {
              intData[2] = 0;
              intData[3] = 0;
              intData[4] = 0;
              intData[5] = 0;
            }
            saveStaticIP(intData[2], intData[3], intData[4], intData[5]);
            break;
          case 2:  
            // $21 2; Выполнить переподключение к сети WiFi
            FastLED.clear();
            FastLED.show();
            startWiFi();
            wifi_print_ip = true;
            wifi_print_idx = 0;       
            break;
        }
        if (intData[1] == 0 || intData[1] == 1) {
          sendAcknowledge();
        } else {
          sendPageParams(5);
        }
        break;
      case 22:
      /*  22 - настройки включения режимов матрицы в указанное время
       - $22 HH1 MM1 NN1 HH2 MM2 NN2
           HHn - час срабатывания
           MMn - минуты срабатывания
           NNn - эффект: -2 - выключено; -1 - выключить матрицу; 0 - случайный режим и далее по кругу; 1 и далее - список режимов EFFECT_LIST 
      */    
        AM1_hour = intData[1];
        AM1_minute = intData[2];
        AM1_effect_id = intData[3];
        if (AM1_hour < 0) AM1_hour = 0;
        if (AM1_hour > 23) AM1_hour = 23;
        if (AM1_minute < 0) AM1_minute = 0;
        if (AM1_minute > 59) AM1_minute = 59;
        if (AM1_effect_id < -2) AM1_effect_id = -2;
        setAM1params(AM1_hour, AM1_minute, AM1_effect_id);
        AM2_hour = intData[4];
        AM2_minute = intData[5];
        AM2_effect_id = intData[6];
        if (AM2_hour < 0) AM2_hour = 0;
        if (AM2_hour > 23) AM2_hour = 23;
        if (AM2_minute < 0) AM2_minute = 0;
        if (AM2_minute > 59) AM2_minute = 59;
        if (AM2_effect_id < -2) AM2_effect_id = -2;
        setAM2params(AM2_hour, AM2_minute, AM2_effect_id);

        saveSettings();
        sendPageParams(6);
        break;
      case 23:
        // $23 0 VAL - лимит по потребляемому току
        switch(intData[1]) {
          case 0:
            setPowerLimit(intData[2]);
            CURRENT_LIMIT = getPowerLimit();
            FastLED.setMaxPowerInVoltsAndMilliamps(5, CURRENT_LIMIT == 0 ? 100000 : CURRENT_LIMIT);            
            break;
        }
        break;
    }
  }

  // ****************** ПАРСИНГ *****************

  // Если предыдущий буфер еще не разобран - новых данных из сокета не читаем, продолжаем разбор уже считанного буфера
  haveIncomeData = bufIdx > 0 && bufIdx < packetSize; 
  if (!haveIncomeData) {
    packetSize = udp.parsePacket();      
    haveIncomeData = packetSize > 0;      
  
    if (haveIncomeData) {                
      // read the packet into packetBufffer
      int len = udp.read(incomeBuffer, UDP_PACKET_MAX_SIZE);
      if (len > 0) {          
        incomeBuffer[len] = 0;
      }
      bufIdx = 0;
      
      delay(0);            // ESP8266 при вызове delay отпрабатывает стек IP протокола, дадим ему поработать        

      Serial.print(F("UDP пакeт размером "));
      Serial.print(packetSize);
      Serial.print(F(" от "));
      IPAddress remote = udp.remoteIP();
      for (int i = 0; i < 4; i++) {
        Serial.print(remote[i], DEC);
        if (i < 3) {
          Serial.print(F("."));
        }
      }
      Serial.print(F(", порт "));
      Serial.println(udp.remotePort());
      if (udp.remotePort() == localPort) {
        Serial.print(F("Содержимое: "));
        Serial.println(incomeBuffer);
      }
    }

    // NTP packet from time server
    if (haveIncomeData && udp.remotePort() == 123) {
      parseNTP();
      haveIncomeData = false;
      bufIdx = 0;      
    }
  }

  if (haveIncomeData) {         

    // Из за ошибки в компоненте UdpSender в Thunkable - теряются половина отправленных 
    // символов, если их кодировка - двухбайтовый UTF8, т.к. оно вычисляет длину строки без учета двухбайтовости
    // Чтобы символы не терялись - при отправке строки из андроид-программы, она добивается с конца пробелами
    // Здесь эти конечные пробелы нужно предварительно удалить
    while (packetSize > 0 && incomeBuffer[packetSize-1] == ' ') packetSize--;
    incomeBuffer[packetSize] = 0;

    if (parseMode == TEXT) {                         // если нужно принять строку - принимаем всю

        // Оставшийся буфер преобразуем с строку
        if (intData[0] == 6) {  // текст
          receiveText = String(&incomeBuffer[bufIdx]);
          receiveText.trim();
        }
                  
        incomingByte = ending;                       // сразу завершаем парс
        parseMode = NORMAL;
        bufIdx = 0; 
        packetSize = 0;                              // все байты из входящего пакета обработаны
      } else {
        incomingByte = incomeBuffer[bufIdx++];       // обязательно ЧИТАЕМ входящий символ
    } 
  }       
    
  if (haveIncomeData) {

    if (parseStarted) {                                             // если приняли начальный символ (парсинг разрешён)
      if (incomingByte != divider && incomingByte != ending) {      // если это не пробел И не конец
        string_convert += incomingByte;                             // складываем в строку
      } else {                                                      // если это пробел или ; конец пакета
        if (parse_index == 0) {
          byte cmdMode = string_convert.toInt();
          intData[0] = cmdMode;
          if (cmdMode == 6) {
            parseMode = TEXT;
          }
          else parseMode = NORMAL;
        }

        if (parse_index == 1) {       // для второго (с нуля) символа в посылке
          if (parseMode == NORMAL) intData[parse_index] = string_convert.toInt();           // преобразуем строку в int и кладём в массив}
          if (parseMode == COLOR) {                                                         // преобразуем строку HEX в цифру
            globalColor = (uint32_t)HEXtoInt(string_convert);           
            setGlobalColor(globalColor);
            if (intData[0] == 0) {
              incomingByte = ending;
              parseStarted = false;
              BTcontrol = true;
            } else {
              parseMode = NORMAL;
            }
          }
        } else {
          intData[parse_index] = string_convert.toInt();  // преобразуем строку в int и кладём в массив
        }
        string_convert = "";                        // очищаем строку
        parse_index++;                              // переходим к парсингу следующего элемента массива
      }
    }

    if (incomingByte == header) {                   // если это $
      parseStarted = true;                          // поднимаем флаг, что можно парсить
      parse_index = 0;                              // сбрасываем индекс
      string_convert = "";                          // очищаем строку
    }

    if (incomingByte == ending) {                   // если таки приняли ; - конец парсинга
      parseMode == NORMAL;
      parseStarted = false;                         // сброс
      recievedFlag = true;                          // флаг на принятие
      bufIdx = 0;
    }

    if (bufIdx >= packetSize) {                     // Весь буфер разобран 
      bufIdx = 0;
      packetSize = 0;
    }
  }
}

void sendPageParams(int page) {
  // W:число     ширина матрицы
  // H:число     высота матрицы
  // DM:Х        демо режим, где Х = 0 - выкл (ручное управление); 1 - вкл
  // AP:Х        автосменарежимов, где Х = 0 - выкл; 1 - вкл
  // RM:Х        смена режимов в случайном порядке, где Х = 0 - выкл; 1 - вкл
  // PD:число    продолжительность режима в секундах
  // IT:число    время бездействия в секундах
  // BR:число    яркость
  // EF:число    текущий эффект
  // SE:число    скорость эффектов
  // SS:число    параметр эффекта "масштаб"
  // NP:Х        использовать NTP, где Х = 0 - выкл; 1 - вкл
  // NT:число    период синхронизации NTP в минутах
  // NZ:число    часовой пояс -12..+12
  // NS:[текст]  сервер NTP, ограничители [] обязательны
  // NС:Х        цвет ночных часов, где Х = 0 - R; 1 - G; 2 - B; 3 - C; 4 - M; 5 - Y;
  // OF:X        выключать часы вместе с лампой 0-нет, 1-да
  // UE:X        использовать эффект в демо-режиме 0-нет, 1-да
  // LE:[список] список эффектов, разделенный запятыми, ограничители [] обязательны        
  // AL:X        сработал будильник 0-нет, 1-да
  // AT:HH MM    часы-минуты времени будильника -> например "09 15"
  // AW:число    битовая маска дней недели будильника b6..b0: b0 - пн .. b7 - вс
  // AD:число    продолжительность рассвета, мин
  // AE:число    эффект, использующийся для будильника
  // AO:X        включен будильник 0-нет, 1-да
  // NW:[текст]  SSID сети подключения
  // NA:[текст]  пароль подключения к сети
  // AU:X        создавать точку доступа 0-нет, 1-да
  // AN:[текст]  имя точки доступа
  // AA:[текст]  пароль точки доступа
  // MX:X        MP3 плеер доступен для использования 0-нет, 1-да
  // MU:X        использовать звук в будильнике 0-нет, 1-да
  // MD:число    сколько минут звучит будильник, если его не отключили
  // MV:число    максимальная громкость будильника
  // MA:число    номер файла звука будильника из SD:/01
  // MB:число    номер файла звука рассвета из SD:/02
  // MP:папка.файл  номер папки и файла звука который проигрывается
  // IP:xx.xx.xx.xx Текущий IP адрес WiFi соединения в сети
  // AM1H:HH     час включения режима 1     00..23
  // AM1M:MM     минуты включения режима 1  00..59
  // AM1E:NN     номер эффекта режима 1:   -2 - не используется; -1 - выключить матрицу; 0 - включить случайный с автосменой; 1 - номер режима из спписка EFFECT_LIST
  // AM2H:HH     час включения режима 2     00..23
  // AM2M:MM     минуты включения режима 2  00..59
  // AM2E:NN     номер эффекта режима 1:   -2 - не используется; -1 - выключить матрицу; 0 - включить случайный с автосменой; 1 - номер режима из спписка EFFECT_LIST
  // PW:число    ограничение по току в миллиамперах
  // CE:X        оверлей часов вкл/выкл, где Х = 0 - выкл; 1 - вкл (использовать часы в эффектах)
  // CС:X        режим цвета часов оверлея: 0,1,2
  // CT:X        режим цвета часов текстовой строкой: 0,1,2
  // CO:X        ориентация часов: 0 - горизонтально, 1 - вертикально
  // CF:X        формат часов бегущей строкой: 0 - только часы, 1 - часы и дата кратко; 2 - часы и дата полностью
  // SC:число    скорость смещения часов оверлея
  // ST:число    скорость смещения текстовых часов
  // C1:цвет     цвет режима "монохром" часов оверлея; цвет: 192,96,96 - R,G,B
  // C2:цвет     цвет режима "монохром" текстовых часов; цвет: 192,96,96 - R,G,B

  String str = "", color, text;
  boolean allowed;
  byte b_tmp;
  CRGB c1, c2;
  
  switch (page) { 
    case 1:  // Настройки. Вернуть: Ширина/Высота матрицы; Яркость; Деморежм и Автосмена; Время смены режимо
      str="$18 W:"+String(WIDTH)+"|H:"+String(HEIGHT)+"|DM:";
      if (BTcontrol)  str+="0|AP:"; else str+="1|AP:";
      if (AUTOPLAY)   str+="1|BR:"; else str+="0|BR:";
      str+=String(globalBrightness) + "|PD:" + String(autoplayTime / 1000) + "|IT:" + String(idleTime / 60 / 1000) +  "|AL:";
      if ((isAlarming || isPlayAlarmSound) && !isAlarmStopped) str+="1"; else str+="0";
      str+="|RM:" + String(useRandomSequence);
      str+="|PW:" + String(CURRENT_LIMIT);
      str+=";";
      break;
    case 2:  // Эффекты. Вернуть: Номер эффекта, Остановлен или играет; Яркость; Скорость эффекта; Использовать в демо 
      allowed = false;
      str="$18 EF:"+String(thisMode+1);
      str+="|BR:"+String(globalBrightness);
      if (getEffectUsage(thisMode))
          str+="|UE:1";
      else    
          str+="|UE:0";
      // Эффекты не имеющие настройки скорости отправляют значение "Х" - программа делает ползунок настройки недоступным
      str+="|SE:"+(thisMode == MC_CLOCK || thisMode == MC_PAINTBALL
         ? "X" 
         : String(255 - constrain(map(effectSpeed, D_EFFECT_SPEED_MIN,D_EFFECT_SPEED_MAX, 0, 255), 0,255)));
      // Эффекты не имеющие настройки вариации отправляют значение "Х" - программа делает ползунок настройки недоступным
      str+="|SS:"+(thisMode == MC_DAWN_ALARM || thisMode == MC_RAINBOW_DIAG || thisMode == MC_BALLS || thisMode == MC_STARFALL || thisMode == MC_COLORS
         ? "X" 
         : String(effectScaleParam[thisMode]));
      str+=";";
      break;
    case 3:  // Настройки часов.
      c1 = CRGB(globalClockColor);
      c2 = CRGB(globalTextColor);
      // Часы могут отображаться: 
      // - вертикальные при высоте матрицы >= 11 и ширине >= 7; 
      // - горизонтальные при ширене матрицы >= 15 и высоте >= 5
      // Настройки часов можно отображать только если часы доступны по размерам: - или вертикальные или горизонтальные часы влазят на матрицу
      // Настройки ориентации имеют смыcл только когда И горизонтальные И вертикальные часы могут быть отображены на матрице; В противном случае - смысла нет, так как выбор очевиден (только один вариант)
      str="$18 CE:"+(allowVertical || allowHorizontal ? String(getClockOverlayEnabled()) : "X") + "|CC:" + String(COLOR_MODE) + 
          "|CO:" + (allowVertical && allowHorizontal ? String(CLOCK_ORIENT) : "X") + 
          "|NC:" + String(nightClockColor) + "|CF:" + String(formatClock) + "|CT:" + String(COLOR_TEXT_MODE);
      str += "|SC:" + String(255 - getEffectSpeed(MC_CLOCK)) + "|ST:" + String(255 - getEffectSpeed(MC_TEXT));
      str += "|C1:" + String(c1.r) + "," + String(c1.g) + "," + String(c1.b);
      str += "|C2:" + String(c2.r) + "," + String(c2.g) + "," + String(c2.b);      
      str += "|NP:"; 
      if (useNtp)  str+="1|NT:"; else str+="0|NT:";
      str+=String(SYNC_TIME_PERIOD) + "|NZ:" + String(timeZoneOffset);
      str+="|NS:["+String(ntpServerName)+"]";
      if (needTurnOffClock)
          str+="|OF:1";
      else    
          str+="|OF:0";
      str+=";";
      break;
    case 4:  // Настройки будильника
      str="$18 AL:"; 
      if ((isAlarming || isPlayAlarmSound) && !isAlarmStopped) str+="1|AD:"; else str+="0|AD:";
      str+=String(dawnDuration)+"|AW:";
      for (int i=0; i<7; i++) {
         if (((alarmWeekDay>>i) & 0x01) == 1) str+="1"; else str+="0";  
         if (i<6) str+='.';
      }
      for (int i=0; i<7; i++) {      
            str+="|AT:"+String(i+1)+" "+String(alarmHour[i])+" "+String(alarmMinute[i]);
      }
      str+="|AE:" + String(alarmEffect + 1);                   // Индекс в списке в приложении смартфона начинается с 1
      str+="|MX:" + String(isDfPlayerOk ? "1" : "0");          // 1 - MP3 доступен; 0 - MP3 не доступен
      str+="|MU:" + String(useAlarmSound ? "1" : "0");         // 1 - использовать звук; 0 - MP3 не использовать звук
      str+="|MD:" + String(alarmDuration); 
      str+="|MV:" + String(maxAlarmVolume); 
      if (soundFolder == 0) {      
        str+="|MA:" + String(alarmSound+2);                      // Знач: -1 - нет; 0 - случайно; 1 и далее - файлы; -> В списке индексы: 1 - нет; 2 - случайно; 3 и далее - файлы
        str+="|MB:" + String(dawnSound+2);                       // Знач: -1 - нет; 0 - случайно; 1 и далее - файлы; -> В списке индексы: 1 - нет; 2 - случайно; 3 и далее - файлы
      } else if (soundFolder == 1) {      
        str+="|MB:" + String(dawnSound+2);                       // Знач: -1 - нет; 0 - случайно; 1 и далее - файлы; -> В списке индексы: 1 - нет; 2 - случайно; 3 и далее - файлы
      } else if (soundFolder == 2) {      
        str+="|MA:" + String(alarmSound+2);                      // Знач: -1 - нет; 0 - случайно; 1 и далее - файлы; -> В списке индексы: 1 - нет; 2 - случайно; 3 и далее - файлы
      }
      str+="|MP:" + String(soundFolder) + '~' + String(soundFile+2); 
      str+=";";
      break;
    case 5:  // Настройки подключения
      str="$18 AU:"; 
      if (useSoftAP) str+="1|AN:["; else str+="0|AN:[";
      str+=String(apName) + "]|AA:[";
      str+=String(apPass) + "]|NW:[";
      str+=String(ssid) + "]|NA:[";
      str+=String(pass) + "]|IP:";
      if (wifi_connected) str += WiFi.localIP().toString(); 
      else                str += String(F("нет подключения"));
      str+=";";
      break;
    case 6:  // Настройки режимов автовключения по времени
      str="$18 AM1T:"+String(AM1_hour)+" "+String(AM1_minute)+"|AM1A:"+String(AM1_effect_id)+
             "|AM2T:"+String(AM2_hour)+" "+String(AM2_minute)+"|AM2A:"+String(AM2_effect_id); 
      str+=";";
      break;
    case 95:  // Ответ состояния будильника - сообщение по инициативе сервера
      str = "$18 AL:"; 
      if ((isAlarming || isPlayAlarmSound) && !isAlarmStopped) str+="1;"; else str+="0;";
      cmd95 = str;
      break;
    case 96:  // Ответ демо-режима звука - сообщение по инициативе сервера
      str ="$18 MP:" + String(soundFolder) + '~' + String(soundFile+2) + ";"; 
      cmd96 = str;
      break;
    case 99:  // Запрос списка эффектов
      str="$18 LE:[" + String(EFFECT_LIST) + "];"; 
      break;
  }
  
  if (str.length() > 0) {
    // Отправить клиенту запрошенные параметры страницы / режимов
    str.toCharArray(incomeBuffer, str.length()+1);    
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write((const uint8_t*) incomeBuffer, str.length()+1);
    udp.endPacket();
    delay(0);
    Serial.println(String(F("Ответ на ")) + udp.remoteIP().toString() + ":" + String(udp.remotePort()) + " >> " + String(incomeBuffer));
  } else {
    sendAcknowledge();
  }
}

void sendAcknowledge() {
  // Отправить подтверждение, чтобы клиентский сокет прервал ожидание
  String reply = "";
  bool isCmd = false; 
  if (cmd95.length() > 0) { reply += cmd95; cmd95 = ""; isCmd = true;}
  if (cmd96.length() > 0) { reply += cmd96; cmd96 = ""; isCmd = true; }
  reply += "ack" + String(ackCounter++) + ";";  
  reply.toCharArray(replyBuffer, reply.length()+1);
  udp.beginPacket(udp.remoteIP(), udp.remotePort());
  udp.write((const uint8_t*) replyBuffer, reply.length()+1);
  udp.endPacket();
  delay(0);
  if (isCmd) {
    Serial.println(String(F("Ответ на ")) + udp.remoteIP().toString() + ":" + String(udp.remotePort()) + " >> " + String(replyBuffer));
  }
}

void setSpecialMode(int spc_mode) {
        
  AUTOPLAY = false;
  BTcontrol = true;
  loadingFlag = true;
  isTurnedOff = false;
  isNightClock = false;
  specialModeId = -1;

  String str;
  byte tmp_eff = -1;
  specialBrightness = globalBrightness;
  specialClock = getClockOverlayEnabled();

  switch(spc_mode) {
    case 0:  // Черный экран (выкл);
      tmp_eff = MC_FILL_COLOR;
      globalColor = 0x000000;
      specialBrightness = 0;
      specialClock = false;
      isTurnedOff = true;
      break;
    case 1:  // Белый экран (освещение);
      tmp_eff = MC_FILL_COLOR;
      globalColor = 0xffffff;
      specialBrightness = 255;
      break;
    case 2:  // Лампа указанного цвета;
      tmp_eff = MC_FILL_COLOR;
      globalColor = getGlobalColor();
      break;
    case 3:  // Огонь;
      tmp_eff = MC_FIRE;
      break;
    case 4:  // Пейнтбол;
      tmp_eff = MC_PAINTBALL;
      break;
    case 5:  // Радуга;
      tmp_eff = MC_RAINBOW_DIAG;
      break;
    case 6:  // Матрица;
      tmp_eff = MC_MATRIX;
      break;
    case 7:  // Светлячки;
      tmp_eff = MC_LIGHTERS;
      break;
    case 8:  // Ночные часы;  
      tmp_eff = MC_CLOCK;
      specialClock = false;
      isNightClock = true;
      specialBrightness = 255;
      break;
    case 9:  // Часы бегущей строкой;
      tmp_eff = MC_TEXT;
      specialClock = false;
      setGlobalColor(getGlobalTextColor());
      break;
    case 10:  // Часы;  
      tmp_eff = MC_CLOCK;
      setGlobalColor(getGlobalClockColor());
      specialClock = true;
      break;
  }

  if (tmp_eff >= 0) {    
    // Дльнейшее отображение изображения эффекта будет выполняться стандартной процедурой customRoutine()
    thisMode = tmp_eff;
    specialMode = true;
    setTimersForMode(thisMode);
    // Таймер возврата в авторежим отключен    
    idleTimer.setInterval(4294967295);
    idleTimer.reset();
    FastLED.setBrightness(specialBrightness);
    specialModeId = spc_mode;
  }  
  
  setCurrentSpecMode(spc_mode);
  setCurrentManualMode(-1);
}

void resetModes() {
  // Отключение спец-режима перед включением других
  specialMode = false;
  isTurnedOff = false;
  isNightClock = false;
  specialModeId = -1;
  loadingFlag = false;
  wifi_print_ip = false;
  wifi_print_ip_text = false;
}

void setEffect(byte eff) {

  resetModes();
  loadingFlag = true;
  thisMode = eff;

  setTimersForMode(thisMode);  

  setCurrentSpecMode(-1);
  if (!AUTOPLAY)
    setCurrentManualMode(thisMode);

  if (thisMode != MC_DAWN_ALARM)
    FastLED.setBrightness(globalBrightness);      
}

void showCurrentIP() {
  setEffect(MC_TEXT);
  BTcontrol = false;
  AUTOPLAY = true;
  wifi_print_ip = true;
  wifi_print_ip_text = true;
  wifi_print_idx = 0; 
  wifi_current_ip = wifi_connected ? WiFi.localIP().toString() : String(F("Нет подключения к сети WiFi"));
}

void setRandomMode() {
    String s_tmp = String(EFFECT_LIST);    
    uint32_t cnt = CountTokens(s_tmp, ','); 
    byte ef = random(0, cnt - 1); 
    setEffect(ef);
}

void setRandomMode2() {
  byte cnt = 0;
  while (cnt < 10) {
    cnt++;
    byte newMode = random(0, MAX_EFFECT - 1);
    if (!getEffectUsage(newMode)) continue;

    setEffect(newMode);
    break;
  }
  
  if (cnt >= 10) setEffect(0);
}
