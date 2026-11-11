//==============================================================================
// G4CARE - g4macro.js — G4 macro (.mac) language: Monarch grammar, completion, snippets, hover
//
// G4 macro (.mac) language: Monarch grammar, completion, snippets, hover
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

// [command, En description, Ru description]
const CMDS = [
    // ── Run control ──
    ['/run/initialize', 'Reinitialize geometry, physics and cuts', 'Переинициализировать геометрию, физику и куты'],
    ['/run/beamOn', 'Start a run with N events', 'Запустить прогон из N событий'],
    ['/run/verbose', 'Set run verbosity level', 'Уровень подробности вывода'],
    ['/run/printProgress', 'Print progress every N events', 'Печать прогресса каждые N событий'],
    ['/run/setCut', 'Set default production threshold (cut)', 'Задать порог рождения (кат)'],
    ['/run/setCutForAGivenParticle', 'Set cut for a given particle', 'Задать кат для конкретной частицы'],
    ['/run/particle/dumpList', 'Print the particle table', 'Напечатать таблицу частиц'],
    ['/run/particle/setVerbose', 'Set verbosity for a particle', 'Подробность для частицы'],
    ['/run/activateRegion', 'Activate a region for tracking', 'Активировать регион для трекинга'],
    ['/run/numberOfThreads', 'Set number of worker threads', 'Задать число рабочих потоков'],
    ['/run/terminate', 'Terminate the run loop', 'Завершить цикл прогона'],

    // ── Control ──
    ['/control/execute', 'Execute a macro file', 'Выполнить макрофайл'],
    ['/control/loop', 'Loop a macro with a counter', 'Цикл по макросу со счётчиком'],
    ['/control/foreach', 'Loop over a list of values', 'Цикл по списку значений'],
    ['/control/verbose', 'Set control verbosity', 'Подробность управления'],
    ['/control/alias', 'Define an alias (variable)', 'Определить псевдоним (переменную)'],
    ['/control/unsetAlias', 'Remove an alias', 'Удалить псевдоним'],
    ['/control/divide', 'Divide an alias value', 'Разделить значение псевдонима'],
    ['/control/listAlias', 'List all aliases', 'Список всех псевдонимов'],
    ['/control/manual', 'Browse commands in a directory', 'Просмотр команд каталога'],
    ['/control/maximumStoredHistory', 'Max stored command history', 'Макс. сохраняемая история команд'],
    ['/control/saveHistory', 'Save command history to a file', 'Сохранить историю команд в файл'],

    // ── GPS (General Particle Source) ──
    ['/gps/particle', 'Primary particle (gamma, e-, proton, ion…)', 'Первичная частица (gamma, e-, proton, ion…)'],
    ['/gps/ion', 'Ion with Z, A, Q, excitation energy', 'Ион с Z, A, Q, энергией возбуждения'],
    ['/gps/pos/type', 'Position type (Point, Beam, Plane, Surface, Volume)', 'Тип распределения позиции (Point, Beam, Plane, Surface, Volume)'],
    ['/gps/pos/centre', 'Source centre coordinates', 'Координаты центра источника'],
    ['/gps/pos/shape', 'Shape for surface/volume sources', 'Форма источника (поверхность/объём)'],
    ['/gps/pos/radius', 'Radius of source', 'Радиус источника'],
    ['/gps/pos/halfx', 'Half-size along x', 'Полуразмер по x'],
    ['/gps/pos/halfy', 'Half-size along y', 'Полуразмер по y'],
    ['/gps/pos/halfz', 'Half-size along z', 'Полуразмер по z'],
    ['/gps/pos/rot1', 'First rotation vector of source', 'Первый вектор поворота источника'],
    ['/gps/pos/rot2', 'Second rotation vector of source', 'Второй вектор поворота источника'],
    ['/gps/ang/type', 'Angular distribution (iso, planar, beam1d, beam2d, focused, cos)', 'Тип углового распределения (iso, planar, beam1d, beam2d, focused, cos)'],
    ['/gps/ang/mintheta', 'Minimum polar angle', 'Минимальный полярный угол'],
    ['/gps/ang/maxtheta', 'Maximum polar angle', 'Максимальный полярный угол'],
    ['/gps/ang/minphi', 'Minimum azimuthal angle', 'Минимальный азимутальный угол'],
    ['/gps/ang/maxphi', 'Maximum azimuthal angle', 'Максимальный азимутальный угол'],
    ['/gps/direction', 'Emission direction vector', 'Вектор направления вылета'],
    ['/gps/energy', 'Particle energy (shorthand)', 'Энергия частицы (краткая форма)'],
    ['/gps/position', 'Source position (shorthand)', 'Позиция источника (краткая форма)'],
    ['/gps/ene/type', 'Energy distribution (Mono, Lin, Pow, Exp, Gauss, Brem, Arb)', 'Тип спектра (Mono, Lin, Pow, Exp, Gauss, Brem, Arb)'],
    ['/gps/ene/mono', 'Monoenergetic energy', 'Моноэнергия'],
    ['/gps/ene/min', 'Minimum energy of spectrum', 'Минимальная энергия спектра'],
    ['/gps/ene/max', 'Maximum energy of spectrum', 'Максимальная энергия спектра'],
    ['/gps/ene/sigma', 'Gaussian sigma (Gauss spectrum)', 'Сигма гауссиана (спектр Gauss)'],
    ['/gps/ene/alpha', 'Alpha parameter (Pow / Brem)', 'Параметр альфа (Pow / Brem)'],
    ['/gps/number', 'Number of particles per event', 'Число частиц на событие'],
    ['/gps/verbose', 'GPS verbosity', 'Подробность GPS'],
    ['/gps/source/add', 'Add a GPS source', 'Добавить GPS-источник'],
    ['/gps/source/list', 'List GPS sources', 'Список GPS-источников'],
    ['/gps/source/clear', 'Clear all GPS sources', 'Очистить все GPS-источники'],
    ['/gps/source/multipleVertex', 'Allow multiple primary vertices', 'Разрешить несколько первичных вершин'],

    // ── Particle gun ──
    ['/gun/particle', 'Gun particle', 'Частица «пушки»'],
    ['/gun/energy', 'Gun kinetic energy', 'Кинетическая энергия «пушки»'],
    ['/gun/position', 'Gun position', 'Позиция «пушки»'],
    ['/gun/direction', 'Gun direction vector', 'Вектор направления «пушки»'],
    ['/gun/number', 'Particles per event', 'Число частиц на событие'],

    // ── Tracking ──
    ['/tracking/storeTrajectory', 'Store trajectories (0/1/2)', 'Сохранять траектории (0/1/2)'],
    ['/tracking/verbose', 'Tracking verbosity', 'Подробность трекинга'],

    // ── Physics processes ──
    ['/process/verbose', 'Physics process verbosity', 'Подробность процессов'],
    ['/process/activate', 'Activate a process', 'Активировать процесс'],
    ['/process/inactivate', 'Deactivate a process', 'Деактивировать процесс'],
    ['/process/list', 'List registered processes', 'Список зарегистрированных процессов'],
    ['/process/chem/TimeStepModel', 'Chemistry time-step model (SBS, IRT, IRT_syn)', 'Модель шага химии (SBS, IRT, IRT_syn)'],
    ['/process/dna/e-SolvationSubType', 'Electron solvation subtype', 'Подтип сольватации электрона'],

    // ── Visualization ──
    ['/vis/open', 'Open a visualization driver (OGL, VRML2FILE, HepRepFile…)', 'Открыть драйвер визуализации (OGL, VRML2FILE, HepRepFile…)'],
    ['/vis/drawVolume', 'Draw the geometry volume', 'Нарисовать геометрию'],
    ['/vis/enable', 'Enable visualization', 'Включить визуализацию'],
    ['/vis/disable', 'Disable visualization', 'Выключить визуализацию'],
    ['/vis/verbose', 'Visualization verbosity', 'Подробность визуализации'],
    ['/vis/viewer/set/background', 'Background colour (r g b)', 'Цвет фона (r g b)'],
    ['/vis/viewer/set/viewpointThetaPhi', 'View angle (theta phi)', 'Угол обзора (theta phi)'],
    ['/vis/viewer/set/viewpointVector', 'View direction vector', 'Вектор направления обзора'],
    ['/vis/viewer/set/autoRefresh', 'Auto refresh on/off', 'Автообновление вкл/выкл'],
    ['/vis/viewer/set/style', 'View style (surface, wireframe)', 'Стиль отображения (surface, wireframe)'],
    ['/vis/viewer/set/auxiliaryEdge', 'Auxiliary edges on/off', 'Вспомогательные рёбра вкл/выкл'],
    ['/vis/viewer/set/lineSegmentsPerCircle', 'Line segments per circle', 'Число сегментов на окружность'],
    ['/vis/viewer/set/targetPoint', 'Rotation target point', 'Точка вращения'],
    ['/vis/viewer/zoom', 'Zoom factor', 'Масштаб'],
    ['/vis/viewer/zoomTo', 'Zoom to a factor', 'Масштаб к заданному фактору'],
    ['/vis/viewer/flush', 'Flush viewer', 'Сбросить буфер просмотра'],
    ['/vis/viewer/update', 'Update viewer', 'Обновить просмотр'],
    ['/vis/scene/add/axes', 'Add axes decoration', 'Добавить оси'],
    ['/vis/scene/add/scale', 'Add scale decoration', 'Добавить масштабную линейку'],
    ['/vis/scene/add/text', 'Add text decoration', 'Добавить текст'],
    ['/vis/scene/add/trajectories', 'Add trajectories (smooth, rich)', 'Добавить траектории (smooth, rich)'],
    ['/vis/scene/endOfEventAction', 'End-of-event action (accumulate, refresh)', 'Действие в конце события (accumulate, refresh)'],
    ['/vis/geometry/set/colour', 'Set volume colour', 'Задать цвет объёма'],
    ['/vis/geometry/set/visibility', 'Set volume visibility', 'Задать видимость объёма'],
    ['/vis/set/textColour', 'Text colour', 'Цвет текста'],
    ['/vis/set/textLayout', 'Text layout (left, right, centre)', 'Выравнивание текста (left, right, centre)'],
    ['/vis/set/colour', 'Default colour', 'Цвет по умолчанию'],
    ['/vis/set/lineWidth', 'Default line width', 'Толщина линии по умолчанию'],
    ['/vis/modeling/trajectories/create/drawByParticleID', 'Colour trajectories by particle ID', 'Цвет траекторий по ID частицы'],
    ['/vis/modeling/trajectories/create/drawByCharge', 'Colour trajectories by charge', 'Цвет траекторий по заряду'],
    ['/vis/filtering/trajectories/create/particleFilter', 'Filter trajectories by particle', 'Фильтр траекторий по частице'],

    // ── Analysis ──
    ['/analysis/setFileName', 'Set output file name', 'Задать имя выходного файла'],
    ['/analysis/h1/set', 'Configure a 1D histogram', 'Настроить 1D гистограмму'],
    ['/analysis/h2/set', 'Configure a 2D histogram', 'Настроить 2D гистограмму'],
    ['/analysis/h1/setAscii', 'Enable ASCII output for h1', 'Включить ASCII-вывод для h1'],

    // ── Materials / random / units / cuts / geometry ──
    ['/material/nist/listMaterials', 'List NIST materials', 'Список материалов NIST'],
    ['/random/setSeeds', 'Set random number seeds', 'Задать сиды генератора'],
    ['/random/resetEngineFrom', 'Reset engine from a state file', 'Сбросить генератор из файла состояния'],
    ['/random/setEngineName', 'Set random engine name', 'Задать имя генератора'],
    ['/units/list', 'List available units', 'Список доступных единиц'],
    ['/cuts/setWorld', 'Set world production cut', 'Задать кат для мира'],
    ['/geometry/test/run', 'Test geometry for overlaps', 'Проверить геометрию на перекрытия'],

    // ── G4CARE chemistry (custom) ──
    ['/chem/chemistry/addMolecule', 'Add a chemical species/molecule', 'Добавить химическую частицу/молекулу'],
    ['/chem/diffusion/temperature', 'Set diffusion temperature', 'Задать температуру диффузии'],
    ['/chem/diffusion/pressure', 'Set diffusion pressure', 'Задать давление диффузии'],
    ['/chem/diffusion/density', 'Set diffusion density', 'Задать плотность диффузии'],
    ['/chem/time/start', 'Chemistry start time', 'Время старта химии'],
    ['/chem/time/end', 'Chemistry end time', 'Время окончания химии'],
    ['/chem/reaction/modify', 'Modify a reaction rate', 'Изменить константу реакции'],
    ['/chem/reaction/UI', 'Chemistry reactions UI/verbosity', 'Управление/подробность реакций'],
    ['/chem/species', 'List/select chemical species', 'Виды химических частиц'],
    ['/chem/stage/activate', 'Activate a chemistry stage', 'Активировать этап химии'],

    // ── More physics processes (radioactive decay, EM) ──
    ['/process/em/nuclearStopping', 'Enable nuclear stopping', 'Включить ядерное торможение'],
    ['/process/had/rdm/applyARM', 'Enable atomic relaxation (ARM)', 'Включить атомную релаксацию'],
    ['/process/had/rdm/selectAll', 'Select all radioactive decay modes', 'Выбрать все моды распада'],
    ['/process/had/rdm/setPhotoEvaporation', 'Photon evaporation on/off', 'Фотоиспарение вкл/выкл'],
    ['/process/had/rdm/thresholdForVeryLongDecay', 'Threshold for very long decays', 'Порог очень долгих распадов'],
    ['/process/had/rdm/thresholdForVeryLongDecayTime', 'Threshold time for decay', 'Пороговое время распада'],
    ['/process/had/rdm/verbose', 'RDM verbosity', 'Подробность RDM'],

    // ── Scheduler / scoring mesh / control echo ──
    ['/scheduler/verbose', 'Scheduler verbosity', 'Подробность планировщика'],
    ['/score/close', 'Close the scoring mesh', 'Закрыть сетку скоринга'],
    ['/score/create/boxMesh', 'Create a box scoring mesh', 'Создать сетку-параллелепипед'],
    ['/score/mesh/boxSize', 'Set mesh box size', 'Задать размер сетки'],
    ['/score/mesh/nBin', 'Number of bins per axis', 'Число бинов по осям'],
    ['/score/mesh/translate/xyz', 'Translate the mesh', 'Сдвинуть сетку'],
    ['/score/quantity/cellFlux', 'Score cell flux', 'Считать флюенс в ячейках'],
    ['/control/echo', 'Print text to output', 'Вывести текст в журнал'],

    // ── Extra visualization (lights, time window, trajectory model, VTK) ──
    ['/vis/viewer/set/lightsVector', 'Set lights direction', 'Направление источника света'],
    ['/vis/viewer/set/timeWindow/displayHeadTime', 'Time-window head time display', 'Отображение головы временного окна'],
    ['/vis/viewer/set/timeWindow/fadeFactor', 'Time-window fade factor', 'Коэффициент затухания временного окна'],
    ['/vis/viewer/set/timeWindow/startTime', 'Time-window start time', 'Стартовое время временного окна'],
    ['/vis/modeling/trajectories/drawByParticleID-0/default/setDrawStepPts', 'Draw step points (model index 0)', 'Рисовать точки шагов (индекс модели 0)'],
    ['/vis/modeling/trajectories/drawByParticleID-0/default/setStepPtsSize', 'Step point size', 'Размер точек шагов'],
    ['/vis/modeling/trajectories/drawByParticleID-0/default/setTimeSliceInterval', 'Time slice interval', 'Интервал временного среза'],
    ['/vis/modeling/trajectories/drawByParticleID-0/set', 'Set particle colour in trajectory model', 'Цвет частицы в модели траекторий'],
    ['/vis/scene/add/volume', 'Add a volume to the scene', 'Добавить объём в сцену'],
    ['/vis/scene/create', 'Create a scene handler', 'Создать обработчик сцены'],
    ['/vis/sceneHandler/attach', 'Attach a scene handler', 'Прикрепить обработчик сцены'],
    ['/vis/vtk/export', 'Export VTK from the viewer', 'Экспорт VTK из вьювера'],
    ['/geometry/test/recursive_test', 'Recursive geometry test', 'Рекурсивная проверка геометрии'],
];

