"""Exercise the real installer in an isolated game folder, never the installed game."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import time
import uuid

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
CONFIG = json.loads((HERE / 'release.json').read_text())
SETUP = HERE / 'test-results/TEST-ONLY-ETS2-Assistant-Setup.exe'
REAL_EXE = Path(os.environ.get('AA_TEST_GAME_EXE', r'F:\SteamLibrary\steamapps\common\Euro Truck Simulator 2\bin\win_x64\eurotrucks2.exe'))


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(exe, *args):
    return subprocess.run([str(exe), *map(str, args)], creationflags=subprocess.CREATE_NO_WINDOW, timeout=90, check=False).returncode


def main():
    assert sha(REAL_EXE) == CONFIG['game_sha256'], 'Test requires the supported game executable (not distributed).'
    area = HERE / 'test-area' / ('run-' + uuid.uuid4().hex)
    area.mkdir(parents=True)
    assert area.resolve().is_relative_to((HERE / 'test-area').resolve())
    game = area / 'Тестовая игра с пробелами'
    plugins = game / 'bin/win_x64/plugins'
    plugins.mkdir(parents=True)
    exe = plugins.parent / 'eurotrucks2.exe'
    sentinel = plugins / 'OtherPlugin.dll'
    sentinel.write_bytes(b'UNRELATED PLUGIN')
    settings = Path(os.environ['LOCALAPPDATA']) / 'ETS2AchievementAssistant/preferences.json'
    original_settings = sha(settings) if settings.exists() else None

    def install(label):
        return run(SETUP, '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/LANG=russian', '/DIR=' + str(game), '/LOG=' + str(area / (label + '.log')))

    assert install('missing-exe') != 0
    assert not (plugins / 'AchievementAssistant.dll').exists()
    exe.write_bytes(b'not the supported game')
    assert install('unsupported-exe') != 0
    assert not (plugins / 'AchievementAssistant.dll').exists()
    shutil.copy2(REAL_EXE, exe)

    # A separate sleeping process tests the running-game guard without launching ETS2.
    fixture_source = area / 'GameRunningFixture.cs'
    fixture_source.write_text('class Program { static void Main() { System.Threading.Thread.Sleep(60000); } }')
    fixture_exe = area / 'eurotrucks2.exe'
    compiler = Path(os.environ.get('AA_TEST_CSC', r'G:\Codex\tools\BuildTools\MSBuild\Current\Bin\Roslyn\csc.exe'))
    assert run(compiler, '/nologo', '/out:' + str(fixture_exe), str(fixture_source)) == 0
    fixture = subprocess.Popen([str(fixture_exe)], creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        time.sleep(0.3)
        assert fixture.poll() is None
        assert install('running-guard') != 0
        assert not (plugins / 'AchievementAssistant.dll').exists()
    finally:
        fixture.terminate()
        fixture.wait(timeout=10)

    probe = plugins / 'map-probe.json'
    probe.write_text('{"existing_user_option":true}', encoding='utf-8')
    assert install('install') == 0
    dll = plugins / 'AchievementAssistant.dll'
    assert sha(dll) == CONFIG['dll_sha256']
    assert (plugins / 'AchievementAssistant/AADataWorker.exe').exists()
    assert sentinel.read_bytes() == b'UNRELATED PLUGIN'
    assert probe.read_text() == '{"existing_user_option":true}'
    extra = plugins / 'AchievementAssistant/user-added.txt'
    extra.write_text('preserve unknown files')
    dll.write_bytes(b'old version')
    assert install('update') == 0
    assert sha(dll) == CONFIG['dll_sha256']
    assert extra.read_text() == 'preserve unknown files'
    assert run(game / '.assistant-uninstall/unins000.exe', '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/LOG=' + str(area / 'uninstall.log')) == 0
    assert not dll.exists()
    assert not (plugins / 'AchievementAssistant/AADataWorker.exe').exists()
    assert sentinel.read_bytes() == b'UNRELATED PLUGIN'
    assert extra.read_text() == 'preserve unknown files'
    assert probe.read_text() == '{"existing_user_option":true}'
    assert sha(exe) == CONFIG['game_sha256']
    assert (sha(settings) if settings.exists() else None) == original_settings
    result = {'passed': True, 'test_folder': str(area), 'checks': ['missing game rejected', 'unsupported hash rejected', 'running-game fixture rejected', 'install in Unicode path', 'update replaces own DLL', 'uninstall removes own runtime', 'other plugins and unknown files preserved', 'existing map configuration preserved', 'real user settings unchanged', 'game executable unchanged'], 'actual_game_started': False, 'test_build_has_no_uninstall_registration': True}
    (HERE / 'test-results/installer-validation.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
