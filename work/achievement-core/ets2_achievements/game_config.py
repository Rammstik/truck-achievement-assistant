"""Per-process game selection, isolated per plugin."""
import os
from pathlib import Path
GAME=os.environ.get('AA_GAME','ets2').lower()
if GAME not in ('ets2','ats'):raise ValueError('Unsupported game')
APP_ID='270880' if GAME=='ats' else '227300'
DOCUMENTS_NAME='American Truck Simulator' if GAME=='ats' else 'Euro Truck Simulator 2'
def data_file(name):
    root=Path(__file__).parent
    return root/'game_data'/'ats'/name if GAME=='ats' else root/name
