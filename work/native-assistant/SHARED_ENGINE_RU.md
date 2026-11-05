# Общий код ETS2 и ATS

Общие исходники: native-assistant/src, worker/data_worker.py и achievement-core/ets2_achievements. GameConfig.h выбирает идентификатор SDK, отдельную папку настроек, название и передаваемый worker параметр --game. Python game_config.py выбирает AppID, папку сохранений и набор определений до импорта модулей.

ATS: src/games/ats с отдельными координатами, правилами и адресами; Python game_data/ats. Для ETS2 сохранены прежние таблицы и идентификаторы профилей. Идентификаторы профилей ATS независимы от расположения Steam. Оба набора включаются в общий AADataWorker; при запуске плагин явно передаёт нужную игру.

Сборка: build.ps1 -Game ats -> bin-ats/build-ats, build.ps1 -Game ets2 -> bin/build. build_worker.ps1 собирает один модуль чтения. Игровые перехваты разрешены только для точного SHA256 каждой игры плюс отдельных сигнатур функций. Текущая ATS 1.61.3.18: c60e8ee02cc97b3cc6ef8c6298bb5c3c23c4ffd330b8185b646fb1b608a3c7b4.

Тесты: ATSCoreTests, ATSMapTintTests, check_ats_tracking.py и общие MapProbeTests/JobProbeTests/InputGateTests; прежние ETS2 OfferFilterTests/MapTintTests и Python проверки пройдены. NativeHost используется только как собственный графический стенд. Запуск внутри игры проверяет пользователь.
