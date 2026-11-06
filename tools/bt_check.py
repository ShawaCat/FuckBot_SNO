#!/usr/bin/env python3
"""
Проверка робота по Bluetooth без телефона.

Нужен HC-06, спаренный с ПК и подключённый как COM-порт:
  Windows : "Исходящие" -> COM5 (смотреть в Диспетчере устройств)
  Linux   : sudo rfcomm bind 0 AA:BB:CC:DD:EE:FF   -> /dev/rfcomm0
  macOS   : /dev/tty.HC-06-DevB

Установка зависимости:  pip install pyserial

Использование:
  python3 bt_check.py COM5            # интерактивно, стрелки/WASD
  python3 bt_check.py /dev/rfcomm0 --demo   # автопрогон всех режимов
"""

import sys
import time

try:
    import serial
except ImportError:
    sys.exit("Нет pyserial: pip install pyserial")

BAUD = 9600
HZ = 20  # частота отправки кадров


def open_port(name: str) -> "serial.Serial":
    port = serial.Serial(name, BAUD, timeout=0)
    time.sleep(0.2)
    return port


def send(port, y: int, x: int) -> None:
    port.write(f"{max(-100, min(100, y))},{max(-100, min(100, x))}\n".encode("ascii"))


def demo(port) -> None:
    """Автопрогон: вперёд, дуга, развороты, стоп."""
    steps = [
        ("вперёд 50%",      50,   0, 1.5),
        ("вперёд 100%",    100,   0, 1.5),
        ("стоп",             0,   0, 0.5),
        ("дуга вправо",     60,  40, 2.0),
        ("дуга влево",      60, -40, 2.0),
        ("разворот вправо",  0, 100, 1.5),
        ("разворот влево",   0, -100, 1.5),
        ("назад 60%",      -60,   0, 1.5),
        ("стоп",             0,   0, 0.5),
    ]
    for name, y, x, dur in steps:
        print(f"  {name:<16} y={y:>4} x={x:>4}")
        t_end = time.time() + dur
        while time.time() < t_end:
            send(port, y, x)
            time.sleep(1.0 / HZ)
    print("  готово")


def interactive(port) -> None:
    """Стрелки или WASD: скорость набирается шагами, пробел — стоп, q — выход."""
    import termios
    import tty

    def read_key() -> str:
        ch = sys.stdin.read(1)
        if ch == "\x1b":  # стрелки приходят как ESC [ A/B/C/D
            seq = sys.stdin.read(2)
            return {"[A": "up", "[B": "down", "[C": "right", "[D": "left"}.get(seq, "")
        return ch

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    y = x = 0
    step = 25
    print("Стрелки/WASD — ехать, пробел — стоп, q — выход")
    try:
        tty.setraw(fd)
        while True:
            key = read_key()
            if key == "q":
                break
            if key in ("up", "w"):
                y = min(100, y + step)
            elif key in ("down", "s"):
                y = max(-100, y - step)
            elif key in ("right", "d"):
                x = min(100, x + step)
            elif key in ("left", "a"):
                x = max(-100, x - step)
            elif key == " ":
                y = x = 0
            send(port, y, x)
            print(f"\r y={y:>4} x={x:>4}   ", end="", flush=True)
    finally:
        for _ in range(3):  # перед выходом обязательно стоп
            send(port, 0, 0)
            time.sleep(0.05)
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        print("\nстоп, отключено")


def main() -> None:
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    port_name = sys.argv[1]
    mode = sys.argv[2] if len(sys.argv) > 2 else ""
    port = open_port(port_name)
    try:
        print(f"Открыт {port_name} @ {BAUD} бод")
        if mode == "--demo":
            demo(port)
        else:
            interactive(port)
    except KeyboardInterrupt:
        pass
    finally:
        for _ in range(3):
            send(port, 0, 0)
            time.sleep(0.05)
        port.close()


if __name__ == "__main__":
    main()
