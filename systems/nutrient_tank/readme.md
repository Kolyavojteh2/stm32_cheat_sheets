chatgpt:

---

# NutrientTank System Documentation

## 1. Призначення

`NutrientTank_t` — модуль керування гідропонним розчином у головному резервуарі з підтримкою:

* керування насосами (вода/добрива/pH up/pH down/air/circulation/drain/return)
* безпечного запуску насосів через `PumpGuard`
* оцінки рівнів/об’ємів резервуарів через датчик дистанції (SR04M) за callback-мапінгом `distance_mm -> volume_ul`
* closed-loop регуляції **TDS** і **pH** через `RecipeController`
* стабілізаційних затримок (aerate + settle)
* захисту від роботи по “старих” вимірюваннях (gating: **потрібні нові дані після settle**)
* timeout очікування нового вимірювання (щоб control не зависав)

Модуль розрахований на **інстанси**: можна мати кілька резервуарів/підсистем.

---

## 2. Склад модулів і файли

### Основні модулі

* `nutrient_tank.h/.c`
  FSM керування, інтеграція guards/sensors/recipe, події, команди.
* `recipe_controller.h/.c`
  Closed-loop логіка: pH/TDS, пропорційне дозування добрив, адаптивні кроки, commit accounting.
* `tank_sensors.h/.c`
  Зберігання значень pH/TDS/Temp з timestamp, перевірка freshness, перевірка “оновлено після моменту”.
* `pump_guard.h/.c`
  Безпека насоса: блокування по рівню/сенсору, max runtime, старт по часу/об’єму.
* `pump_unit.h/.c`
  Низькорівневий “насос”: on/off, параметри продуктивності (калібрування), контроль стану роботи.

### Зовнішні/інтеграційні залежності

* `GPIO_switch_t` (фізичне керування насосом)
* датчики `SR04M_t`, `TDS_Meter_t`, `PH_Sensor_t`, `DS18B20_t` не підключені напряму до `NutrientTank`.
  Їхні значення маєш подавати в `TankSensors_t`/`nutrient_tank_update_*()` ззовні.

---

## 3. Одиниці вимірювання (важливо)

* Об’єм: `volume_ul` — **мікролітри** (uL)
  1 мл = 1000 uL
  1 л = 1,000,000 uL
* Дистанція: `distance_mm` — **міліметри**
* pH: `ph_x1000` — pH * 1000 (6.250 → 6250)
* Температура: `temperature_mC` — **milli°C**
* TDS: `tds_ppm` — **ppm**
* Час: `now_ms` — **мс** монотонний tick (HAL_GetTick або аналог)

---

## 4. Архітектура та потік даних

### 4.1. Основний цикл (non-blocking)

Модуль працює без блокувань. Ти викликаєш:

* `nutrient_tank_process(&tank, now_ms)` — часто (наприклад кожні 10–50 мс)
* пушиш дані сенсорів окремо:

  * `tank_sensors_update_ph_x1000(...)`
  * `tank_sensors_update_tds_ppm(...)`
  * `nutrient_tank_update_main_distance_mm(...)` тощо

### 4.2. Хто за що відповідає

* `NutrientTank`:

  * запускає/зупиняє насоси через `PumpGuard`
  * тримає FSM (IDLE/EXECUTING/AERATE/WAIT_SETTLE/STOPPED)
  * керує циркуляцією з політикою рівнів
  * генерує події
  * інжектить команди дозування з `RecipeController` у closed-loop режимі
* `RecipeController`:

  * вирішує, що дозувати далі (water/nutrient/ph up/ph down)
  * дозує добрива **пропорційно** (4 канали) з кроком, масштабованим по об’єму
  * має **адаптивний розмір кроку** за величиною помилки
  * робить **commit accounting** тільки після success (через `recipe_controller_on_dose_result()`)
  * має inflight step: поки нема ack — не генерує новий
* `TankSensors`:

  * зберігає значення + timestamp
  * визначає “fresh/stale”
  * визначає “оновлено після settle”
* `PumpGuard`:

  * застосовує ліміти і блокування
  * запускає насос по часу/об’єму (використовує калібрування `PumpUnit`)
* `PumpUnit`:

  * вмикає/вимикає GPIO
  * рахує час для заданого об’єму (за `flow_ul_per_s`)

---

## 5. Рівні резервуарів і volume mapping

### 5.1. Мапінг дистанції у об’єм

Для кожного резервуара задається callback:

