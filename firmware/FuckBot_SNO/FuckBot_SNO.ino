/*
 * ============================================================================
 *  FuckBot SNO — прошивка робота с танковым управлением
 *  Железо: Arduino Uno + HC-06 (Bluetooth SPP) + L298N (два моста)
 * ----------------------------------------------------------------------------
 *  ПРОТОКОЛ (текст, ASCII, UART 9600 8N1 — заводские настройки HC-06):
 *
 *      "<Y>,<X>\n"      Y = -100..100   вперёд (+) / назад (-)
 *                       X = -100..100   вправо (+) / влево (-)
 *
 *  Примеры:  "0,0\n"     — стоп
 *            "100,0\n"   — полный вперёд
 *            "0,-70\n"   — разворот влево на месте
 *            "-40,20\n"  — назад с подворотом вправо
 *
 *  Разделителем чисел может быть любой нецифровой символ (",", ";", " ", ":"
 *  и т.п.), в конце строки — "\n" или "\r". Если прислать одно число, оно
 *  трактуется как Y, а X = 0. Значения вне диапазона обрезаются до -100..100.
 *
 *  ВАЖНО: кадр ОБЯЗАТЕЛЬНО должен заканчиваться переводом строки ('\n'),
 *  иначе числа склеятся. Если ваше готовое приложение шлёт другой формат
 *  (одиночные буквы F/B/L/R, байты и т.п.) — правьте parseFrame() ниже,
 *  вся остальная логика от формата не зависит.
 *
 *  БЕЗОПАСНОСТЬ: если 600 мс не пришло ни одного корректного кадра (обрыв
 *  связи, телефон уснул, приложение свернулось) — моторы обесточиваются.
 * ============================================================================
 */

#include <Arduino.h>
#include <stdlib.h>   // strtol()

// ============================================================================
//                            НАСТРОЙКИ
// ============================================================================

// ----------------------------- Пины Arduino --------------------------------
// ENA/ENB L298N должны идти на пины с поддержкой PWM: 3, 5, 6, 9, 10, 11.
static const uint8_t PIN_L_EN  = 5;    // ENA — ШИМ левого борта
static const uint8_t PIN_L_IN1 = 7;    // IN1
static const uint8_t PIN_L_IN2 = 8;    // IN2
static const uint8_t PIN_R_EN  = 6;    // ENB — ШИМ правого борта
static const uint8_t PIN_R_IN1 = 9;    // IN3
static const uint8_t PIN_R_IN2 = 10;   // IN4

static const uint8_t PIN_LED  = 13;    // статус: горит = команды идут

// ----------------------------- Связь ---------------------------------------
static const long     SERIAL_BAUD      = 9600;  // HC-06 из коробки: 9600 8N1
static const uint16_t FAILSAFE_MS      = 600;   // нет кадров -> аварийный стоп
                                                // 0 = отключить (см. README)
static const uint8_t  RX_BUF_SIZE      = 32;    // макс. длина кадра

// ----------------------------- Ходовая -------------------------------------
static const int16_t  INPUT_DEADBAND   = 4;     // мёртвая зона стика, %
static const int16_t  MIN_PWM          = 30;    // "трогание": ниже мотор гудит,
                                                // но не крутится (у каждого свой)
static const int16_t  MAX_PWM          = 255;   // ограничитель макс. скорости
static const int16_t  SPEED_LIMIT      = 100;   // общий лимит скорости, %
static const int16_t  ACCEL_PCT_PER_S  = 700;   // плавный разгон, %/с (0 = резко)
static const bool     NORMALIZE_MIX    = true;  // не "срезать" скорость в повороте
static const bool     BRAKE_ON_STOP    = false; // true = тормозить, false = накат

// ----------------------------- Инверсии (если робот едет не туда) -----------
static const bool     INVERT_Y        = false;  // Y: инвертировать вперёд/назад
static const bool     INVERT_X        = false;  // X: инвертировать вправо/влево
static const bool     INVERT_LEFT     = false;  // левый мотор крутится наоборот
static const bool     INVERT_RIGHT    = false;  // правый мотор крутится наоборот
static const bool     SWAP_SIDES      = false;  // поменять борта местами
                                                // (если робот едет "крабом")

// ----------------------------- Тайминги ------------------------------------
static const uint16_t CONTROL_PERIOD_MS = 20;   // период регулятора, мс

// ============================================================================
//                        ВНУТРЕННИЕ ПЕРЕМЕННЫЕ
// ============================================================================

// принятый кадр -> желаемые значения со стика
static int16_t reqY = 0;
static int16_t reqX = 0;

// текущая (сглаженная) скорость бортов, % -100..100
static int16_t curL = 0;
static int16_t curR = 0;

