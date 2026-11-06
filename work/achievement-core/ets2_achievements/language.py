"""Language selection and presentation-only translations; rule IDs stay unchanged."""
from contextvars import ContextVar
from functools import lru_cache
from pathlib import Path
import json
import re
from .game_config import GAME

_active = ContextVar('assistant_language', default='ru')
_LANG = re.compile(r'\buset\s+g_lang(?:_init)?\s+"([^"\r\n]+)"', re.I)
_DISPLAY = {'name', 'description', 'group', 'action', 'title', 'notice', 'reason', 'text', 'label',
            'source', 'history_notice', 'summary', 'requirements', 'evidence', 'action_hint', 'caveat', 'details_error', 'error'}

def normalize(value):
    return 'ru' if isinstance(value, str) and value.lower().replace('-', '_').split('_')[0] == 'ru' else 'en'

def detect_game_language(documents):
    """The current game log overrides the startup language in config.cfg."""
    documents = Path(documents)
    for name in ('game.log.txt', 'config.cfg'):
        try:
            with (documents / name).open('rb') as stream:
                stream.seek(0, 2)
                size = stream.tell()
                stream.seek(0)
                data = stream.read(min(size, 512 * 1024))
                if size > len(data):
                    stream.seek(max(len(data), size - 2 * 1024 * 1024))
                    data += b'\n' + stream.read()
                matches = _LANG.findall(data.decode('utf-8-sig', errors='replace'))
            if matches:
                return normalize(matches[-1])
        except OSError:
            pass
    return 'en'

def resolve(setting, game_language):
    return setting if setting in ('ru', 'en') else normalize(game_language)

def current():
    return _active.get()

@lru_cache(maxsize=2)
def catalogue(game=GAME):
    root = Path(__file__).parent / 'locales'
    phrases = json.loads((root / 'en.json').read_text(encoding='utf-8'))
    labels = json.loads((root / (game + '-names-en.json')).read_text(encoding='utf-8'))
    phrases.update(labels)
    return phrases

@lru_cache(maxsize=2)
def _replacement(game=GAME):
    phrases = catalogue(game)
    return re.compile('|'.join(re.escape(k) for k in sorted(phrases, key=len, reverse=True) if k and '\0' not in k))

def translate(value, language=None):
    if not isinstance(value, str) or (language or current()) != 'en':
        return value
    phrases = catalogue()
    if value in phrases:
        return phrases[value]
    return _replacement().sub(lambda m: phrases[m.group()], value)

def present_snapshot(value, language):
    """Copy presentation strings; account/profile names and identifiers are untouched."""
    if language != 'en':
        return value
    def visit(node, display=False, identity=False):
        if isinstance(node, dict):
            return {key: visit(child, key in _DISPLAY, identity or key in ('accounts', 'profiles', 'profile'))
                    for key, child in node.items()}
        if isinstance(node, list):
            return [visit(child, display, identity) for child in node]
        return translate(node, language) if display and not identity and isinstance(node, str) else node
    return visit(value)
