"""Real installed rules: clear directions without altering progress or filter IDs."""
from pathlib import Path
import os,sys,json,copy
os.environ['AA_GAME']='ets2'
root=Path(__file__).resolve().parents[1]
for p in [root.parent/'achievement-core',root/'core']:
 if (p/'ets2_achievements').is_dir():sys.path.insert(0,str(p));break
from ets2_achievements.achievement_guidance import CAT,UNITS,guidance_for,describe_condition
from ets2_achievements.tracking import tracking_details

def text(aid):return '\n'.join(r['label']+': '+r['text'] for r in guidance_for(aid))
sc=text('sc_scania_cargo');vo=text('sc_volvo_cargo')
assert 'Грузовики Scania' in sc and 'Завод Scania · Сёдертелье' in sc
assert 'Откуда:' in sc and 'Куда: Любой пункт' in sc
assert 'Гётеборг' in vo and 'Завод Volvo Trucks' in vo
assert 'Сёдертелье' not in vo
assert 'Разных пунктов: 4 из 5' in text('gr_hotels')
assert 'Разных пунктов: 4 из 11' in text('ib_container_ports')
assert 'Разных пунктов: 5 из 15' in text('gr_olive')
assert 'Всего доставок: 6' in text('ba_industry')
assert 'Калининград' in text('ba_to_russia').split('Куда:')[0]
assert 'Санкт-Петербург' in text('ba_to_russia').split('Куда:')[1]
assert '2500 км' in text('be_istanbul_jobs') and 'Стамбул' in text('be_istanbul_jobs')
assert '195 т' in text('st_mass') and '3' in text('st_mass')
assert '250 т' in text('hc_250t5jobs') and '5' in text('hc_250t5jobs')
assert 'Оба направления' in text('ba_job_countries')
assert 'Только указанное направление' in text('be_express') and 'Строго сверху вниз' in text('be_express')
assert 'Исключить' in text('pc_salm_fillet') and 'Норвегия' in text('pc_salm_fillet')
assert 'подробных правил' in text('future_nonexistent_goal')
for aid in CAT['goals']:
 guide=guidance_for(aid)
 assert guide and all(x['label'] and x['text'] for x in guide),aid
 assert not any('.achievement.' in x['text'] for x in guide),aid
for uid,u in UNITS.items():
 if u['class'].startswith('achievement_delivery_') and 'steam_id' not in u['attrs']:
  assert 'пока не расшифрована' not in describe_condition(uid),uid
before=copy.deepcopy(CAT);tracking_details([],{},set());assert CAT==before
print('PASS: all installed goal instructions; exact Scania/Volvo origin, any destination, N-of-M, industry quotas, ordered/reversible routes, distance/mass and exclusion text; immutable rules.')
