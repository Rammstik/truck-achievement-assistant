from .game_config import GAME,APP_ID,data_file
"""Baltic history evidence, never individual Steam flags.

Log p7 is cargo damage, p8 signed time remaining (online ±1). Their meaning
is reverse engineered; controlled in-game checks are still required.
"""
from pathlib import Path
import json, math, re

RULES = json.loads((data_file('baltic_delivery_rules.json')).read_text(encoding='utf-8'))
ENDPOINT = re.compile(r'company\.volatile\.([a-z0-9_]+)\.([a-z0-9_]+)\Z')


def baltic_delivery_details(blocks):
    concrete = {'kind':'concrete_deliveries', 'available':False}
    tour = {'kind':'country_pairs', 'available':False}
    result = {'ba_concrete':concrete, 'ba_job_countries':tour}
    logs = [u['attrs'] for u in blocks if u['class']=='delivery_log']
    refs = logs[0].get('entries') if len(logs)==1 and logs[0].get('version')==1 else None
    if not isinstance(refs,list) or not all(isinstance(r,str) for r in refs):
        for goal in result.values():
            goal['reason']='Поддерживаемый журнал доставок не найден. Прогресс по отдельным пунктам неизвестен.'
        return result
    units = {}; duplicates = set()
    for unit in blocks:
        if unit['class']=='delivery_log_entry':
            if unit['id'] in units: duplicates.add(unit['id'])
            units[unit['id']] = unit['attrs']
    plants = {row['id']:{**row,'deliveries':[]} for row in RULES['plants']}
    pairs = {tuple(sorted(row['countries'])):{**row,'deliveries':[], 'rejected':[], 'uncertain':[]} for row in RULES['pairs']}
    seen = set(); unreadable = unverified = unknown_cities = 0
    for ref in refs:
        if ref in seen: continue
        seen.add(ref)
        p = units.get(ref,{}).get('params')
        if ref in duplicates or not isinstance(p,list) or len(p)!=24 or not all(isinstance(v,str) for v in p):
            unreadable += 1; continue
        if p[18]=='freerm': continue
        if p[18] not in {'compn','quick','on_compn','spec_oversize'}:
            unreadable += 1; continue
        source = ENDPOINT.fullmatch(p[1]); dest = ENDPOINT.fullmatch(p[2])
        if not source or not dest:
            unverified += 1; continue
        try:
            payment = float(p[5]); distance = float(p[6])
        except ValueError:
            unverified += 1; continue
        if not (math.isfinite(payment) and payment>0 and math.isfinite(distance) and distance>0
                and re.fullmatch(r'cargo\.[a-z0-9_]+',p[3])):
            unverified += 1; continue
        sc, dc = source[2], dest[2]
        labels = RULES['city_labels']
        evidence = {'id':ref,'source':labels.get(sc,sc),'destination':labels.get(dc,dc),'job_type':p[18]}
        plant = plants.get(f'{source[1]}.{sc}')
        if plant is not None:
            # No damage/lateness restriction here; repeated jobs count.
            plant['deliveries'].append(evidence)
        countries = RULES['city_countries']
        if sc not in countries or dc not in countries:
            unknown_cities += 1; continue
        pair = pairs.get(tuple(sorted((countries[sc],countries[dc]))))
        if pair is None: continue
        try:
            damage = float(p[7]); remaining = int(p[8])
            if not math.isfinite(damage) or not 0<=damage<=1: raise ValueError()
            if p[18]=='on_compn' and remaining not in {-1,1}: raise ValueError()
        except ValueError:
            pair['uncertain'].append(evidence); continue
        reasons = []
        if damage>0: reasons.append('Повреждён груз')
        if remaining<0: reasons.append('Опоздание')
        if reasons:
            pair['rejected'].append({**evidence,'reasons':reasons})
        else:
            # Fines and parking mode are not achievement conditions.
            pair['deliveries'].append(evidence)
    common = {'available':True,'estimated':True,'records':len(seen),
              'unreadable_records':unreadable,'unverified_records':unverified,
              'unknown_city_records':unknown_cities,'rule_version':RULES['source_game_version'],
              'source':'Журнал доставок выбранного сохранения',
              'history_notice':'Журнал может быть неполным. Отсутствие записи не доказывает невыполнение; это восстановление истории, а не отдельные отметки Steam.'}
    concrete.update(common,items=list(plants.values()),target=10,
                    observed_deliveries=sum(len(row['deliveries']) for row in plants.values()))
    tour.update(common,items=list(pairs.values()),target=5,
                observed_completed=sum(bool(row['deliveries']) for row in pairs.values()))
    return result