static uint32_t lastCommandMs = 0;     // время последнего корректного кадра
static uint32_t lastTickMs    = 0;     // время последнего шага регулятора

static char    rxBuf[RX_BUF_SIZE];
static uint8_t rxLen = 0;

// ============================================================================
//                              УТИЛИТЫ
// ============================================================================

static inline int16_t clampI16(int32_t v, int16_t lo, int16_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return (int16_t)v;
}

static inline int16_t iabs16(int16_t v) { return (v < 0) ? (int16_t)-v : v; }

// Плавно подтягиваем cur к target не быстрее maxStep
static inline int16_t approach(int16_t cur, int16_t target, int16_t maxStep) {
  if (cur < target) {
    int16_t v = cur + maxStep;
    return (v > target) ? target : v;
  }
  if (cur > target) {
    int16_t v = cur - maxStep;
    return (v < target) ? target : v;
  }
  return cur;
}

// ============================================================================
//                          РАЗБОР ПРИНЯТОЙ СТРОКИ
// ============================================================================

// Вытаскивает до maxCount целых чисел из строки, игнорируя все разделители.
// Возвращает количество найденных чисел.
static uint8_t extractNumbers(const char *s, long *out, uint8_t maxCount) {
  uint8_t n = 0;
  const char *p = s;

  while (*p != '\0' && n < maxCount) {
    // пропускаем всё, что не может начинать число
    if (!(*p >= '0' && *p <= '9') && *p != '-' && *p != '+') {
      p++;
      continue;
    }
    char *end = nullptr;
    long v = strtol(p, &end, 10);
    if (end == p) {          // знак без цифр ("-,") — пропускаем символ
      p++;
      continue;
    }
    out[n++] = v;
    p = end;
  }
  return n;
}

// Разбирает кадр. true = данных достаточно, reqY/reqX обновлены.
static bool parseFrame(const char *line) {
  long vals[4];
  uint8_t n = extractNumbers(line, vals, 4);
  if (n == 0) return false;                 // мусорная строка — игнорируем

  long y = vals[0];
  long x = (n >= 2) ? vals[1] : 0;

  reqY = clampI16(y, -100, 100);
  reqX = clampI16(x, -100, 100);
  return true;
}

// ============================================================================
//                            ПРИЁМ ПО UART
// ============================================================================

static void handleLine() {
  rxBuf[rxLen] = '\0';
  if (rxLen > 0) {
    if (parseFrame(rxBuf)) {
      lastCommandMs = millis();
    }
  }
  rxLen = 0;
}

static void readBluetooth() {
  uint8_t budget = 128;                    // не залипаем в приёме
  while (budget-- > 0) {
    int c = Serial.read();
    if (c < 0) break;

    if (c == '\n' || c == '\r') {
      handleLine();
      continue;
    }
    if (rxLen < RX_BUF_SIZE - 1) {
      rxBuf[rxLen++] = (char)c;
    } else {
      rxLen = 0;                           // переполнение — кадр в мусор
    }
  }
}

// ============================================================================
//                              МИКШЕР
// ============================================================================

// Танковая схема: стик (Y,X) -> скорости бортов (-100..100).
static void mixTank(int16_t y, int16_t x, int16_t &outL, int16_t &outR) {
  int32_t l = (int32_t)y + x;
  int32_t r = (int32_t)y - x;

  if (NORMALIZE_MIX) {
    // Если при повороте на полном ходу сумма вылезает за 100 —
    // масштабируем оба борта, чтобы робот не терял скорость дуги.
    int32_t m = l < 0 ? -l : l;
    int32_t rm = r < 0 ? -r : r;
    if (rm > m) m = rm;
    if (m > 100) {
      l = l * 100 / m;
      r = r * 100 / m;
    }
  }

  outL = clampI16(l, -100, 100);
  outR = clampI16(r, -100, 100);
}

// ============================================================================
//                          ВЫВОД НА L298N
// ============================================================================

static inline int16_t mapPercentToPwm(int16_t percent);

// speed: -100..100 (знак = направление), invert — реверс борта
static void applyMotor(uint8_t pinEN, uint8_t pinIN1, uint8_t pinIN2,
                       int16_t speed, bool invert) {
  if (invert) speed = -speed;

  uint8_t pwm = 0;

  if (speed > 0) {
    digitalWrite(pinIN1, HIGH);
    digitalWrite(pinIN2, LOW);
    pwm = (uint8_t)mapPercentToPwm(speed);
  } else if (speed < 0) {
    digitalWrite(pinIN1, LOW);
    digitalWrite(pinIN2, HIGH);
    pwm = (uint8_t)mapPercentToPwm((int16_t)-speed);
  } else {
    // Стоп: либо свободный накат (оба LOW), либо электрический тормоз
    if (BRAKE_ON_STOP) {
      digitalWrite(pinIN1, HIGH);
      digitalWrite(pinIN2, HIGH);
      pwm = 255;
    } else {
      digitalWrite(pinIN1, LOW);
      digitalWrite(pinIN2, LOW);
      pwm = 0;
    }
  }

  analogWrite(pinEN, pwm);
}

