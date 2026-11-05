from .game_config import GAME,APP_ID,data_file
"""Special Transport route evidence from a possibly incomplete delivery log.

Only exact route IDs named in the achievement condition are counted. A route
appearing in offers or a normal job with the same cities is not completion.
Absence in this log is unknown, not proof that Steam has not counted a route.
"""
from pathlib import Path
import json,re,math
RULES=json.loads((data_file('special_route_rules.json')).read_text(encoding='utf-8'))

def special_route_details(blocks):
    unavailable={'kind':'special_routes','available':False,
                 'reason':'Поддерживаемый журнал доставок не найден. Отдельные маршруты не определены; отсутствие данных не считается нулевым прогрессом.'}
    logs=[u for u in blocks if u['class']=='delivery_log']
    if len(logs)!=1:return unavailable
    log=logs[0]['attrs'];refs=log.get('entries')
    if log.get('version')!=1 or not isinstance(refs,list) or not all(isinstance(r,str) for r in refs):return unavailable
    units={};duplicates=set()
    for unit in blocks:
        if unit['class']=='delivery_log_entry':
            uid=unit['id']
            if uid in units:duplicates.add(uid)
            units[uid]=unit['attrs']
    counts={r['id']:0 for r in RULES['items']}
    seen=set();unreadable=0;uncertain=0;other_routes=0;ignored=0
    for ref in refs:
        if ref in seen:continue
        seen.add(ref);p=units.get(ref,{}).get('params')
        if ref in duplicates or not isinstance(p,list) or len(p)!=24 or not all(isinstance(v,str) for v in p):
            unreadable+=1;continue
        if p[18]!='spec_oversize':
            if p[18] in {'compn','on_compn','quick','freerm'}:ignored+=1
            else:unreadable+=1
            continue
        route=p[19]
        if route not in counts:
            if route in RULES['known_routes']:other_routes+=1
            else:unreadable+=1
            continue
        # Conservative evidence filter. The undocumented log cannot be used as
        # a per-route Steam flag; zero/negative payment is not asserted success.
        try:distance=float(p[6]);payment=float(p[13])
        except ValueError:unreadable+=1;continue
        if (not p[3].startswith('cargo.') or not re.fullmatch(r'[a-z0-9_]+',p[3][6:]) or
            not math.isfinite(distance) or not math.isfinite(payment) or distance<=0 or payment<=0):
            uncertain+=1;continue
        counts[route]+=1
    items=[{**r,'logged':counts[r['id']]>0,'deliveries':counts[r['id']]} for r in RULES['items']]
    completed=sum(r['logged'] for r in items)
    return {'kind':'special_routes','available':True,'estimated':True,'items':items,
            'observed_completed':completed,'target':RULES['target'],'total':len(items),
            'needed_by_log':max(0,RULES['target']-completed),'unrecorded':len(items)-completed,
            'records':len(seen),'matched_deliveries':sum(counts.values()),'other_routes':other_routes,
            'unreadable_records':unreadable,'uncertain_records':uncertain,'ignored_records':ignored,
            'source':'Журнал доставок выбранного сохранения; точный ID маршрута Special Transport',
            'rule_version':RULES['source_game_version'],
            'action_hint':'Выполнить доставку Special Transport по другому маршруту из списка. Обычный заказ между теми же городами не подходит. Повтор одного маршрута не добавляет новый пункт.'}
