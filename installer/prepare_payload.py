"""Build a runtime-only public package from an explicit allow-list."""
from pathlib import Path
import hashlib
import importlib.metadata
import json
import shutil
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / 'installer'
CONFIG = json.loads((HERE / 'release.json').read_text(encoding='utf-8'))
NATIVE = ROOT / 'work/native-assistant'
STAGE = HERE / 'staging'
OUT = ROOT / 'outputs/public'


def sha(path):
    return hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()


def main():
    dll = NATIVE / 'bin/AchievementAssistant.dll'
    if sha(dll) != CONFIG['dll_sha256']:
        raise SystemExit('DLL changed: review and update installer/release.json before publishing.')
    if STAGE.exists():
        assert STAGE.resolve().parent == HERE.resolve() and not STAGE.is_symlink()
        shutil.rmtree(STAGE)
    plugins = STAGE / 'bin/win_x64/plugins'
    plugins.mkdir(parents=True)
    shutil.copy2(dll, plugins / dll.name)
    worker = NATIVE / 'bin/AchievementAssistant'
    assert (worker / 'AADataWorker.exe').is_file()
    shutil.copytree(worker, plugins / 'AchievementAssistant')
    (plugins / 'map-probe.json').write_text('{"enabled":true,"highlight_viewpoints":true}\n', encoding='utf-8')
    licenses = STAGE / 'licenses'
    licenses.mkdir()
    for relative, name in [('scs_sdk/sdk_license.txt', 'SCS-SDK.txt'), ('minhook/LICENSE.txt', 'MinHook.txt'), ('imgui/LICENSE.txt', 'Dear-ImGui.txt'), ('json/LICENSE.MIT', 'nlohmann-json.txt')]:
        shutil.copy2(NATIVE / 'third_party' / relative, licenses / name)
    for package in ['pyinstaller', 'cryptography', 'cffi']:
        dist = importlib.metadata.distribution(package)
        for file in dist.files or []:
            if file.name.upper().startswith(('LICENSE', 'COPYING')) and dist.locate_file(file).is_file():
                shutil.copy2(dist.locate_file(file), licenses / (package + '-' + file.name))
    shutil.copy2(Path(sys.base_prefix) / 'LICENSE.txt', licenses / 'Python-PSF.txt')
    shutil.copy2(ROOT / 'publication/public/README.md', STAGE / 'README_RU.md')
    shutil.copy2(ROOT / 'publication/public/RELEASE_NOTES_RU.md', STAGE / 'RELEASE_NOTES_RU.md')
    paths = sorted(p for p in STAGE.rglob('*') if p.is_file())
    forbidden_names = {'preferences.json', 'state.json', 'control.json', 'error.json', 'AA_plugins.zip'}
    forbidden_suffixes = {'.cpp', '.h', '.asm', '.ps1', '.pbo', '.py', '.zip', '.log', '.sii'}
    for path in paths:
        runtime_library = path.relative_to(STAGE).as_posix() == 'bin/win_x64/plugins/AchievementAssistant/_internal/base_library.zip'
        assert path.name not in forbidden_names and (path.suffix.lower() not in forbidden_suffixes or runtime_library), path
        assert not {'sessions', 'player-data', 'source', '__pycache__'} & set(path.parts), path
    manifest = {p.relative_to(STAGE).as_posix(): sha(p) for p in paths}
    (STAGE / 'manifest-sha256.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    OUT.mkdir(parents=True, exist_ok=True)
    archive = OUT / f"ETS2-Achievement-Assistant-Portable-{CONFIG['version']}.zip"
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
        for path in sorted(p for p in STAGE.rglob('*') if p.is_file()):
            bundle.write(path, path.relative_to(STAGE).as_posix())
    with zipfile.ZipFile(archive) as bundle:
        assert bundle.testzip() is None
    print(json.dumps({'archive': str(archive), 'sha256': sha(archive), 'files': len(manifest)}, indent=2))


if __name__ == '__main__':
    main()
