## How to use
```
Як використовувати (мінімально)

Налаштуй ADC канал на пін “A” модуля (вихід 0…2.3V, безпечно для 3.3V ADC). 
DFRobot Wiki
+1

Створи буфери (наприклад 30 семплів, як у прикладі Keyestudio/DFRobot). 
wiki.keyestudio.com
+1

Раз на ~40мс роби tds_meter_sample(&tds, 10);, а раз на ~800–1000мс — tds_meter_get_tds_ppm(&tds).

Приклад ініціалізації (ідея, без CubeMX коду):

vref_v = 3.3f

adc_range = 4096 для 12-bit (F0/F4 зазвичай так) або 1024 якщо реально 10-bit. 
DFRobot Wiki
+1

tds_meter_set_temperature_c(&tds, temp_from_ds18b20); (якщо нема температури — залиш 25°C).
```
