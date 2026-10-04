# Hisense AC → ESPHome / Home Assistant

> Форк проекта [pio2398/W4G2](https://github.com/pio2398/W4G2), доработанный и проверенный на реальных кондиционерах Hisense с проводной шиной RS-485.

## Русский

### Что это

Компонент ESPHome для прямого управления кондиционером Hisense по внутренней RS-485 шине без штатного Wi-Fi-модуля AEH-W4A1 / AEH-W4G1.

Текущая версия проверена на ESP8266 D1 mini и ESPHome **2026.8.2**. На двух кондиционерах с одинаковым форматом кадров компонент стабильно читает состояние, управляет режимами, скоростью вентилятора, жалюзи и дополнительными функциями.

### Что исправлено и добавлено

По сравнению с исходным W4G2:

- миграция `climate.py` на актуальный API ESPHome (`climate.climate_schema()`) с совместимостью со старыми версиями;
- правильные режимы: `OFF`, `COOL`, `HEAT`, `DRY`, `FAN_ONLY`, `AUTO/SMART`;
- правильный диапазон заданной температуры: **16…30 °C**;
- корректная текущая комнатная температура в сущности `climate`;
- исправлено чтение наружной температуры и температуры наружного теплообменника;
- значение влажности `0x80` трактуется как **нет данных / unavailable**, а не как 128 %;
- добавлено корректное состояние действия: Cooling / Heating / Drying / Fan / Idle;
- исправлены коды скоростей вентилятора;
- поддерживаются все семь скоростей с пульта:
  - AUTO
  - QUIET
  - LOWER
  - LOW
  - MEDIUM
  - HIGH
  - HIGHER
- `LOWER` и `HIGHER` реализованы как custom fan modes, остальные — как штатные режимы ESPHome;
- поддерживаются обе оси жалюзи: OFF / VERTICAL / HORIZONTAL / BOTH;
- исправлено формирование TX-команд и проверка температуры;
- команды управления больше не отправляются одновременно с запросом статуса: это устраняет ситуацию, когда кондиционер пищал, но игнорировал команду;
- добавлены дополнительные функции:
  - SUPER / Turbo;
  - ECONOMY;
  - DIMMER / Display;
  - SLEEP 1…4;
- SUPER запоминает предыдущую температуру и скорость вентилятора и восстанавливает их при выключении;
- для Display добавлена фильтрация кратковременного изменения статуса: некоторые блоки примерно на 10 секунд включают дисплей после любой команды даже при выключенном DIMMER;
- исправлено восстановление после ошибок обмена и уменьшен лишний отладочный шум.

### Проверенные коды вентилятора

| Режим | status RX | command TX |
|---|---:|---:|
| AUTO | `0x01` | `0x01` |
| QUIET | `0x02` | `0x03` |
| LOWER | `0x0A` | `0x0B` |
| LOW | `0x0C` | `0x0D` |
| MEDIUM | `0x0E` | `0x0F` |
| HIGH | `0x10` | `0x11` |
| HIGHER | `0x12` | `0x13` |

### Подключение

Пример для **ESP8266 D1 mini + полудуплексный RS-485 трансивер**:

| D1 mini | Назначение |
|---|---|
| GPIO1 / TX | DI / передача RS-485 |
| GPIO3 / RX | RO / приём RS-485 |
| GPIO4 / D2 | DE + /RE, объединённые вместе |
| GND | общая земля |

UART: **9600 8N1**.

Уровни питания и логики конкретного RS-485 модуля обязательно проверяйте отдельно. Не подавайте напряжение шины кондиционера напрямую на ESP8266.

### Подключение компонента из GitHub

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/sonooby/hisense_AEG
    components: [hisense]
```

### Минимальная конфигурация ESPHome

```yaml
esp8266:
  board: d1_mini

logger:
  level: DEBUG
  baud_rate: 0

uart:
  id: mod_bus
  tx_pin: GPIO1
  rx_pin: GPIO3
  baud_rate: 9600

climate:
  - platform: hisense
    name: "Hisense AC"
    uart_id: mod_bus
    flow_control_pin: GPIO4
    id: hisense_ac

sensor:
  - platform: hisense
    hisense_id: hisense_ac
    indoor_temperature:
      name: "Indoor Temperature"
    indoor_coil_temperature:
      name: "Indoor Coil Temperature"
    outdoor_temperature:
      name: "Outdoor Temperature"
    outdoor_coil_temperature:
      name: "Outdoor Coil Temperature"
    indoor_humidity:
      name: "Indoor Humidity"
```

Для анализа протокола можно временно включить:

```yaml
uart:
  id: mod_bus
  tx_pin: GPIO1
  rx_pin: GPIO3
  baud_rate: 9600
  debug:
    direction: BOTH
    dummy_receiver: false
    after:
      bytes: 512
      timeout: 300ms
```

### SUPER, Economy, Display и Sleep в Home Assistant

Дополнительные функции удобно вывести отдельными сущностями ESPHome:

```yaml
switch:
  - platform: template
    name: "AC — Super"
    id: ac_super
    icon: mdi:rocket-launch
    lambda: |-
      return id(hisense_ac).get_turbo_switch();
    turn_on_action:
      - lambda: |-
          id(hisense_ac).set_turbo_switch(true);
    turn_off_action:
      - lambda: |-
          id(hisense_ac).set_turbo_switch(false);

  - platform: template
    name: "AC — Economy"
    id: ac_economy
    icon: mdi:leaf
    lambda: |-
      return id(hisense_ac).get_economy_switch();
    turn_on_action:
      - lambda: |-
          id(hisense_ac).set_economy_switch(true);
    turn_off_action:
      - lambda: |-
          id(hisense_ac).set_economy_switch(false);

  - platform: template
    name: "AC — Display"
    id: ac_display
    icon: mdi:lightbulb-outline
    lambda: |-
      return id(hisense_ac).get_display_switch();
    turn_on_action:
      - lambda: |-
          id(hisense_ac).set_display_switch(true);
    turn_off_action:
      - lambda: |-
          id(hisense_ac).set_display_switch(false);

select:
  - platform: template
    name: "AC — Sleep"
    id: ac_sleep
    icon: mdi:sleep
    options:
      - "OFF"
      - "SLEEP 1"
      - "SLEEP 2"
      - "SLEEP 3"
      - "SLEEP 4"
    update_interval: 2s
    lambda: |-
      switch (id(hisense_ac).get_sleep_profile()) {
        case 1: return std::string("SLEEP 1");
        case 2: return std::string("SLEEP 2");
        case 3: return std::string("SLEEP 3");
        case 4: return std::string("SLEEP 4");
        default: return std::string("OFF");
      }
    set_action:
      - lambda: |-
          if (x == "SLEEP 1") {
            id(hisense_ac).set_sleep_profile(1);
          } else if (x == "SLEEP 2") {
            id(hisense_ac).set_sleep_profile(2);
          } else if (x == "SLEEP 3") {
            id(hisense_ac).set_sleep_profile(3);
          } else if (x == "SLEEP 4") {
            id(hisense_ac).set_sleep_profile(4);
          } else {
            id(hisense_ac).set_sleep_profile(0);
          }
```

> Важно: у `switch.template` здесь не нужен `update_interval`. Для `select.template` он используется.

### Особенности и ограничения

**I FEEL** пока не реализован. На проверенном пульте эта функция использует датчик температуры в самом пульте, а отдельного надёжного состояния I FEEL в основном RS-485 status frame обнаружить не удалось.

**Наружная температура.** Байты наружного датчика и наружного теплообменника читаются корректно во время работы наружного блока. Когда наружный блок выключен или простаивает, значения могут долго не обновляться и оставаться на последнем/служебном значении. Поэтому их нельзя считать полноценным непрерывным уличным термометром.

**Влажность.** На проверенных кондиционерах приходит `0x80`, то есть датчик/значение недоступно. В Home Assistant это публикуется как unavailable/NaN.

**Совместимость.** Протокол Hisense отличается между моделями. Этот компонент проверен на конкретных блоках с кадрами семейства `F4 F5 ... 01 40 8D ... F4 FB`. Перед использованием на другой модели рекомендуется сначала проверить RX-кадры.

### Благодарности

Основа проекта: [pio2398/W4G2](https://github.com/pio2398/W4G2).

Исходная работа также опиралась на:
- [straga/scrivo_project](https://github.com/straga/scrivo_project) — @straga;
- исследования @polsup2 в issue #1 проекта scrivo_project.

Лицензия проекта: GPL-3.0.

---

## English

### What this is

An ESPHome component for direct Hisense air-conditioner control over the internal RS-485 bus without the original AEH-W4A1 / AEH-W4G1 Wi-Fi module.

The current version has been tested with an ESP8266 D1 mini and ESPHome **2026.8.2** on two Hisense units using the same frame format.

### Changes and improvements

Compared with the original W4G2 component, this fork includes:

- migration of `climate.py` to the current ESPHome climate API, with a compatibility fallback for older ESPHome releases;
- working `OFF`, `COOL`, `HEAT`, `DRY`, `FAN_ONLY`, and `AUTO/SMART` modes;
- correct 16…30 °C target-temperature range;
- current room temperature exposed by the climate entity;
- corrected outdoor and outdoor-coil temperature offsets;
- humidity `0x80` treated as unavailable instead of 128%;
- proper climate action reporting;
- corrected fan-speed RX/TX mapping;
- all seven remote-controller fan speeds:
  - AUTO
  - QUIET
  - LOWER
  - LOW
  - MEDIUM
  - HIGH
  - HIGHER
- full swing support: OFF / VERTICAL / HORIZONTAL / BOTH;
- command frames are queued until the RS-485 bus is idle, preventing status polling and control commands from being concatenated;
- SUPER/Turbo, Economy, Display/Dimmer, and Sleep 1…4 support;
- SUPER stores the previous target temperature and fan speed and restores them when Turbo is disabled;
- Display status debounce to ignore the short display wake-up some units perform after ordinary commands;
- communication recovery fixes and reduced parser log spam.

### Fan protocol mapping

| Mode | status RX | command TX |
|---|---:|---:|
| AUTO | `0x01` | `0x01` |
| QUIET | `0x02` | `0x03` |
| LOWER | `0x0A` | `0x0B` |
| LOW | `0x0C` | `0x0D` |
| MEDIUM | `0x0E` | `0x0F` |
| HIGH | `0x10` | `0x11` |
| HIGHER | `0x12` | `0x13` |

### Wiring

Example for **ESP8266 D1 mini + half-duplex RS-485 transceiver**:

| D1 mini | Function |
|---|---|
| GPIO1 / TX | RS-485 DI |
| GPIO3 / RX | RS-485 RO |
| GPIO4 / D2 | DE + /RE tied together |
| GND | common ground |

UART: **9600 8N1**.

Always verify the supply and logic-level requirements of your RS-485 board. Never connect the AC bus supply directly to the ESP8266.

### Use this component from GitHub

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/sonooby/hisense_AEG
    components: [hisense]
```

### Minimal ESPHome configuration

```yaml
esp8266:
  board: d1_mini

logger:
  level: DEBUG
  baud_rate: 0

uart:
  id: mod_bus
  tx_pin: GPIO1
  rx_pin: GPIO3
  baud_rate: 9600

climate:
  - platform: hisense
    name: "Hisense AC"
    uart_id: mod_bus
    flow_control_pin: GPIO4
    id: hisense_ac

sensor:
  - platform: hisense
    hisense_id: hisense_ac
    indoor_temperature:
      name: "Indoor Temperature"
    indoor_coil_temperature:
      name: "Indoor Coil Temperature"
    outdoor_temperature:
      name: "Outdoor Temperature"
    outdoor_coil_temperature:
      name: "Outdoor Coil Temperature"
    indoor_humidity:
      name: "Indoor Humidity"
```

The Russian section above contains the complete template-switch/select example for SUPER, Economy, Display, and Sleep; the entity API is language-independent.

### Known limitations

**I FEEL** is not implemented. On the tested remote, I FEEL uses the temperature sensor inside the remote, while no reliable persistent I FEEL state was found in the main RS-485 status frame.

**Outdoor temperature sensors** are useful while the outdoor unit is active, but on tested units they may stop updating while the outdoor unit is idle/off and remain at a stale or placeholder value. They should not be treated as a continuously updated outdoor thermometer.

**Humidity** is unsupported on the tested units and reports `0x80`; it is exposed as unavailable/NaN.

**Compatibility** is not guaranteed across all Hisense models. This fork was tested on units using the `F4 F5 ... 01 40 8D ... F4 FB` status-frame family.

### Credits

Based on [pio2398/W4G2](https://github.com/pio2398/W4G2).

The original project also references:
- [straga/scrivo_project](https://github.com/straga/scrivo_project) by @straga;
- protocol work by @polsup2 in scrivo_project issue #1.

License: GPL-3.0.
