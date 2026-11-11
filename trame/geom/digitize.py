#==============================================================================
# G4CARE - digitize.py — Digitizer (voxelization) helpers
#
# Digitizer (voxelization) helpers
#
# @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
# @date 2026-11-11  @version 0.9.3
# @copyright Copyright (c) 2026 G4CARE Developers
# @license SPDX-License-Identifier: Apache-2.0
#==============================================================================

"""DIGITIZE block parsing -> /api/digitize for the viewer digitizer editor."""

from . import config


# Unit groups used by the structured editor (number + unit composite fields).
ENERGY_UNITS = ["", "eV", "keV", "MeV", "GeV"]
TIME_UNITS = ["", "ps", "ns", "us", "ms", "s"]
LENGTH_UNITS = ["", "nm", "um", "mm", "cm", "m"]
_UNIT_GROUPS = {"energy": ENERGY_UNITS, "time": TIME_UNITS, "length": LENGTH_UNITS}

# Pipeline step names (exact strings accepted by DigitizerModule::ParsePipelineStep).
PIPELINE_STEPS = [
    "efficiency", "signal_time", "time_smear", "dead_time", "energy_smear",
    "gain", "recovery", "noise", "energy_framing", "threshold", "afterpulse",
]


def _f(key, label, typ, help_, **kw):
    d = {"key": key, "label": label, "type": typ, "help": help_}
    d.update(kw)
    return d


def _num_unit(key, label, group, default_unit, help_):
    return _f(key, label, "number_unit", help_, unit=default_unit,
              units=_UNIT_GROUPS[group])


def _expr(label, use_key, expr_key, help_):
    return _f(expr_key, label, "expr", help_, use_key=use_key)


