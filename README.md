# FreeRTOS Fan — ATmega128

A desk-fan controller with a countdown timer, built on **FreeRTOS Kernel V11.1.0** for the
**ATmega128** (Atmel Studio 7 / avr-gcc).

* PWM fan with 9 speed levels (0–8)
* Countdown timer in minutes (0–9999); fan stops and an alarm blinks when it reaches 0
* 4‑digit 7‑segment display, 16×2 character LCD, 8‑LED bar, 4 push buttons, buzzer

---

## 1. Behaviour

| | |
|---|---|
| **BASIC mode** | Adjust fan speed. If a timer is armed it counts down in the background. |
| **TIMER_SET mode** | Edit the timer value (minutes). |
| **ALARM** | Countdown hit 0 → fan off, FND shows `----` and the LED bar blinks. Dismiss with any key. |

### Controls

| Key | BASIC | TIMER_SET |
|-----|-------|-----------|
| **SW2** (PE4) | fan OFF (speed 0) | **+10 min** |
| **SW3** (PE5) | speed **−** | **+1 min** |
| **SW4** (PE6) | speed **+** | reset to 0 |
| **SW5** (PE7) | enter TIMER_SET | apply value, return to BASIC (starts countdown) |

* SW2/SW3/SW4 **auto‑repeat** while held (≈0.5 s delay, then ≈8/s).
* In TIMER_SET the step **accelerates** while held: ×1 → ×10 (after ≈1 s) → ×100 (after ≈2.5 s),
  so 9999 is reachable in a few seconds.
* In ALARM, **any** key returns to BASIC.

### What each display shows

| Output | BASIC (no timer) | BASIC (counting) | TIMER_SET | ALARM |
|---|---|---|---|---|
| **FND** (4 digit) | `   N` speed | `NNNN` minutes left | `NNNN` set minutes *(blink)* | `----` *(blink)* |
| **LED bar** (PB0–PB7) | speed 1–8 (fills from **PB7**) | speed 1–8 | speed 1–8 | all *(blink)* |
| **LCD line 0** | `Wind speed : N/8` | `Wind speed : N/8` | `Wind speed : N/8` | `Wind speed : 0/8` |
| **LCD line 1** | `Timer : OFF` | `Time left:NNNN m` | `Set timer:NNNN m` | `*** TIME  UP ***` |

---

## 2. Hardware / wiring

MCU: **ATmega128**, external **16 MHz** crystal, CKDIV8 fuse **off**.

| Signal | MCU pin(s) | Notes |
|---|---|---|
| LCD data `D0..D7` | **PORTA** `PA0..PA7` | 8‑bit interface |
| LCD `RS` | **PG0** | |
| LCD `R/W` | **PG1** | firmware holds it low (write‑only) |
| LCD `E` | **PG2** | |
| Buzzer | **PG3** | active‑high; **disabled in firmware** (`BUZZER_ENABLED 0`) |
| LED bar | **PORTB** `PB0..PB7` | `PB7` = bottom of bar, active‑high |
| FND segments `a..dp` | **PORTC** `PC0=a … PC7=dp` | common **anode** → segment ON = **LOW** |
| FND digit select | **PD4** (leftmost) … **PD7** (rightmost) | digit ON = **HIGH** |
| Fan motor PWM | **PE3 / OC3A** | Timer3, 8‑bit Fast PWM, prescale /8 (≈7.8 kHz) |
| `SW2..SW5` | **PE4..PE7** | active‑low, internal pull‑ups |

```
                          ATmega128
   PORTA  PA0..PA7  ─────  LCD  D0..D7
   PORTB  PB0..PB7  ─────  LED bar          (PB7 = bottom)
   PORTC  PC0..PC7  ─────  FND segments a..dp   (ON = LOW, common anode)
          PD4 PD5 PD6 PD7  FND digit 1 2 3 4    (ON = HIGH)
          PE3             ─ Motor PWM  (OC3A)
          PE4 PE5 PE6 PE7 ─ SW2 SW3 SW4 SW5     (pull-up, active low)
          PG0 PG1 PG2     ─ LCD  RS  R/W  E
          PG3             ─ Buzzer
```