```c
typedef uint32_t (*NutrientTank_VolumeMapFn)(void *ctx, uint32_t distance_mm);
```

Це дозволяє:

* різні геометрії ємностей
* лінійний або табличний/кусочно-лінійний перерахунок
* калібрування “залив N літрів → зняв дистанцію”

### 5.2. Наявність датчика

Якщо датчик відсутній:

* `map_fn == NULL`
  → `NutrientTank` не блокує операції по рівню (але тоді відповідальність за безпеку — на користувачі/верхньому рівні).

Якщо датчик є, але:

* fault або stale → операції, які залежать від рівня, **блокуються**.

---

## 6. Pump safety (PumpGuard)

`PumpGuard` додає захист:

* `max_run_ms` — максимум безперервної роботи
* блокування по датчику рівня (якщо датчик прив’язаний)
* блокування при помилках/відсутності даних з датчика

Типові сценарії:

* вода/добриво/pH: блокувати, якщо головний резервуар вже **high**
* drain/circulation: блокувати, якщо головний резервуар **low/critical**
* return: блокувати, якщо головний резервуар вже біля **block_return**

---

## 7. NutrientTank FSM (стани)

### Стани (`NutrientTank_State_t`)

* `IDLE` — готовий приймати/виконувати
* `EXECUTING` — виконує активну команду (помпа крутиться)
* `AERATE_AFTER_DOSE` — після дозування працює air pump (mix)
* `WAIT_SETTLE` — чекає стабілізацію перед наступними діями
* `STOPPED` — аварійно зупинений (тільки reset/explicit команди повертають)
* `ERROR` — зарезервовано (в коді зазвичай подія + last_error)

### Після дозування

Якщо команда дозування потребує стабілізації:

1. `EXECUTING` (dose pump)
2. `AERATE_AFTER_DOSE` (air pump `after_dose_aerate_ms`)
3. `WAIT_SETTLE` (`after_dose_settle_ms`)
4. `IDLE`

---

## 8. Команди (API)

### Подати команду

```c
uint8_t nutrient_tank_submit_command(NutrientTank_t *tank, const NutrientTank_Command_t *cmd);
```

#### Команди

* `NUTRIENT_TANK_CMD_AERATE_FOR_MS`
* `NUTRIENT_TANK_CMD_CIRCULATION_SET`
* `NUTRIENT_TANK_CMD_DOSE_VOLUME`
* `NUTRIENT_TANK_CMD_CONTROL_START`
* `NUTRIENT_TANK_CMD_CONTROL_STOP`
* `NUTRIENT_TANK_CMD_EMERGENCY_STOP`

### Важливі правила

* Поки active operation іде — нові команди можуть бути відхилені як `BUSY`.
* Поки `control_active=1` — manual дозування блокується (крім circulation/control_stop/emergency_stop).

---

## 9. Події (Events)

### Витяг подій

```c
uint8_t nutrient_tank_pop_event(NutrientTank_t *tank, NutrientTank_Event_t *ev_out);
```

### Основні події

* рівень головного: `MAIN_LOW`, `MAIN_CRITICAL`, `MAIN_RESUMED`
* рівень return: `RETURN_HIGH`
* запити: `REQUEST_RETURN`, `REQUEST_REFILL`
* control: `CONTROL_DONE`, `CONTROL_ERROR`
* блокування операції: `OPERATION_BLOCKED`

Подія містить:

* `main_volume_ul`, `return_volume_ul`
* `error`, `block_reason`

---

## 10. Closed-loop керування (RecipeController)

### 10.1. Цілі

* pH: `target_ph_x1000`, `ph_tolerance_x1000`
* TDS: `target_tds_ppm`, `tds_tolerance_ppm`

### 10.2. Пріоритети

1. TDS корекція (nutrients або dilution water)
2. pH корекція (ph up/ph down)

### 10.3. Пропорційне дозування 4 добрив

Є ваги каналів (використовується перший доступний набір):

1. `nutrient_ratio[]` якщо сума > 0
2. `nutrient_parts_per_l[]` якщо сума > 0
3. інакше — рівні ваги

Один TDS-крок формує “mix plan” (pending amounts по каналах), а далі видає під-кроки round-robin по каналах.

### 10.4. Масштабування кроку по об’єму (на 1 літр)

Якщо відомий `main_volume_ul`, крок рахується як:

* `uL_per_l * liters` (ceil)
* де `uL_per_l` береться:

  * напряму з `tds_nutrient_step_ul_per_l` / `tds_water_step_ul_per_l`, або
  * може бути виведений з `parts_per_l` та `nutrient_part_volume_ul`

