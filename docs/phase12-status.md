# Phase 12 status (визуальный редизайн Atlas Studio)

## Сделано (сборка проходит, 5/5 тестов зелёные):
1. SVG-иконки в assets/icons/: file-folder, file-atlas, file-atbc, file-atp, file-json, compile, package, upload, book, code, module, error-dot, ok-dot + atlas-mark/catalog/library/play/settings (из Launcher).
2. assets/assets.qrc (AUTORCC ON), Qt5::Svg в find_package, новая библиотека atlas_studio_ui (studio_theme.cpp/.h + atlas_code_highlighter.cpp/.h), подключена к AtlasStudio и обоим GUI-тестам.
3. CI Studio: Qt5Svg.dll в portable, QT_QPA_PLATFORM=offscreen для ctest, версия 0.2.0, art name atlas-studio-0.2.0-windows.
4. StudioTheme: QSvgRenderer raster + кэш, палитра цветов.
5. AtlasCodeHighlighter (QSyntaxHighlighter): ключевые слова, call-ы (ui.page.create), строки, числа, комментарии.
6. UI: brandIcon atlas-mark 22px + бирюзовый ATLAS STUDIO; иконки кнопок commandBar (compile/package/upload 16px, icon-size QSS, padding); explorer title ПРОВОДНИК с module-иконкой; кнопки Новый/Открыть проект с иконками; дерево projectTree с иконками (rebuildProjectTree: module/folder/file-atlas/file-atbc/file-atp/file-json); бейдж проблем (ok-dot/error-dot) на табе "Проблемы и сборка" + иконка book на табе справочника; animateStatusMessage (QPropertyAnimation на background статус-бара); QSS: hover табов, icon-size кнопок, статус-бар.

## Визуальная проверка:
- Скриншоты Xvfb :99: иконки видны в командной строке (бренд, compile, package, upload), в ПРОВОДНИК, в кнопках Новый/Открыть проект. Работает.
- Скриншоты лежат: /tmp/studio_shot3.png, studio_zoom*.png
- Дерево с открытым проектом не проверял (диалог QFileDialog в headless). Методика: xdotool по меню Файл→Новый работает, но диалог выбора каталога — через xdotool вводить путь.

## Осталось:
1. Проверить/зафиксировать изменения (git add: CMakeLists.txt, assets/*, src/studio/*, src/main.cpp, .github/workflows). Коммит "Atlas Studio 0.2.0: полный VS Code-стиль — иконки, подсветка, статус".
2. Проверить CI обоих репо (Launcher 0.7.0 был запущен ранее; Studio новый push запускает CI).
3. Фаза 13: релизы (GitHub release assets + CHANGELOG).
4. Проверка связки: плагин Studio → submit → Issue в kutsandriy14-cyber/atlas-plugin-submissions.

## Ключевые факты:
- Репозитории: kutsandriy14-cyber/atlas-launcher (Launcher 0.7.0 зафиксирован, CI in_progress), kutsandriy14-cyber/atlas-studio (0.2.0), kutsandriy14-cyber/atlas-plugin-submissions (публичный, для заявок).
- Отправка плагинов: Studio → меню Сборка → Отправить в каталог… → GitHub Issue в submissions.
- CI Studio: windows-latest + install-qt-action 5.15.2 mingw81.
- Тесты запуска: QT_QPA_PLATFORM=offscreen ctest --test-dir build-native.
