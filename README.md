# Atlas Studio

**Atlas Studio** — отдельная программа на C++17 и Qt 5.15 для создания расширений Atlas Code для Atlas Launcher. Она не является частью процесса установки Minecraft и не требует установленного компилятора C++ у автора готового расширения. Программа создаёт проект, редактирует метаданные и Atlas Code, компилирует его в **ATBC 2** и собирает устанавливаемый пакет `.atp`.

> ATBC 2 — это бинарный формат данных Atlas Runtime, а не DLL и не машинный код. Готовый `.atp` содержит только `manifest.json` и `program/main.atbc`; исходный файл `.atlas` в архив не копируется. Бинарный формат не следует рассматривать как средство защиты интеллектуальной собственности: его назначение — воспроизводимая доставка ограниченной программы и проверка её целостности.

| Компонент | Назначение | Результат |
|---|---|---|
| `AtlasStudio.exe` | Визуальная IDE: создание проекта, форма метаданных, редактор и упаковка | `build/main.atbc` и `dist/<id>-<version>.atp` |
| `AtlasCompiler.exe` | Консольная компиляция одного `.atlas` с явными метаданными | Один файл ATBC 2 |
| `atlas_studio_services` | Общая библиотека компилятора и упаковщика schema 2 | Используется обеими программами |

## Что умеет Atlas Studio 0.1.0

IDE создаёт и открывает проекты Atlas Code, сохраняет метаданные плагина и исходный код, отображает диагностику с номерами строк и выполняет бинарную компиляцию. Форма проекта содержит ID, название, версию, автора, описание, категорию, минимальную версию Launcher, сайт, разрешения, декларации страниц UI и декларации действий Runtime.

Раздел **«Страницы UI»** является белым списком ID вкладок, которые программа может создать через `ui.page.create`. Раздел **«Действия Runtime»** задаёт белый список действий доверенных модулей Runtime, на которые Atlas Code вправе ссылаться. Поля принимают уникальные ID через запятую; для страниц доступно не более 16 значений, для действий — не более 64. Это устраняет ситуацию, когда синтаксически корректный скрипт пытается создать незадекларированную вкладку и отклоняется Runtime.

| Ограничение | Значение |
|---|---:|
| Исходный файл Atlas Code | До 128 КиБ |
| Скомпилированный ATBC 2 | До 256 КиБ |
| Команд на событие | До 64 |
| Страниц UI в пакете | До 16 |
| Действий Runtime в пакете | До 64 |
| Разрешённые permissions | `servers.control`, `servers.console`, `files.plugin-data`, `network.metadata` |

## Быстрый старт в IDE

Запустите `AtlasStudio.exe`, выберите **Файл → Новый проект…** и укажите каталог проекта. Studio создаст каталоги `src`, `build` и `dist`, запишет `atlas-project.json` и готовый стартовый `src/main.atlas`. Стартовый пример уже содержит `welcome` в поле **«Страницы UI»**, поэтому его вызов `ui.page.create id=welcome` согласован с манифестом пакета.

Заполните обязательные поля, внесите изменения в код и нажмите **«Собрать .atp»**. IDE сначала сохранит проект, затем создаст `build/main.atbc`, проверит целостность и сформирует пакет в каталоге `dist`. Установите результат из Atlas Launcher через **Плагины → Установить .atp**, включите расширение и перезапустите Launcher.

```atlas
on launcher.started
  call ui.page.create id=welcome title="Hello Atlas"
  call ui.control.add id=welcome type=label text="Плагин создан в Atlas Studio"
end
```

## Структура проекта

```text
my-plugin/
├── atlas-project.json
├── src/
│   └── main.atlas
├── build/
│   └── main.atbc
└── dist/
    └── org.example.my-plugin-1.0.0.atp
```

`atlas-project.json` — редактируемый файл проекта. IDE автоматически создаёт и обновляет его; поле `source` указывает на `src/main.atlas`. Поля `pages` и `actions` должны соответствовать вызовам программы. При упаковке в `.atp` записываются только нормализованный `manifest.json` и скомпилированный `program/main.atbc`.

```json
{
  "formatVersion": 1,
  "id": "org.example.my-plugin",
  "name": "My Plugin",
  "version": "1.0.0",
  "publisher": "Example Author",
  "description": "Example Atlas Code extension.",
  "minimumLauncherVersion": "0.4.0",
  "permissions": ["files.plugin-data"],
  "source": "src/main.atlas",
  "entryPoint": "program/main.atbc",
  "pages": ["welcome"],
  "actions": []
}
```

## AtlasCompiler

`AtlasCompiler.exe` удобен для автоматизированной сборки ATBC 2. Он не создаёт `.atp`: упаковка и генерация `manifest.json` выполняются Atlas Studio либо AtlasPackager из комплекта Launcher. Все метаданные передаются явно, а компилятор записывает их в ATBC для последующей проверки согласованности.

```text
AtlasCompiler.exe \
  --id org.example.my-plugin \
  --name "My Plugin" \
  --version 1.0.0 \
  --publisher "Example Author" \
  --description "Example Atlas Code extension" \
  --permissions files.plugin-data \
  --min-launcher 0.4.0 \
  src\main.atlas build\main.atbc
```

| Параметр | Обязателен | Описание |
|---|---:|---|
| `--id` | Да | ID: строчные латинские буквы, цифры, точки и дефисы; от 3 до 128 символов |
| `--name` | Да | Отображаемое название программы |
| `--version` | Да | Версия в формате SemVer, например `1.0.0` |
| `--publisher` | Да | Автор или издатель |
| `--description` | Да | Краткое назначение расширения |
| `--permissions` | Нет | Список разрешений через запятую |
| `--min-launcher` | Да | Минимальная версия Atlas Launcher в SemVer |
| `input.atlas` | Да | Исходный файл Atlas Code |
| `output.atbc` | Да | Путь целевого бинарного ATBC 2 |

## Граница безопасности

Atlas Code намеренно не предоставляет доступ к C++, DLL, памяти процесса, командной строке, произвольным путям, произвольной сети или Qt-указателям. Программа реагирует только на поддерживаемые события (`launcher.started`, `game.started`, `game.exited`) и направляет вызовы через ABI-шлюзы Atlas Runtime. Окончательное решение о разрешениях принимает Atlas Launcher во время установки и исполнения; метаданные ATBC не повышают полномочия пакета.

> Не помещайте в проект или `.atp` пароли, токены CurseForge, ключи API, JAR-файлы или DLL. Atlas Code-пакет schema 2 не предназначен для нативных модулей.

## Сборка из исходников

Для нативной сборки необходимы CMake 3.16+, C++17 и Qt 5.15 с компонентами Core, Gui, Widgets и Test.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

После сборки получаются `AtlasStudio` и `AtlasCompiler`. В Windows непрерывная интеграция собирает те же исполняемые файлы с Qt 5.15.2 / MinGW 8.1, выполняет CTest, упаковывает переносимый ZIP и публикует SHA-256 как артефакты задания.

## Совместимость

Atlas Studio 0.1.0 создаёт пакеты **schema 2** для Atlas Launcher 0.4.0 и новее. Пакеты native-qt с DLL используют отдельный `schemaVersion: 1` путь и не создаются этой IDE.

## Лицензия

Исходный код проекта распространяется по лицензии MIT; текст лицензии находится в файле `LICENSE`.
