#!/usr/bin/env bash
#
# Сборка APK приложения FuckBot RC БЕЗ Android Studio.
#
# Скрипт сам ставит всё, что нужно:
#   1) проверяет JDK 17+;
#   2) скачивает Android command-line tools и нужные компоненты SDK;
#   3) берёт Gradle (wrapper / системный / скачивает 8.7);
#   4) собирает debug APK.
#
# Требуется: bash, curl, unzip. Linux, macOS или Windows в WSL/Git Bash.
# Первый запуск скачивает ~700 МБ и занимает 10–20 минут, дальше — быстро.
#
# Использование:
#   bash tools/build_apk.sh
#
# Готовый файл: android/app/build/outputs/apk/debug/app-debug.apk

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_DIR="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/android-sdk}}"
CMDLINE_TOOLS_BUILD="11076708"
GRADLE_VERSION="8.7"
TOOLS_DIR="$ROOT/.build-tools"

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nОШИБКА: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- 1. Java ---
command -v java >/dev/null 2>&1 || die "не найден java. Установите JDK 17 (Temurin: https://adoptium.net) и повторите"

JAVA_VERSION_RAW="$(java -version 2>&1 | head -n 1)"
JAVA_MAJOR="$(printf '%s' "$JAVA_VERSION_RAW" | sed -E 's/.*version "([0-9]+)(\.([0-9]+))?.*/\1/')"
# Java 8 и старше рапортуются как "1.8.0_xxx" — приводим к нормальному номеру
if [ "$JAVA_MAJOR" = "1" ]; then
  JAVA_MAJOR="$(printf '%s' "$JAVA_VERSION_RAW" | sed -E 's/.*version "1\.([0-9]+).*/\1/')"
fi
case "$JAVA_MAJOR" in
  ''|*[!0-9]*) die "не удалось определить версию Java: $JAVA_VERSION_RAW" ;;
esac
[ "$JAVA_MAJOR" -ge 17 ] || die "нужен JDK 17 или новее, найден $JAVA_MAJOR ($JAVA_VERSION_RAW)"

log "Java $JAVA_MAJOR — подходит"

# ------------------------------------------------------- 2. Android SDK ----
case "$(uname -s)" in
  Linux*)  SDK_OS="linux" ;;
  Darwin*) SDK_OS="mac" ;;
  *)       die "неизвестная ОС: $(uname -s). Используйте Linux, macOS, WSL или GitHub Actions" ;;
esac

if [ ! -x "$SDK_DIR/cmdline-tools/latest/bin/sdkmanager" ]; then
  command -v unzip >/dev/null 2>&1 || die "не найден unzip — установите его (apt install unzip / brew install unzip)"
  command -v curl  >/dev/null 2>&1 || die "не найден curl"

  URL="https://dl.google.com/android/repository/commandlinetools-${SDK_OS}-${CMDLINE_TOOLS_BUILD}_latest.zip"
  log "Скачиваю Android command-line tools (~150 МБ)"
  TMP_ZIP="$(mktemp)"
  curl -fL --retry 3 --progress-bar -o "$TMP_ZIP" "$URL" || die "не удалось скачать $URL"
  mkdir -p "$SDK_DIR/cmdline-tools"
  rm -rf "$SDK_DIR/cmdline-tools/latest.part"
  mkdir -p "$SDK_DIR/cmdline-tools/latest.part"
  unzip -q "$TMP_ZIP" -d "$SDK_DIR/cmdline-tools/latest.part" || die "архив повреждён"
  mv "$SDK_DIR/cmdline-tools/latest.part/cmdline-tools" "$SDK_DIR/cmdline-tools/latest"
  rm -rf "$SDK_DIR/cmdline-tools/latest.part" "$TMP_ZIP"
  log "SDK tools установлены в $SDK_DIR"
fi

export ANDROID_SDK_ROOT="$SDK_DIR"
export ANDROID_HOME="$SDK_DIR"
SDKMANAGER="$SDK_DIR/cmdline-tools/latest/bin/sdkmanager"

log "Принимаю лицензии SDK"
yes | "$SDKMANAGER" --licenses >/dev/null 2>&1 || true

log "Ставлю platform-tools, platforms;android-34, build-tools;34.0.0"
"$SDKMANAGER" --install "platform-tools" "platforms;android-34" "build-tools;34.0.0" >/dev/null \
  || die "sdkmanager не смог поставить компоненты"

# ------------------------------------------------------------ 3. Gradle ----
if [ -x "$ROOT/android/gradlew" ]; then
  GRADLE_CMD="$ROOT/android/gradlew"
  log "Использую gradle wrapper проекта"
elif command -v gradle >/dev/null 2>&1; then
  GRADLE_CMD="$(command -v gradle)"
  log "Использую системный Gradle: $("$GRADLE_CMD" --version | awk '/^Gradle /{print $2; exit}')"
else
  GRADLE_HOME="$TOOLS_DIR/gradle-$GRADLE_VERSION"
  if [ ! -x "$GRADLE_HOME/bin/gradle" ]; then
    log "Скачиваю Gradle $GRADLE_VERSION (~130 МБ)"
    mkdir -p "$TOOLS_DIR"
    curl -fL --retry 3 --progress-bar -o "$TOOLS_DIR/gradle.zip" \
      "https://services.gradle.org/distributions/gradle-${GRADLE_VERSION}-bin.zip" \
      || die "не удалось скачать Gradle"
    unzip -q -o "$TOOLS_DIR/gradle.zip" -d "$TOOLS_DIR" || die "архив Gradle повреждён"
    rm -f "$TOOLS_DIR/gradle.zip"
  fi
  GRADLE_CMD="$GRADLE_HOME/bin/gradle"
  log "Gradle $GRADLE_VERSION распакован в $TOOLS_DIR"
fi

# ------------------------------------------------------------- 4. Сборка ---
log "Собираю APK (первый раз Gradle тянет зависимости AGP — это долго)"
cd "$ROOT/android"
"$GRADLE_CMD" :app:assembleDebug --no-daemon

APK="$ROOT/android/app/build/outputs/apk/debug/app-debug.apk"
[ -f "$APK" ] || die "сборка прошла, но APK не найден: $APK"

SIZE="$(du -h "$APK" | cut -f1)"
log "Готово: $APK ($SIZE)"
printf '\nЭтот APK подписан debug-ключом — для установки на телефон включите\n'
printf '"Установка из неизвестных источников" для файлового менеджера.\n\n'
