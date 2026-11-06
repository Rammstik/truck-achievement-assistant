from .game_config import GAME,DOCUMENTS_NAME
import ctypes,time
from pathlib import Path
from .steam_cache import accounts,state,find_steam
from .profile_details import profiles,public_profiles,details
from . import CORE_VERSION
from . import language as languages
def snapshot(steam, documents, control):
    game_language = languages.detect_game_language(documents)
    setting = control.get('language', 'auto')
    language = languages.resolve(setting, game_language)
    token = languages._active.set(language)
    try:
        result = _snapshot(steam, documents, control)
        result.update(language=language, game_language=game_language, language_setting=setting)
        return languages.present_snapshot(result, language)
    finally:
        languages._active.reset(token)

def _snapshot(steam, documents, control):
    found = accounts(steam)
    account = control.get('account', '')
    if account not in {a['id'] for a in found}:
        account = found[0]['id'] if len(found) == 1 else ''
    result = {'version': '0.6-native', 'core_version': CORE_VERSION, 'game':GAME, 'accounts': found, 'account': account, 'profiles': [], 'profile': '', 'achievements': [], 'read_at': time.time(), 'request': {**{k: control.get(k, '') for k in ('account', 'profile')}, 'language':control.get('language', 'auto')}}
    if not account:
        result['notice'] = 'Выберите свой Steam-аккаунт.' if found else f'Кэш {GAME.upper()} в Steam не найден. Запустите игру через Steam.'
        return result
    found_profiles = profiles(steam, account, documents)
    result['profiles'] = public_profiles(found_profiles)
    profile = control.get('profile', '')
    if profile not in {p['id'] for p in found_profiles}:
        profile = ''
    result.update(state(steam, account))
    from .achievement_guidance import guidance_for,EXTRA_GOALS
    for item in result['achievements']:item['guidance']=guidance_for(item['id'])
    # Future Steam entries have no installed conditions to invent a checklist from.
    from .tracking import CAT
    result['catalogue']={'reviewed':len(CAT['goals']),'steam_total':len(result['achievements']),
                         'upcoming':[a['id'] for a in result['achievements'] if a['id'] not in CAT['goals'] and a['id'] not in EXTRA_GOALS],
                         'unimplemented':[a['id'] for a in result['achievements'] if a['id'] in EXTRA_GOALS]}
    result['profile'] = profile
    result['notice'] = 'Выберите игровой профиль для подробностей.'
    if profile:
        try:
            result['details'] = details(steam, account, profile, documents)
            for item in result['achievements']:
                if item['id']=='hc_1000xps':
                    info=dict(result['details']['achievements'][item['id']])
                    info['observed']=item['current'];info['available']=item['current'] is not None
                    info['items']=[{**r,'current':item['current'],'manual':item['current'] is None} for r in info['items']]
                    result['details']['achievements'][item['id']]=info
                if item['id'] not in CAT['goals']:
                    result['details']['achievements'][item['id']]={'kind':'tracking','title':'Поддержка новой цели' if item['id'] in EXTRA_GOALS else 'Будущее дополнение','mode':'unimplemented' if item['id'] in EXTRA_GOALS else 'upcoming','available':False,
                        'items':[],'observed':None,'target':item['target'],
                        'reason':'Определение этой цели найдено в игре 1.62, но её подробный трекинг и подсветка пока не реализованы. Условие и общий прогресс читаются из Steam.' if item['id'] in EXTRA_GOALS else 'Эта цель есть в схеме Steam, но её условий ещё нет в установленной игре. Автоматический трекинг до появления правил недоступен.'}
            result['notice'] = 'Подробности обновляются после записи сохранения игрой.'
        except Exception as exc:
            result['details_error'] = f'Сохранение временно недоступно ({type(exc).__name__}). Подробности не подменены нулями.'
    return result

def documents_directory():
    buffer = ctypes.create_unicode_buffer(32768)
    if ctypes.windll.shell32.SHGetFolderPathW(None, 5, None, 0, buffer) == 0:
        return Path(buffer.value) / DOCUMENTS_NAME
    return Path.home() / 'Documents' / DOCUMENTS_NAME
