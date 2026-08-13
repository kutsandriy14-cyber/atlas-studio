# Сведения о toolchain Windows CI

Windows-сборка Atlas Studio использует Qt 5.15.2 `win64_mingw81`. Для совместимости с его POSIX/SEH ABI в workflow закреплён пакет Chocolatey `mingw` версии `8.1.0`; использование текущего UCRT toolchain из того же пакета вызывало аварийное завершение package-теста до вывода QtTest.

Доступность версии `8.1.0` подтверждена официальным OData-каталогом Chocolatey: <https://community.chocolatey.org/api/v2/FindPackagesById()?id='mingw'>.

В качестве независимого архивного источника существует MinGW-w64 `x86_64-8.1.0-release-posix-seh-rt_v6-rev0`: <https://sourceforge.net/projects/mingw-w64/files/Toolchains%20targetting%20Win64/Personal%20Builds/mingw-builds/8.1.0/threads-posix/seh/x86_64-8.1.0-release-posix-seh-rt_v6-rev0.7z/download>.

Для возможного перехода на GitHub Action проверена документация `egor-tensin/setup-mingw`: <https://github.com/egor-tensin/setup-mingw>. Действие принимает параметр `version`, однако текущий workflow оставлен на прямом закреплении версии Chocolatey, чтобы сохранить существующие пути к runtime DLL.
