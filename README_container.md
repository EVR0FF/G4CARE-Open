# G4CARE Apptainer Container

Переносимый контейнер со скомпилированным G4CARE, Geant4 v11.4.0 и ROOT.

## Сборка контейнера (на этой машине)

```bash
cd /home/ever/GProjects/G4CARE
chmod +x build_container.sh
./build_container.sh
```

Результат: файл `G4CARE.sif` (ожидаемый размер ~8-15 ГБ).

Если скрипт не работает — запусти вручную:

```bash
apptainer build G4CARE.sif G4CARE.def
```

## Перенос на целевой компьютер

```bash
scp G4CARE.sif user@target-host:/path/to/destination/
```

Или через флешку / сетевую папку.

## Запуск на целевом компьютере

```bash
# Базовый запуск с конфигом
apptainer run G4CARE.sif config.yaml

# Интерактивный shell внутри контейнера
apptainer shell G4CARE.sif

# Монтирование внешней папки для данных
apptainer run --bind /path/to/data:/data G4CARE.sif /data/config.yaml

# Запуск со своим скриптом
apptainer exec G4CARE.sif /opt/g4care/G4CARE my_config.yaml
```

## Структура внутри контейнера

| Путь | Содержимое |
|------|-----------|
| `/opt/geant4/` | Geant4 v11.4.0 (библиотеки) |
| `/opt/geant4-data/` | Данные Geant4 (G4NDL, G4EMLOW, ...) |
| `/opt/root/` | ROOT (библиотеки + бинарники) |
| `/opt/g4care/G4CARE` | Исполняемый файл G4CARE |
| `/opt/g4care/configs/` | Примеры YAML-конфигов |

## Требования к целевому компьютеру

- **Apptainer** ≥ 1.1 (или Singularity ≥ 3.5)
- **x86_64** архитектура
- Рекомендуется Ubuntu 22.04+ (но работает на любом Linux с Apptainer)

## Примечания

- Контейнер собран на Ubuntu 24.04, базовый образ — `docker://ubuntu:24.04`
- Все переменные окружения (G4DATA, LD_LIBRARY_PATH) настроены внутри контейнера
- YAML-конфиги можно передавать снаружи через `--bind` или копировать внутрь