# Structured field catalog served to the viewer. `type` values understood by the
# viewer: number_unit (число + единица), number, int, bool, select, text,
# list_int, expr (use-flag + expression), pipeline (reorderable steps).
FIELD_CATALOG = [
    {
        "section": "Порог и энергетика",
        "fields": [
            _num_unit("threshold", "Порог срабатывания", "energy", "MeV",
                      "Порог срабатывания (МэВ). 0 = без порога."),
            _num_unit("upper_threshold", "Верхний порог (ограничитель)", "energy", "MeV",
                      "Отбрасывать энергию выше порога. 0 = выкл."),
            _num_unit("energy_frame_MeV", "Квантование энергии (framing)", "energy", "MeV",
                      "Шаг энергетического квантования. 0 = выкл."),
        ],
    },
    {
        "section": "Усиление и эффективность",
        "fields": [
            _f("gain", "Коэффициент усиления", "number", "Коэффициент усиления (≥ 0).", minimum=0),
            _f("quantum_efficiency", "Квантовая эффективность", "number",
               "Квантовая эффективность детектора (0..1).", minimum=0, maximum=1),
            _f("efficiency_mode", "Режим эффективности", "select",
               "constant — постоянная, energy — энергетическая кривая.", enum=["constant", "energy"]),
            _f("efficiency_center_MeV", "Центр кривой эффективности", "number",
               "Центр энергетической кривой эффективности (МэВ)."),
            _f("efficiency_fwhm_MeV", "FWHM кривой эффективности", "number",
               "FWHM энергетической кривой эффективности (МэВ).", minimum=0),
            _f("efficiency_file", "Файл кривой эффективности", "text",
               "Файл кривой эффективности (таблица, линейная интерполяция)."),
        ],
    },
    {
        "section": "Световыход (GATE-модель)",
        "fields": [
            _f("light_yield", "Световыход", "number",
               "Световыход сцинтиллятора (фотонов/МэВ). >0 включает модель разрешения по световыходу.", minimum=0),
            _f("transfer_efficiency", "Эффективность переноса света", "number",
               "Эффективность переноса света (0..1).", minimum=0, maximum=1),
            _f("intrinsic_res_fwhm", "Дробное собственное разрешение", "number",
               "Дробное собственное энергетическое разрешение FWHM на референсной энергии.", minimum=0),
            _f("intrinsic_res_ref_energy_MeV", "Референсная энергия (МэВ)", "number",
               "Референсная энергия для дробного собственного разрешения (МэВ)."),
        ],
    },
    {
        "section": "Мёртвое время и восстановление",
        "fields": [
            _num_unit("dead_time_ns", "Мёртвое время", "time", "ns", "Мёртвое время детектора (нс)."),
            _f("dead_time_model", "Модель мёртвого времени", "select",
               "nonparalyzable — непродлевающееся, paralyzable — продлевающееся.",
               enum=["nonparalyzable", "paralyzable"]),
            _f("dead_time_buffer_size", "Глубина буфера мёртвого времени", "int",
               "Глубина буфера непродлевающегося мёртвого времени. 0 = выкл.", minimum=0),
            _num_unit("recovery_time_ns", "Время восстановления", "time", "ns",
                      "Время восстановления детектора (нс)."),
        ],
    },
    {
        "section": "Разрешение",
        "fields": [
            _num_unit("time_res_fwhm_ns", "Временное разрешение (FWHM)", "time", "ns",
                      "Временное разрешение (FWHM)."),
            _f("energy_res_fwhm", "Энергетическое разрешение (FWHM, отн.)", "number",
               "Энергетическое разрешение (FWHM, относительное).", minimum=0),
            _f("energy_res_ref_energy_MeV", "Референсная энергия (МэВ)", "number",
               "Референсная энергия для энергетического разрешения (МэВ)."),
            _f("intrinsic_res_fwhm_MeV", "Собственное разрешение (FWHM, МэВ)", "number",
               "Собственное энергетическое разрешение (FWHM) в МэВ.", minimum=0),
            _num_unit("spatial_res_fwhm_mm", "Пространственное разрешение (FWHM)", "length", "mm",
                      "Пространственное разрешение (FWHM)."),
            _num_unit("doi_resolution_mm", "Разрешение глубины (DoI)", "length", "mm",
                      "Разрешение глубины взаимодействия (DoI)."),
        ],
    },
    {
        "section": "Пространственное ограничение",
        "fields": [
            _f("spatial_confine", "Ограничивать смазанную позицию", "bool",
               "Ограничивать смазанную позицию (усечённый гауссиан)."),
            _num_unit("spatial_half_x_mm", "Полуширина по X", "length", "mm",
                      "Полуширина ограничения по X (0 = без ограничения)."),
            _num_unit("spatial_half_y_mm", "Полуширина по Y", "length", "mm",
                      "Полуширина ограничения по Y (0 = без ограничения)."),
            _num_unit("spatial_half_z_mm", "Полуширина по Z", "length", "mm",
                      "Полуширина ограничения по Z (0 = без ограничения)."),
        ],
    },
    {
        "section": "Форма сигнала",
        "fields": [
            _f("signal_model", "Модель формы сигнала", "select", "Модель формы сигнала.",
               enum=["exponential", "gaussian", "landau"]),
            _num_unit("signal_rise_ns", "Время нарастания", "time", "ns", "Время нарастания сигнала (нс)."),
            _num_unit("signal_fall_ns", "Время спада", "time", "ns", "Время спада сигнала (нс)."),
            _f("afterpulse_probability", "Вероятность послеимпульсов", "number",
               "Вероятность послеимпульсов (0..1).", minimum=0, maximum=1),
        ],
    },
    {
        "section": "Смазывание (размытие)",
        "fields": [
            _f("time_smear_model", "Модель временного размытия", "select", "Модель временного размытия.",
               enum=["gaussian", "landau"]),
            _f("energy_smear_model", "Модель энергетического размытия", "select", "Модель энергетического размытия.",
               enum=["gaussian", "landau"]),
            _num_unit("time_landau_mpv_ns", "Landau MPV (время)", "time", "ns",
                      "MPV распределения Ландау для времени (нс)."),
            _num_unit("time_landau_sigma_ns", "Landau sigma (время)", "time", "ns",
                      "Сигма распределения Ландау для времени (нс)."),
            _num_unit("energy_landau_mpv_MeV", "Landau MPV (энергия)", "energy", "MeV",
                      "MPV распределения Ландау для энергии (МэВ)."),
            _num_unit("energy_landau_sigma_MeV", "Landau sigma (энергия)", "energy", "MeV",
                      "Сигма распределения Ландау для энергии (МэВ)."),
        ],
    },
    {
        "section": "Шум и темновой ток",
        "fields": [
            _num_unit("noise_level", "Уровень шума", "energy", "MeV", "Уровень шума (МэВ)."),
            _f("dark_count_rate_kHz", "Частота темновых отсчётов", "number",
               "Частота темновых отсчётов (кГц).", minimum=0),
            _num_unit("dark_count_energy_MeV", "Энергия темновых импульсов", "energy", "MeV",
                      "Энергия темновых импульсов (МэВ)."),
        ],
    },
    {
        "section": "Перекрёстные наводки (crosstalk)",
        "fields": [
            _f("cross_talk_probability", "Вероятность crosstalk", "number",
               "Вероятность перекрёстных наводок (0..1).", minimum=0, maximum=1),
            _f("cross_talk_fraction", "Доля энергии соседу", "number",
               "Доля энергии, передаваемая каждому соседу (с сохранением энергии).", minimum=0),
            _f("cross_talk_neighbors", "Смещения ID соседей", "list_int",
               "Смещения ID соседних детекторов для crosstalk (например: 1, 2)."),
        ],
    },
    {
        "section": "Наложение (pile-up)",
        "fields": [
            _f("pileup_model", "Модель pile-up", "select", "Модель наложения сигналов.",
               enum=["none", "simple"]),
            _num_unit("pileup_window_ns", "Окно pile-up", "time", "ns", "Окно наложения сигналов (нс)."),
            _f("pileup_buffer_size", "Размер буфера pile-up", "int",
               "Размер буфера для хранения наложенных сигналов.", minimum=0),
        ],
    },
    {
        "section": "Считывание и тип",
        "fields": [
            _f("calorimeter_mode", "Калориметр (суммировать в один диджит)", "bool",
               "Суммировать все попадания в один диджит на событие."),
            _f("digit_type", "Тип подсистемы", "int", "Тип подсистемы: 0 = TKR, 1 = CAL, 2 = ACD."),
            _f("readout_group", "Группа считывания", "int",
               "Идентификатор группы считывания (детекторы одной группы суммируются)."),
            _f("readout_policy", "Политика считывания", "select",
               "energy_winner — победитель по энергии, energy_centroid — энерго-взвешенный центроид.",
               enum=["energy_winner", "energy_centroid"]),
        ],
    },
    {
        "section": "Совпадения (coincidence)",
        "fields": [
            _num_unit("coincidence_window_ns", "Окно совпадений", "time", "ns", "Окно совпадений (нс)."),
            _f("coincidence_multiplicity", "Кратность совпадений", "int",
               "Кратность совпадений (детекторы одной группы считывания).", minimum=0),
            _f("coincidence_min_sector_diff", "Мин. разница ID детекторов", "int",
               "Минимальная разница ID детекторов для валидного совпадения (0 = выкл).", minimum=0),
            _f("coincidence_multiples_policy", "Политика кратных", "select",
               "reject — отбрасывать кратные (CoincidenceID=-2), keep_all — сохранять.",
               enum=["reject", "keep_all"]),
        ],
    },
    {
        "section": "Выражения (ExprTk)",
        "fields": [
            _expr("Эффективность", "use_efficiency_expression", "efficiency_expression",
                  "Выражение, возвращающее эффективность (0..1)."),
            _expr("Усиление", "use_gain_expression", "gain_expression",
                  "Выражение, возвращающее коэффициент усиления."),
            _expr("Шум", "use_noise_expression", "noise_expression",
                  "Выражение, возвращающее вклад шума (МэВ)."),
            _expr("Нижний порог", "use_threshold_expression", "threshold_expression",
                  "Выражение, возвращающее нижний порог (МэВ)."),
            _expr("Верхний порог", "use_upper_threshold_expression", "upper_threshold_expression",
                  "Выражение, возвращающее верхний порог (МэВ)."),
            _expr("Время сигнала", "use_signal_time_expression", "signal_time_expression",
                  "Выражение для времени сигнала."),
            _expr("Временное размытие", "use_time_smear_expression", "time_smear_expression",
                  "Выражение для временного размытия."),
            _expr("Энергетическое размытие", "use_energy_smear_expression", "energy_smear_expression",
                  "Выражение для энергетического размытия."),
            _expr("Энергия pile-up", "use_pileup_energy_expression", "pileup_energy_expression",
                  "Выражение для энергии в pile-up."),
            _expr("Время pile-up", "use_pileup_time_expression", "pileup_time_expression",
                  "Выражение для времени в pile-up."),
        ],
    },
]


