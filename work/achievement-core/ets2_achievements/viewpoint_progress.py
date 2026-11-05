from .game_config import GAME,APP_ID,data_file
"""Saved map-item states, matched to exact achievement actions in game maps.

State 2 is interpreted as viewed based on the current save/Steam comparisons.
Other numeric states stay unknown; discovery alone is not counted as viewing.
UIDs are decimal strings at the JSON boundary to preserve all 64 bits in JS.
"""
from pathlib import Path
import json
CATALOGUE=json.loads((data_file('viewpoint_rules.json')).read_text(encoding='utf-8'))

def viewpoint_details(economy):
    ids=economy.get('discovered_cutscene_items')
    states=economy.get('discovered_cutscene_items_states')
    valid=(isinstance(ids,list) and isinstance(states,list) and len(ids)==len(states)
           and all(type(v) is int and 0<=v<2**64 for v in ids)
           and all(type(v) is int and 0<=v<2**32 for v in states))
    recorded={}
    if valid:
        for uid,value in zip(ids,states):
            if uid in recorded and recorded[uid]!=value:valid=False;break
            recorded[uid]=value
    result={}
    for aid,rule in CATALOGUE['rules'].items():
        if not valid:
            result[aid]={'kind':'viewpoints','available':False,
                         'reason':'Список состояний обзорных точек отсутствует или имеет неподдерживаемый формат. Просмотр не считается нулевым.'}
            continue
        items=[]
        for point in rule['items']:
            state=recorded.get(int(point['id']))
            viewed=False if state is None else True if state==CATALOGUE['viewed_state'] else None
            items.append({**point,'viewed':viewed,'saved_state':state})
        completed=sum(p['viewed'] is True for p in items)
        unknown=sum(p['viewed'] is None for p in items)
        result[aid]={'kind':'viewpoints','available':True,'items':items,'completed':completed,'target':rule['target'],
                     'needed':max(0,rule['target']-completed),'unmarked':sum(p['viewed'] is False for p in items),
                     'unknown':unknown,'total':len(items),'source':'Отдельные состояния обзорных точек выбранного сохранения',
                     'rule_version':CATALOGUE['source_game_version'],
                     'action_hint':'Доехать до значка обзорной точки и запустить обзор. Сам проезд рядом с точкой не является просмотром.',
                     'interpretation':'Отметка просмотра — сохранённое состояние 2. Значение сверено со счётчиками текущего профиля; другие состояния не угадываются.'}
    return result
