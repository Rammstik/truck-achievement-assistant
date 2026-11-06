"""Compile shared UI translations into the DLL; no translation service is required."""
from pathlib import Path
import json

root = Path(__file__).resolve().parent
data = json.loads((root.parent / 'achievement-core/ets2_achievements/locales/en.json').read_text(encoding='utf-8'))
body = '// Generated from the shared locales/en.json catalogue.\n'
body += ''.join('{' + json.dumps(ru, ensure_ascii=False) + ',' + json.dumps(en, ensure_ascii=False) + '},\n'
                for ru, en in sorted(data.items()) if '\0' not in ru and '\0' not in en)
(root / 'src/LocalizationData.h').write_text(body, encoding='utf-8')
print('Generated native translations:', len(data))