def _find_object(cfg, name):
    """Find an object dict by its 'name' field (top-level OBJECTS + CHILDREN)."""
    objects = cfg.get("GEOMETRY", {}).get("OBJECTS", {})
    if not isinstance(objects, dict):
        return None

    def search(obj):
        if not isinstance(obj, dict):
            return None
        if obj.get("name") == name:
            return obj
        children = obj.get("CHILDREN")
        if isinstance(children, dict):
            for child in children.values():
                r = search(child)
                if r is not None:
                    return r
        return None

    for obj in objects.values():
        r = search(obj)
        if r is not None:
            return r
    return None


def load_digitize(name):
    try:
        cfg = config.load_yaml()
    except Exception:
        return {"name": name, "digitize": {}, "is_sensitive": False, "fields": FIELD_CATALOG, "pipeline_steps": PIPELINE_STEPS}
    obj = _find_object(cfg, name)
    if obj is None:
        return {"name": name, "digitize": {}, "is_sensitive": False, "fields": FIELD_CATALOG, "pipeline_steps": PIPELINE_STEPS}
    d = obj.get("DIGITIZE")
    if not isinstance(d, dict):
        d = {}
    return {"name": name, "digitize": d, "is_sensitive": bool(obj.get("isSensitive")),
            "fields": FIELD_CATALOG, "pipeline_steps": PIPELINE_STEPS}


def _coerce(v):
    if v is None or v == "":
        return None
    if isinstance(v, bool):
        return v
    if isinstance(v, (int, float)):
        return v
    if isinstance(v, (list, dict)):
        return v
    s = str(v).strip()
    if s.lower() == "true":
        return True
    if s.lower() == "false":
        return False
    try:
        f = float(s)
        return int(f) if f.is_integer() else f
    except ValueError:
        return s


def apply_digitize_update(data):
    name = data.get("name")
    if not name:
        return False, "no name"
    digitize = data.get("digitize")
    if not isinstance(digitize, dict):
        return False, "no digitize data"
    try:
        cfg = config.load_yaml()
    except Exception as e:
        return False, str(e)
    obj = _find_object(cfg, name)
    if obj is None:
        return False, "object not found: " + str(name)

    out = {}
    for k, v in digitize.items():
        c = _coerce(v)
        if c is not None:
            out[k] = c

    if out:
        obj["DIGITIZE"] = out
    else:
        obj.pop("DIGITIZE", None)
    try:
        config.save_yaml(cfg)
        return True, ""
    except Exception as e:
        return False, str(e)