### 10.5. Адаптивний step (за помилкою)

Portion `x1000` масштабує step:

* помилка росте → step росте до max
* параметри:

  * `tds_nutrient_err_full_ppm` + `portion_min/max`
  * `tds_water_err_full_ppm` + `portion_min/max`

### 10.6. Commit accounting (важливо)

`RecipeController` НЕ списує дозу “на плані”.
Він генерує step і чекає ack.

Після завершення фізичного дозування `NutrientTank` викликає:

```c
recipe_controller_on_dose_result(rc, success);
```

* success=1: dose комітиться, pending mix зменшується
* success=0: controller переходить у error (і tank зупиняє control)

---

## 11. Sensor gating: “нові вимірювання після settle”

Проблема: без гейта контролер може зробити кілька кроків підряд по старому значенню pH/TDS (особливо якщо сенсори читаються рідко).

Рішення:

* після **успішного** дозування в control режимі `NutrientTank` активує:

  * `control_wait_measurement=1`
  * `control_measurement_after_ms = момент завершення settle`
* `nt_control_process()` **не робить** наступний step, доки:

  * потрібні сенсори `fresh`
  * і `updated_at_ms >= control_measurement_after_ms`

Це змушує closed-loop працювати тільки по реальному оновленню після стабілізації.

---

## 12. Timeout очікування нового вимірювання

Якщо сенсор відвалився, гейт може чекати вічно. Для цього є:

* `cfg.timing.control_measurement_timeout_ms` (0 = вимкнено)

Якщо timeout сплив:

* control зупиняється
* генерується `NUTRIENT_TANK_EVENT_CONTROL_ERROR`
* `last_error = NUTRIENT_TANK_ERR_TIMEOUT`

---

## 13. Рекомендована конфігурація таймінгів

Твої поточні вимоги (приклад):

* після дозування:

  * air 10–15 с
  * settle 10–15 с

```c
cfg.timing.after_dose_aerate_ms = 15000U;
cfg.timing.after_dose_settle_ms = 15000U;
cfg.timing.after_aerate_settle_ms = 10000U; /* optional */
cfg.timing.control_measurement_timeout_ms = 60000U;
```

---

## 14. Типовий сценарій використання

### 14.1. Ініціалізація

1. Створити `PumpUnit` для кожного насоса (GPIO)
2. Створити `PumpGuard` для кожного насоса (max_run_ms, доступність)
3. Створити `TankSensors_t`, `RecipeController_t`
4. Зібрати `NutrientTank_Config_t`
5. `nutrient_tank_init()`

### 14.2. Runtime loop

* у перериваннях/тасках:

  * читаєш SR04M → `nutrient_tank_update_main_distance_mm(...)`
  * читаєш pH/TDS → `tank_sensors_update_*`
* у main loop:

  * `nutrient_tank_process(&tank, HAL_GetTick())`
  * читаєш events через `nutrient_tank_pop_event()`

### 14.3. Старт closed-loop

`CONTROL_START` з ціллю TDS/pH і tolerance.

---

## 15. Межі і відповідальність (що модуль не робить)

* не читає сенсори сам (ти подаєш значення)
* не “вгадує” об’єм, якщо нема `map_fn`
* не робить складну хімію; TDS/pH регуляція — дискретна, з таймінгами стабілізації
* не планує складні multi-step сценарії повернення/переливу — лише події/політики + команди

---

## 16. Поради по стабільності

1. pH/TDS читати не надто часто, але стабільно (наприклад 1–2 Гц), і завжди оновлювати timestamp.
2. `stale_timeout_ms` для `TankSensors` ставити таким, щоб stale швидко блокував control (наприклад 5–10 с).
3. Якщо SR04M шумить — фільтруй дистанцію до `nutrient_tank_update_*` (медіана/EMA), інакше будуть “скачки” policy.
4. Калібрування насосів (`flow_ul_per_s`) критичне для об’ємного дозування.

---

## 17. Мінімальний checklist перед запуском

* [ ] `map_fn` для main і (за потреби) return правильно масштабує uL
* [ ] `PumpUnit.flow_ul_per_s` калібрований
* [ ] `after_dose_aerate_ms/settle_ms` налаштовані
* [ ] `control_measurement_timeout_ms` > 0
* [ ] `TankSensors.stale_timeout_ms` адекватний
* [ ] `RecipeController` weights (ratio/parts) задані, `nutrient_count=4`

---

