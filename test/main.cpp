// Реализация заглушки + тесты логики прошивки FuckBot_SNO.
//
//   make -C test        (или: g++ -std=c++17 -I mock -o run_tests main.cpp && ./run_tests)

#include "Arduino.h"

uint32_t g_millis = 0;
MockSerial Serial;

static int g_digital[32];
static int g_pwm[32];

void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, uint8_t value) { if (pin < 32) g_digital[pin] = value; }
void analogWrite(uint8_t pin, int value) { if (pin < 32) g_pwm[pin] = value; }
int digitalReadPin(uint8_t pin) { return pin < 32 ? g_digital[pin] : 0; }
int analogReadPin(uint8_t pin) { return pin < 32 ? g_pwm[pin] : 0; }

#include "../firmware/FuckBot_SNO/FuckBot_SNO.ino"

// ---------------------------------------------------------------------------

static int g_failed = 0;
static int g_passed = 0;

static void check(bool ok, const char *what) {
  if (ok) { g_passed++; return; }
  g_failed++;
  printf("  FAIL: %s\n", what);
}

static void resetBot() {
  Serial.rx.clear();
  Serial.pos = 0;
  g_millis = 1000;
  setup();
}

// Прокрутить loop() n раз по 20 мс
static void tick(int n) {
  for (int i = 0; i < n; i++) {
    g_millis += 20;
    loop();
  }
}

// Имитация приложения: кадр повторяется каждые 20 мс (как живой джойстик).
// 20 тиков = 400 мс < FAILSAFE_MS, поэтому связь не считается потерянной.
static void send(const char *frame, int ticks = 20) {
  for (int i = 0; i < ticks; i++) {
    Serial.feed(frame);
    g_millis += 20;
    loop();
  }
}

// ---- состояние моторов ----
static int pwmL() { return g_pwm[PIN_L_EN]; }
static int pwmR() { return g_pwm[PIN_R_EN]; }

// направление: +1 вперёд, -1 назад, 0 стоп
static int dirL() {
  if (g_digital[PIN_L_IN1] == HIGH && g_digital[PIN_L_IN2] == LOW) return 1;
  if (g_digital[PIN_L_IN1] == LOW && g_digital[PIN_L_IN2] == HIGH) return -1;
  return 0;
}
static int dirR() {
  if (g_digital[PIN_R_IN1] == HIGH && g_digital[PIN_R_IN2] == LOW) return 1;
  if (g_digital[PIN_R_IN1] == LOW && g_digital[PIN_R_IN2] == HIGH) return -1;
  return 0;
}

static bool near(int a, int b, int tol = 2) { return (a > b ? a - b : b - a) <= tol; }

// ---------------------------------------------------------------------------

static void testStop() {
  printf("[стоп]\n");
  resetBot();
  send("0,0\n");
  check(pwmL() == 0 && pwmR() == 0, "PWM должен быть 0");
  check(dirL() == 0 && dirR() == 0, "оба мотора стоят (накат)");
}

static void testForward() {
  printf("[полный вперёд]\n");
  resetBot();
  send("100,0\n");
  check(dirL() == 1 && dirR() == 1, "оба борта вперёд");
  check(near(pwmL(), MAX_PWM, 3), "левый на максимуме");
  check(near(pwmR(), MAX_PWM, 3), "правый на максимуме");
}

static void testBackward() {
  printf("[полный назад]\n");
  resetBot();
  send("-100,0\n");
  check(dirL() == -1 && dirR() == -1, "оба борта назад");
  check(near(pwmL(), MAX_PWM, 3), "левый на максимуме");
}

static void testSpinRight() {
  printf("[разворот вправо на месте]\n");
  resetBot();
  send("0,100\n");
  check(dirL() == 1, "левый вперёд");
  check(dirR() == -1, "правый назад");
  check(near(pwmL(), MAX_PWM, 3) && near(pwmR(), MAX_PWM, 3), "оба на максимуме");
}

static void testSpinLeft() {
  printf("[разворот влево на месте]\n");
  resetBot();
  send("0,-100\n");
  check(dirL() == -1, "левый назад");
  check(dirR() == 1, "правый вперёд");
}

static void testMixNormalized() {
  printf("[нормализация дуги 100,100]\n");
  resetBot();
  send("100,100\n");
  // l = 200 -> 100, r = 0 -> 0
  check(near(pwmL(), MAX_PWM, 3), "левый на максимуме");
  check(pwmR() == 0, "правый стоит (радиус поворота)");
}

static void testHalfSpeed() {
  printf("[половина вперёд 50,0]\n");
  resetBot();
  send("50,0\n");
  int expect = MIN_PWM + (int)((50L * (MAX_PWM - MIN_PWM) + 50) / 100);
  check(near(pwmL(), expect, 3), "PWM соответствует 50%");
  check(pwmL() < MAX_PWM && pwmL() > MIN_PWM, "PWM между MIN и MAX");
  check(dirL() == 1 && dirR() == 1, "направление вперёд");
}