Every peripheral is on its own pins — there is **no shared bus**, so the drivers are simple.

---

## 3. RTOS

* **FreeRTOS Kernel V11.1.0** (vendored under `rtos_project/Source/`)
* **Port:** `Source/portable/GCC/ATMega323` — the classic AVR 8‑bit port.
  The system tick runs on **Timer1** (compare‑match A); `TIMER1_COMPA_vect` in `port.c`
  calls `xTaskIncrementTick()` / `vTaskSwitchContext()`.
* **Heap:** `heap_1` — all objects are created once at start‑up, nothing is ever deleted.

Key settings in `FreeRTOSConfig.h`:

| Setting | Value |
|---|---|
| `configCPU_CLOCK_HZ` | `16000000` |
| `configTICK_RATE_HZ` | `1000` (1 ms tick) |
| `configUSE_PREEMPTION` | `1` |
| `configUSE_16_BIT_TICKS` | `1` |
| `configMAX_PRIORITIES` | `6` |
| `configMINIMAL_STACK_SIZE` | `110` |
| `configTOTAL_HEAP_SIZE` | `2800` |
| `configUSE_MUTEXES` | `1` |
| `configUSE_TASK_NOTIFICATIONS` | `1` |

> 16‑bit ticks wrap every ~65 s, so the countdown does **not** rely on tick values.
> `vAppTask` keeps its own 32‑bit "seconds remaining" counter, fed by tick *deltas*.

### Tasks

| Task | Prio | Cadence | Stack | Responsibility |
|---|:--:|---|---|---|
| `vFndTask`    | **4** | `vTaskDelayUntil` 2 ms | `MIN+40`  | Multiplex the 4 FND digits from `s_seg[4]` (PORTC + PD4‑7). |
| `vKeyTask`    | **3** | `vTaskDelayUntil` 10 ms | `MIN+50` | Sample PE4‑7, 3‑sample (30 ms) debounce, detect press edge + auto‑repeat, push `KeyEvent_t` to `xKeyQueue`. |
| `vBuzzerTask` | **3** | blocks on notification | `MIN+40` | On alarm: 5 × (200 ms on / 200 ms off) then disarm the timer. (Buzzer output muted.) |
| `vAppTask`    | **2** | `xQueueReceive` (40 ms timeout) | `MIN+160` | The application: mode state machine, motor duty (`OCR3A`), 1‑second countdown, and building all display content. |
| `vLcdTask`    | **1** | 60 ms, writes only on change | `MIN+90` | Render changed LCD rows to the HD44780 (PORTA + PG0/1/2). |
| *Idle*        | 0 | — | `MIN` | — |

### Inter‑task communication / shared state

| Object | Type | Producer → Consumer |
|---|---|---|
| `xKeyQueue` | Queue, `KeyEvent_t{sw,type}`, len 8 | `vKeyTask` → `vAppTask` |
| task notification | binary | `vAppTask` (`xTaskNotifyGive`) → `vBuzzerTask` (`ulTaskNotifyTake`) |
| `s_seg[4]` | array, `taskENTER_CRITICAL` | `fnd_set()` (in `vAppTask`) → `vFndTask` |
| `s_line[2][16]` + `s_dirty` | buffer + row bitmask, `taskENTER_CRITICAL` | `lcd_set_line()` (in `vAppTask`) → `vLcdTask` (redraws only dirty rows) |
| `s_mode`, `s_armed` | `volatile uint8_t` (atomic on AVR) | `vAppTask` ↔ `vBuzzerTask` |
| `s_speed`, `s_totalMin`, `s_remain` | plain | `vAppTask` only |

---

## 4. Diagrams

### Data flow