// [prefix, label, En/Ru description, body lines]
const SNIPPETS = [
    ['gps', 'GPS source (snippet)', 'En: Full General Particle Source block.\nRu: Полный блок GPS-источника.', [
        '/gps/particle ${1:gamma}',
        '/gps/pos/type ${2:Point}',
        '/gps/pos/centre ${3:0} ${4:0} ${5:0} ${6:mm}',
        '/gps/ang/type ${7:iso}',
        '/gps/ene/type ${8:Mono}',
        '/gps/ene/mono ${9:1} ${10:MeV}',
        '/run/beamOn ${11:100}',
    ]],
    ['run', 'run (snippet)', 'En: Initialize and start a run.\nRu: Инициализация и запуск прогона.', [
        '/run/initialize',
        '/run/beamOn ${1:10}',
    ]],
    ['vis', 'vis init (snippet)', 'En: Minimal visualization setup.\nRu: Минимальная настройка визуализации.', [
        '/vis/open ${1:OGL}',
        '/vis/drawVolume',
        '/vis/scene/add/trajectories smooth',
        '/vis/viewer/set/viewpointThetaPhi ${2:45} ${3:45}',
        '/run/beamOn ${4:10}',
    ]],
    ['loop', 'control/loop (snippet)', 'En: Loop a macro with a counter.\nRu: Цикл по макросу со счётчиком.', [
        '/control/loop ${1:file}.loop ${2:counter} ${3:0} ${4:10} ${5:1}',
    ]],
];