static void testRamp() {
  printf("[плавный разгон]\n");
  resetBot();
  Serial.feed("100,0\n");
  g_millis += 20;
  loop();
  int pwmAfterOneTick = pwmL();
  check(pwmAfterOneTick < MAX_PWM, "за один тик скорость не достигает максимума");
  check(pwmAfterOneTick >= MIN_PWM, "но мотор уже крутится");
  send("100,0\n", 12);
  check(near(pwmL(), MAX_PWM, 3), "через ~250 мс выходит на максимум");
}

static void testDeadband() {
  printf("[мёртвая зона стика]\n");
  resetBot();
  send("2,-3\n");
  check(pwmL() == 0 && pwmR() == 0, "дрожание нуля не крутит моторы");
}

static void testClamp() {
  printf("[обрезка значений]\n");
  resetBot();
  send("1000,0\n");
  check(dirL() == 1 && dirR() == 1, "1000 трактуется как 100 (вперёд)");
  check(near(pwmL(), MAX_PWM, 3) && near(pwmR(), MAX_PWM, 3), "скорость обрезана до 100%");

  resetBot();
  send("0,-1000\n");
  check(dirL() == -1 && dirR() == 1, "-1000 трактуется как -100 (разворот влево)");
}

static void testSeparators() {
  printf("[разные разделители и форматы]\n");
  // y=40, x=10 -> борта 50 и 30, оба вперёд
  const char *frames[] = {"40;10\n", "40 10\r", "40:10\n", " 40 , 10 \n", "y=40,x=10\n"};
  for (const char *f : frames) {
    resetBot();
    send(f);
    check(dirL() == 1 && dirR() == 1, f);
    check(pwmL() > pwmR() && pwmR() > 0, f);
  }
}

static void testSingleValue() {
  printf("[одно число = Y, X=0]\n");
  resetBot();
  send("60\n");
  check(dirL() == 1 && dirR() == 1, "едет вперёд");
  check(near(pwmL(), pwmR(), 1), "борта одинаково");
}

static void testGarbageIgnored() {
  printf("[мусор между кадрами]\n");
  resetBot();
  send("100,0\n", 20);
  int before = pwmL();
  send("hello world\n", 3);
  check(near(pwmL(), before, 3), "мусорная строка не меняет команду");
  check(dirL() == 1, "направление сохранено");
}

static void testFailsafe() {
  printf("[аварийный стоп при потере связи]\n");
  resetBot();
  send("100,0\n");
  check(pwmL() > 0, "сначала едет");
  g_millis += FAILSAFE_MS + 100;   // данные перестали приходить
  loop();
  check(pwmL() == 0 && pwmR() == 0, "моторы мгновенно обесточены");
  check(dirL() == 0 && dirR() == 0, "накат");
  check(g_digital[PIN_LED] == LOW, "LED погас");
}

static void testLedOnLink() {
  printf("[LED при живой связи]\n");
  resetBot();
  send("0,0\n");
  check(g_digital[PIN_LED] == HIGH, "LED горит при приёме кадров");
}

static void testSplitFrame() {
  printf("[кадр приходит по частям]\n");
  resetBot();
  Serial.feed("0,0\n");          // держим связь
  g_millis += 20; loop();
  Serial.feed("77");
  g_millis += 20; loop();
  Serial.feed(",");
  g_millis += 20; loop();
  Serial.feed("-12\n");
  send("77,-12\n", 20);
  check(dirL() == 1 && dirR() == 1, "кадр собран из кусков");
  check(pwmL() > 0, "скорость применена");
}

static void testRxOverflow() {
  printf("[переполнение приёмного буфера]\n");
  resetBot();
  for (int i = 0; i < 100; i++) Serial.feed("9999");
  tick(3);
  send("\n0,0\n", 20);
  check(pwmL() == 0 && pwmR() == 0, "мусор отброшен, следующая команда работает");
}

static void testPairwiseReverse() {
  printf("[реверс с плавным переходом через ноль]\n");
  resetBot();
  send("100,0\n", 30);
  int minPwm = 1000;
  for (int i = 0; i < 20; i++) {
    Serial.feed("-100,0\n");
    g_millis += 20;
    loop();
    if (pwmL() < minPwm) minPwm = pwmL();
  }
  check(minPwm <= MIN_PWM + 10, "при реверсе мотор проходит почти через полный стоп");
  check(pwmL() == MAX_PWM && dirL() == -1, "в итоге едет назад на полной");
}

int main() {
  testStop();
  testForward();
  testBackward();
  testSpinRight();
  testSpinLeft();
  testMixNormalized();
  testHalfSpeed();
  testRamp();
  testDeadband();
  testClamp();
  testSeparators();
  testSingleValue();
  testGarbageIgnored();
  testFailsafe();
  testLedOnLink();
  testSplitFrame();
  testRxOverflow();
  testPairwiseReverse();

  printf("\n%d проверок пройдено, %d провалено\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
