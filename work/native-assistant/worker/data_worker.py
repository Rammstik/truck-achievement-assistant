"""Hidden offline transport; all achievement rules live in the shared core."""
import argparse,ctypes,json,os,sys,time
from pathlib import Path
from ctypes import wintypes
if not getattr(sys,'frozen',False):
    for candidate in [Path(__file__).resolve().parents[1]/'core',Path(__file__).resolve().parents[2]/'achievement-core']:
        if (candidate/'ets2_achievements').is_dir():
            sys.path.insert(0,str(candidate));break
os.environ['AA_GAME']='ets2'
if '--game' in sys.argv:
    index=sys.argv.index('--game');os.environ['AA_GAME']=sys.argv[index+1]
from ets2_achievements.steam_cache import find_steam
from ets2_achievements.service import snapshot,documents_directory
from ets2_achievements.language import detect_game_language,resolve,translate
def atomic_json(path, value):
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(value, ensure_ascii=False, allow_nan=False), encoding='utf-8')
    os.replace(temp, path)

def read_control(path):
    try:
        if path.stat().st_size > 1024 * 1024:
            return {}
        value = json.loads(path.read_text(encoding='utf-8'))
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError):
        return {}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game',choices=('ets2','ats'),default='ets2')
    parser.add_argument('--session', type=Path, required=True)
    parser.add_argument('--parent', type=int, required=True)
    parser.add_argument('--once', action='store_true')
    parser.add_argument('--steam-dir', type=Path, default=find_steam())
    parser.add_argument('--ets-dir', type=Path, default=documents_directory())
    args = parser.parse_args()
    args.session.mkdir(parents=True, exist_ok=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    parent = kernel.OpenProcess(1048576, False, args.parent)
    if not parent:
        raise OSError('Parent process unavailable')
    last_control = None
    last_event = None
    next_read = 0
    try:
        while kernel.WaitForSingleObject(parent, 0) == 258:
            control = read_control(args.session / 'control.json')
            event = read_control(args.session / 'event.json')
            if control.get('stop'):
                break
            if control != last_control or event != last_event or time.monotonic() >= next_read:
                try:
                    result = snapshot(args.steam_dir, args.ets_dir, control)
                    atomic_json(args.session / 'state.json', result)
                except Exception as exc:
                    language=resolve(control.get('language','auto'),detect_game_language(args.ets_dir))
                    atomic_json(args.session / 'error.json', {'error': translate('Ошибка чтения: ',language)+type(exc).__name__, 'language':language, 'at': time.time()})
                else:
                    (args.session / 'error.json').unlink(missing_ok=True)
                last_control = control
                last_event = event
                next_read = time.monotonic() + 5
                if args.once:
                    break
            time.sleep(0.25)
    finally:
        kernel.CloseHandle(parent)
if __name__ == '__main__':
    main()
