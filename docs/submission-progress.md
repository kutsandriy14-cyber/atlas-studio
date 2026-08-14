# Статус: отправка плагина в каталог (фаза 11)

## Сделано:
1. Создан публичный репозиторий kutsandriy14-cyber/atlas-plugin-submissions (README.md + SUBMISSION_GUIDE.md зафиксированы).
2. Studio: добавлен пункт меню «Сборка → Отправить в каталог…», реализация submitToCatalog() в atlas_studio_window.cpp (диалог, создание GitHub Issue с именем/версией/id/автором/описанием/SHA-256, ссылка на репо atlas-plugin-submissions, требование положить plugin.atp + исходники в submissions/<author>/<plugin-id>/).
3. Добавлены include: QDialogButtonBox, QEventLoop, QNetworkAccessManager/Reply/Request, QImageReader.
4. CMakeLists.txt Studio: версия проекта 0.2.0, main.cpp kStudioVersion="0.2.0", все тесты с offscreen на UNIX (atlas_compiler_help, atlas_updater_help, atlas_studio_ui_test, atlas_studio_submit_test).
5. Тест tests/atlas_studio_submit_test.cpp — проверяет наличие и включённость пункта «Отправить» в меню Файл/Сборка.

## РЕШЕНО:
Тест проходит (findChildren<QMenu*> вместо menuBar->actions() — menuBar actions пустые без menu() на этой Qt-конфигурации). Все 5 тестов Studio проходят. Функция submitToCatalog добавлена: Сборка → Отправить в каталог…, создаёт GitHub Issue в kutsandriy14-cyber/atlas-plugin-submissions с именем/версией/id/автором/описанием/SHA-256; требование исходников в submissions/<author>/<id>/. Версия Studio 0.2.0.

Осталось: убрать debug-остатки из теста (нет), фиксация Studio 0.2.0 + push + Windows CI; фаза 12 (визуал Studio глубже), фаза 13 (релизы).
Launcher 0.7.0: зафиксирован и отправлен, CI запущен.

## Старая проблема (debug) — решена:
- submit-тест падает: menuBar actions count = 0 (меню пустые!).
- setupActions() вызывается в конструкторе AtlasStudioWindow (строка ~243).
- Гипотеза: тест собирался до моих правок, и atlas_studio_window.cpp.o кэширован? Или menuBar() не создан — но addMenu создаёт.
- Нужно: полная пересборка (cmake --build build-native --parallel 4), возможно moc-файл устарел.
- В тесте добавил отладочный вывод count — пересобрать и посмотреть.

## Дальше (после теста):
- Фиксация Studio 0.2.0, запуск Windows CI.
- Фаза 12: улучшить визуал Studio глубже (см. docs/next-phases-plan.md в launcher-source).
- Фаза 13: релизы.

## Launcher 0.7.0: уже зафиксирован и отправлен, CI запущен (run проверять gh run list --repo kutsandriy14-cyber/atlas-launcher).