function registerMonarch(monaco) {
    monaco.languages.setMonarchTokensProvider('g4macro', {
        defaultToken: '',
        ignoreCase: false,
        tokenizer: {
            root: [
                [/#.*$/, 'comment'],
                [/\/[\w./+\-]+/, 'keyword'],
                [/"[^"]*"/, 'string'],
                [/[\(\)\[\]\{\}]/, 'delimiter.bracket'],
                [/\b(true|false)\b/, 'keyword.constant'],
                [/-?\d*\.?\d+([eE][+\-]?\d+)?/, 'number'],
                [/\b(mm|cm|m|um|nm|pm|km|deg|rad|mrad|sr|s|ms|us|ns|ps|fs|eV|keV|MeV|GeV|TeV|Bq|kBq|MBq|GBq|Gy|gray)\b/, 'type'],
                [/(gamma|proton|neutron|alpha|deuteron|triton|mu\+|mu-|pi0|pi\+|pi-|e\+|e-|opticalphoton|geantino|ion)(?=\s|$)/, 'type'],
            ],
        },
    });
}

function registerCompletion(monaco) {
    monaco.languages.registerCompletionItemProvider('g4macro', {
        triggerCharacters: ['/'],
        provideCompletionItems(model, position) {
            const lineText = model.getLineContent(position.lineNumber);
            const before = lineText.slice(0, position.column - 1);
            if (before.indexOf('#') !== -1) return { suggestions: [] };
            const m = /(\S*)$/.exec(before);
            const prefix = m ? m[1] : '';
            const startCol = position.column - prefix.length;
            const range = new monaco.Range(position.lineNumber, startCol, position.lineNumber, position.column);
            const suggestions = [];
            if (prefix.startsWith('/') || prefix === '') {
                for (const [cmd, en, ru] of CMDS) {
                    if (prefix === '' || cmd.startsWith(prefix)) {
                        suggestions.push({
                            label: cmd,
                            kind: monaco.languages.CompletionItemKind.Method,
                            detail: 'G4 command',
                            documentation: { value: '**' + cmd + '**\n\nEn: ' + en + '\n\nRu: ' + ru },
                            insertText: cmd,
                            filterText: cmd.slice(1),
                            sortText: '0' + cmd,
                            range,
                        });
                    }
                }
            }
            for (const [pfx, label, doc, body] of SNIPPETS) {
                if (prefix === '' || pfx.startsWith(prefix)) {
                    suggestions.push({
                        label: label,
                        kind: monaco.languages.CompletionItemKind.Snippet,
                        detail: 'Snippet',
                        documentation: { value: doc },
                        insertText: body.join('\n'),
                        insertTextRules: monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet,
                        filterText: pfx,
                        sortText: '1' + pfx,
                        range,
                    });
                }
            }
            return { suggestions };
        },
    });
}

function registerHover(monaco) {
    const map = new Map(CMDS.map(([c, en, ru]) => [c, 'En: ' + en + '\n\nRu: ' + ru]));
    monaco.languages.registerHoverProvider('g4macro', {
        provideHover(model, position) {
            const line = model.getLineContent(position.lineNumber);
            const idx = position.column - 1;
            if (idx < 0 || idx > line.length) return null;
            if (line.slice(0, idx).indexOf('#') !== -1) return null;
            let s = idx, e = idx;
            while (s > 0 && !/\s/.test(line[s - 1])) s--;
            while (e < line.length && !/\s/.test(line[e])) e++;
            const token = line.slice(s, e);
            if (!token.startsWith('/')) return null;
            if (map.has(token)) {
                return { contents: [{ value: '**' + token + '**\n\n' + map.get(token) }] };
            }
            const subs = CMDS.filter(([c]) => c.startsWith(token) && c !== token);
            if (subs.length) {
                const lines = subs.slice(0, 20).map(([c, en]) => '- `' + c + '` — ' + en);
                return { contents: [{ value: '**' + token + '**\n\nSubcommands / подкоманды:\n' + lines.join('\n') }] };
            }
            return null;
        },
    });
}

// Register the 'g4macro' language (Geant4 / G4CARE macro files).
export function registerG4Macro(monaco) {
    monaco.languages.register({ id: 'g4macro', extensions: ['.mac'] });
    monaco.languages.setLanguageConfiguration('g4macro', {
        comments: { lineComment: '#' },
        brackets: [['{', '}'], ['[', ']'], ['(', ')']],
        autoClosingPairs: [
            { open: '{', close: '}' },
            { open: '[', close: ']' },
            { open: '(', close: ')' },
            { open: '"', close: '"' },
        ],
        surroundingPairs: [
            { open: '{', close: '}' },
            { open: '[', close: ']' },
            { open: '(', close: ')' },
            { open: '"', close: '"' },
        ],
    });
    registerMonarch(monaco);
    registerCompletion(monaco);
    registerHover(monaco);
}