```
 [SW2-SW5]                                                     [MOTOR PE3]
  PE4-PE7                                                          ^
    |  poll 10ms                                                   | OCR3A
    v                                                              |
 vKeyTask ──KeyEvent──▶ xKeyQueue ──▶ vAppTask (state machine) ────┤
  debounce               (len 8)         |     ^                    |
  auto-repeat                            |     | 1s tick accumulator|
                                         |     | (tick deltas)      |
                   xTaskNotifyGive ◀─────┤                          |
                          |              |                          |
                          v          fnd_set()   ledbar() ──────▶ [LED bar PB0-7]
                    vBuzzerTask          |
                    beep x5 ──▶ [BUZZER PG3, muted]
                                         |          lcd_set_line()
                              s_seg[4]   |          s_line[2][16] + dirty
                                 |       |               |
                                 v       |               v
                            vFndTask (2ms)          vLcdTask (60ms / on change)
                            multiplex 4 digits      render changed rows
                                 |                        |
                                 v                        v
                       [FND PORTC + PD4-7]        [LCD PORTA + PG0/1/2]
```

### Mode state machine (`vAppTask`)

```
                power on
                   |
                   v
        +----------------------+   SW5 press    +--------------------+
        |        BASIC         | -------------> |     TIMER_SET      |
        |  SW2 off / SW3 -     |               |  SW2 +10  SW3 +1    |
        |  SW4 +  / SW5 -> set |               |  SW4 reset          |
        |                      | <------------- |  SW5 apply+arm     |
        +----------------------+   SW5 press    +--------------------+
             |            ^
             | every 1s, if armed:  s_remain--
             | s_remain == 0
             v
        +----------------------+
        |        ALARM         |  fan off, FND "----", LED bar blink
        |                      |  vBuzzerTask: 5 beeps (muted) then disarm
        +----------------------+
             | any key  /  buzzer pattern done
             v
           BASIC  (disarmed, speed 0)
```

### Speed → outputs

```
 s_speed (0..8)
   ├─ motor:  OCR3A = s_speed * 255 / 8          (0, 31, 63, ... 255)
   └─ LED bar: n LEDs lit from PB7 downward       (speed 3 => PB7 PB6 PB5)
```

---

## 5. Build & flash

1. Open `rtos_project.atsln` in **Atmel Studio 7**.
2. Device: **ATmega128**. Toolchain: AVR/GNU C.
   Compiler symbols: `GCC_MEGA_AVR`, `F_CPU=16000000UL`.
   Include paths: `../`, `../Source/include`, `../Source/portable/GCC/ATMega323`.
3. Fuses: external **16 MHz** crystal, **CKDIV8 off**.
4. Programmer: **AVRISP mkII**, interface **ISP**, clock 125 kHz.
5. **Build** (F7) → **Start Without Debugging** (Ctrl+Alt+F5). *(ISP has no debug mode.)*

To enable the buzzer later: set `BUZZER_ENABLED 1` in `board.h`.

---

## 6. Project layout

```
rtos_project.atsln
rtos_project/
├─ main.c              application: state machine, keys, buzzer task, motor, display content
├─ disp.c / disp.h     vFndTask + vLcdTask + FND/LCD drivers
├─ board.h             all pin assignments + F_CPU  ← edit here for wiring changes
├─ FreeRTOSConfig.h    kernel configuration
├─ rtos_project.cproj  Atmel Studio project file
├─ Source/             FreeRTOS Kernel V11.1.0 (vendored)
└─ tools/
   └─ lcd_probe.c      standalone HD44780 bring-up test (not part of the build)
```

---

## 7. Notes / limits

* Buzzer output is compiled out (`BUZZER_ENABLED 0`); the alarm *logic* still runs.
* `heap_1`: no `vPortFree`; every task/queue is allocated once at boot.
* Timer range: 0–9999 minutes (~166 h).
* FND is common‑anode (`FND_SEG_ON_LOW 1`); digit select is active‑high
  (`FND_DIG_ACTIVE_LOW 0`). Flip those in `board.h` for a different FND module.