// 1..100 % -> MIN_PWM..MAX_PWM (чтобы мотор не "гудел" на малых значениях)
static inline int16_t mapPercentToPwm(int16_t percent) {
  if (percent <= 0) return 0;
  if (percent > 100) percent = 100;

  int16_t minPwm = MIN_PWM;
  if (minPwm < 0) minPwm = 0;
  int16_t maxPwm = MAX_PWM;
  if (maxPwm > 255) maxPwm = 255;
  if (maxPwm < minPwm) maxPwm = minPwm;

  int32_t pwm = (int32_t)minPwm + ((int32_t)percent * (maxPwm - minPwm) + 50) / 100;
  if (pwm > 255) pwm = 255;
  return (int16_t)pwm;
}

static void driveMotors(int16_t left, int16_t right) {
  if (SWAP_SIDES) {
    int16_t t = left; left = right; right = t;
  }
  applyMotor(PIN_L_EN, PIN_L_IN1, PIN_L_IN2, left,  INVERT_LEFT);
  applyMotor(PIN_R_EN, PIN_R_IN1, PIN_R_IN2, right, INVERT_RIGHT);
}

// ============================================================================
//                                SETUP
// ============================================================================

void setup() {
  pinMode(PIN_L_EN,  OUTPUT);
  pinMode(PIN_L_IN1, OUTPUT);
  pinMode(PIN_L_IN2, OUTPUT);
  pinMode(PIN_R_EN,  OUTPUT);
  pinMode(PIN_R_IN1, OUTPUT);
  pinMode(PIN_R_IN2, OUTPUT);
  pinMode(PIN_LED,   OUTPUT);

  driveMotors(0, 0);

  Serial.begin(SERIAL_BAUD);          // UART0 -> HC-06
  Serial.setTimeout(20);

  // Три быстрых "моргания" — плата жива
  for (uint8_t i = 0; i < 3; i++) {
    digitalWrite(PIN_LED, HIGH); delay(60);
    digitalWrite(PIN_LED, LOW);  delay(90);
  }

  uint32_t now = millis();
  lastCommandMs = now;                // даём 600 мс на подключение
  lastTickMs    = now;
}

// ============================================================================
//                                 LOOP
// ============================================================================

void loop() {
  readBluetooth();                    // 1) забираем байты из UART

  uint32_t now = millis();

  // 2) регулятор с фиксированным периодом
  if (now - lastTickMs < CONTROL_PERIOD_MS) return;
  uint16_t dtMs = (uint16_t)(now - lastTickMs);
  lastTickMs = now;

  // FAILSAFE_MS == 0 -> защита отключена (нужно, если приложение шлёт кадры
  // только в момент изменения положения стика)
  bool linkOk = (FAILSAFE_MS == 0) ? true
                                   : ((now - lastCommandMs) < FAILSAFE_MS);

  int16_t targetL = 0;
  int16_t targetR = 0;

  if (linkOk) {
    int16_t y = reqY;
    int16_t x = reqX;

    if (INVERT_Y) y = (int16_t)-y;
    if (INVERT_X) x = (int16_t)-x;

    // мёртвая зона стика — убирает "дрожание" нуля
    if (iabs16(y) < INPUT_DEADBAND) y = 0;
    if (iabs16(x) < INPUT_DEADBAND) x = 0;

    mixTank(y, x, targetL, targetR);
    targetL = clampI16(targetL, -SPEED_LIMIT, SPEED_LIMIT);
    targetR = clampI16(targetR, -SPEED_LIMIT, SPEED_LIMIT);
  } else {
    // связи нет — мгновенный стоп, без разгона
    curL = 0;
    curR = 0;
  }

  // 3) сглаживание (мягкий пуск/реверс — бережёт редукторы и батарею)
  int16_t step = 1000;
  if (ACCEL_PCT_PER_S > 0) {
    int32_t s = ((int32_t)ACCEL_PCT_PER_S * dtMs) / 1000;
    if (s < 1) s = 1;
    step = (int16_t)((s > 1000) ? 1000 : s);
  }
  curL = approach(curL, targetL, step);
  curR = approach(curR, targetR, step);

  // 4) на моторы + индикация
  driveMotors(curL, curR);
  digitalWrite(PIN_LED, linkOk ? HIGH : LOW);
}
