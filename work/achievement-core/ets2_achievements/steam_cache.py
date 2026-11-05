from .game_config import APP_ID
import math,re,struct
from pathlib import Path
from datetime import datetime,timezone
from .steam_kv import binary_kv
def utc(timestamp):
    return datetime.fromtimestamp(timestamp, timezone.utc).isoformat()

def read_stable(path):
    for _ in range(3):
        a = path.stat()
        data = path.read_bytes()
        b = path.stat()
        if a.st_mtime_ns == b.st_mtime_ns and a.st_size == len(data) == b.st_size:
            return (binary_kv(data), utc(b.st_mtime))
    raise ValueError('Steam обновляет файл. Повторите через несколько секунд.')

def localized(value, fallback=''):
    if isinstance(value, dict):
        return value.get('russian') or value.get('english') or fallback
    return value if isinstance(value, str) and value else fallback

def accounts(steam):
    names = {}
    try:
        text = (steam / 'config/loginusers.vdf').read_text(encoding='utf-8-sig')
        for sid, body in re.findall('"(\\d{17})"\\s*\\{([^}]+)\\}', text):
            match = re.search('"PersonaName"\\s*"([^"\\n]+)"', body)
            if match:
                names[str(int(sid) - 76561197960265728)] = match.group(1)
    except OSError:
        pass
    found = []
    for p in sorted((steam / 'appcache/stats').glob(f'UserGameStats_*_{APP_ID}.bin')):
        m = re.fullmatch(r'UserGameStats_(\d+)_'+APP_ID+r'.bin', p.name)
        if m:
            found.append({'id': m[1], 'name': names.get(m[1], f'Аккаунт {m[1]}')})
    return found

def state(steam, account):
    choices = accounts(steam)
    if account not in {a['id'] for a in choices}:
        raise ValueError('Выберите аккаунт из списка.')
    directory = steam / 'appcache/stats'
    schema, _ = read_stable(directory / f'UserGameStatsSchema_{APP_ID}.bin')
    schema = schema[APP_ID]
    raw, modified = read_stable(directory / f'UserGameStats_{account}_{APP_ID}.bin')
    cache = raw['cache']
    stats = schema['stats']
    numeric = {}
    for key, spec in stats.items():
        val = cache.get(key, {}).get('data')
        if spec.get('type') == 'INT' and isinstance(val, int):
            numeric[spec.get('name')] = val
        elif spec.get('type') == 'FLOAT' and isinstance(val, int):
            val = struct.unpack('<f', struct.pack('<I', val & 4294967295))[0]
            if math.isfinite(val):
                numeric[spec.get('name')] = val
    result = []
    for key, spec in stats.items():
        for bit, item in spec.get('bits', {}).items():
            block = cache.get(key, {})
            mask = block.get('data')
            unlocked = bool(mask & 4294967295 & 1 << int(bit)) if isinstance(mask, int) else None
            display = item.get('display', {})
            rule = item.get('progress', {})
            expr = rule.get('value', {})
            current = numeric.get(expr.get('operand1')) if expr.get('operation') == 'statvalue' else None
            target = rule.get('max_val')
            if not isinstance(target, (int, float)) or target <= 0:
                target = None
            status = 'done' if unlocked is True else 'unknown' if unlocked is None else 'progress' if current is not None and current > 0 else 'locked'
            group = schema.get('groups', {}).get(str(item.get('groupid')), {})
            group_name = localized(group.get('name'), 'Основная игра / набор не указан')
            if unlocked:
                action = 'Получено — цель скрыта из основного списка.'
            elif current is not None and target is not None and (current >= target):
                action = 'Счётчик достиг порога, но разблокировка не подтверждена. Проверьте дополнительные условия и синхронизацию Steam.'
            else:
                action = localized(display.get('desc'), 'Подробное условие отсутствует в кэше Steam.')
            result.append(dict(id=item['name'], name=localized(display.get('name'), item['name']), description=localized(display.get('desc')), group=group_name, status=status, current=current, target=target, remaining=max(0, target - current) if target is not None and current is not None else None, action=action, unlocked_at=block.get('AchievementTimes', {}).get(bit) if unlocked else None))
    return dict(account=account, accounts=choices, achievements=result, source='Локальный кэш Steam', cache_updated=modified, read_at=utc(datetime.now().timestamp()), pending_changes=cache.get('PendingChanges'), counts={s: sum((x['status'] == s for x in result)) for s in ('done', 'progress', 'locked', 'unknown')})

def find_steam():
    choices=[]
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Software\Valve\Steam') as key:
            choices.append(Path(winreg.QueryValueEx(key,'SteamPath')[0]))
    except (OSError,ImportError,TypeError):pass
    for p in choices+[Path('C:\\Program Files (x86)\\Steam'), Path('C:\\Program Files\\Steam'), Path('D:\\Steam')]:
        if (p / f'appcache/stats/UserGameStatsSchema_{APP_ID}.bin').is_file():
            return p
    return Path('C:\\Program Files (x86)\\Steam')
