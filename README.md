# Truck Achievement Assistant — исходники

Закрытый репозиторий помощника достижений для ETS2 и ATS. Публичные установщики и инструкции: https://github.com/Rammstik/truck-achievement-assistant

Текущая сборка ETS2: **0.6.15**, Windows x64, DirectX 11, ETS2 **1.62.1.0**. ATS 0.1.0 поддерживает прежнюю 1.61.3.18; в текущий установщик не включён.

## Структура

- `work/native-assistant`: DLL, сборка, тесты, отдельные привязки игр.
- `work/achievement-core`: общий обработчик сохранений, Steam и условий достижений.
- `work/achievement-lab`: проверки правил и фильтров.
- `installer`: установщик ETS2 и проверка его установки/удаления.
- `publication/public`: только публичная документация; исходники в этот репозиторий не копируются.

Сохранения, настройки пользователей, исследовательские дампы, локальная история и готовые сборки исключены из Git. Исходники проекта не публикуются под открытой лицензией. Лицензии зависимостей находятся рядом с ними.

## Сборка

Нужны Windows x64, Visual Studio Build Tools с C++ x64 и Windows SDK, PowerShell 7, Python 3.11 с PyInstaller, cryptography и cffi. Для установщика нужен Inno Setup 7.

```powershell
./work/native-assistant/build.ps1 -Game ets2
./work/native-assistant/build_worker.ps1
python ./installer/prepare_payload.py
./installer/build.ps1 -Compiler 'C:/Program Files/Inno Setup 7/ISCC.exe'
```

`prepare_payload.py` берёт DLL и worker из `work/native-assistant/bin`, а лицензии — из зависимостей и установленного Python. Публичный архив создаётся из явного списка файлов; старые ZIP в `outputs` с исходниками не публикуются.

Исполняемый файл игры проверяется по точному SHA-256. Обновление привязок требует анализа новой версии и проверки сигнатур; отключать эту защиту нельзя. Проверки в настоящей игре проводит владелец проекта.
