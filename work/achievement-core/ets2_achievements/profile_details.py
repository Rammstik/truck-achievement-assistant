from .game_config import GAME,APP_ID,data_file
import hashlib,json
from datetime import datetime,timezone
from pathlib import Path
from .save_reader import load_save
from .brand_progress import brand_details
from .viewpoint_progress import viewpoint_details
from .special_route_progress import special_route_details
from .baltic_delivery_progress import baltic_delivery_details, RULES as GEOGRAPHY
from .tracking import tracking_details

RULES=json.loads((data_file('city_rules.json')).read_text(encoding='utf-8'))
_cache={}

def profiles(steam,account,documents):
    roots=[('steam',steam/'userdata'/account/f'{APP_ID}/remote/profiles'),('local',documents/'profiles')]
    found=[]
    for kind,root in roots:
        if not root.is_dir():continue
        for directory in root.iterdir():
            if not directory.is_dir():continue
            saves=[p for p in directory.glob('save/*/game.sii') if p.is_file()]
            if not saves:continue
            newest=max(saves,key=lambda p:p.stat().st_mtime_ns)
            try:label=bytes.fromhex(directory.name).decode('utf-8')
            except (ValueError,UnicodeError):label=directory.name
            identity=f'ats:{kind}:{account}:{directory.name.casefold()}' if GAME=='ats' else str(directory)
            token=hashlib.sha256(identity.encode()).hexdigest()[:24]
            found.append({'id':token,'name':label,'storage':kind,'save_path':newest,
                'updated':datetime.fromtimestamp(newest.stat().st_mtime,timezone.utc).isoformat()})
    return sorted(found,key=lambda p:p['updated'],reverse=True)

def public_profiles(found):return [{k:v for k,v in p.items() if k!='save_path'} for p in found]

def details(steam,account,profile_id,documents):
    matches=[p for p in profiles(steam,account,documents) if p['id']==profile_id]
    if not matches:raise ValueError('Выбранный профиль не найден')
    selected=matches[0];path=selected['save_path'];stat=path.stat();stamp=(str(path),stat.st_mtime_ns,stat.st_size)
    cached=_cache.get(stamp)
    if cached is None:
        blocks,modified=load_save(path)
        economies=[u['attrs'] for u in blocks if u['class']=='economy']
        economy=economies[0] if len(economies)==1 else {}
        visited=economy.get('visited_cities')
        if not isinstance(visited,list) or not all(isinstance(v,str) for v in visited):visited=None
        else:visited=set(visited)
        # Bound memory; store only read-only derived lists, not full save units.
        cargos=economy.get('transported_cargo_types')
        if not isinstance(cargos,list) or not all(isinstance(c,str) for c in cargos):cargos=None
        else:cargos=set(cargos)
        brands=brand_details(blocks) if GAME=="ets2" else {}
        viewpoints=viewpoint_details(economy)
        routes=special_route_details(blocks)
        baltic=baltic_delivery_details(blocks) if GAME=="ets2" else {}
        # Existing detail schemas remain stable for the native worker.
        old_ids=set(RULES['rules'])|set(RULES['cargo_rules'])|set(viewpoints)|set(baltic)|({'999_km_with_each_brand','st_all_routes'} if GAME=='ets2' else {'st_all_routes'})
        tracking=tracking_details(blocks,economy,old_ids)
        player=next((u['attrs'] for u in blocks if u['id']==economy.get('player') and u['class']=='player'),{})
        garage_refs=economy.get('garages')
        garage_rows=[{'city':u['id'].removeprefix('garage.'),'status':u['attrs'].get('status')}
                     for u in blocks if u['class']=='garage' and isinstance(garage_refs,list) and u['id'] in garage_refs]
        ferry_logs=[u['attrs'] for u in blocks if u['class']=='ferry_log']
        ferry_refs=ferry_logs[0].get('entries') if len(ferry_logs)==1 else None
        used_ports=sorted({u['attrs']['ferry'] for u in blocks if u['class']=='ferry_log_entry'
                         and isinstance(ferry_refs,list) and u['id'] in ferry_refs
                         and u['attrs'].get('use_count',0)>0 and isinstance(u['attrs'].get('ferry'),str)})
        recruitment=economy.get('unlocked_recruitments')
        hq=player.get('hq_city')
        map_progress={'hq_city':hq if isinstance(hq,str) else None,
                      'hq_country':GEOGRAPHY['city_countries'].get(hq) if isinstance(hq,str) else None,
                      'garages':garage_rows,'garages_available':isinstance(garage_refs,list),
                      'recruitment_cities':[v for v in recruitment if isinstance(v,str)] if isinstance(recruitment,list) else None,
                      'used_ports':used_ports if isinstance(ferry_refs,list) else None}
        _cache.clear();_cache[stamp]=(visited,cargos,brands,viewpoints,routes,baltic,tracking,map_progress,modified)
    else:visited,cargos,brands,viewpoints,routes,baltic,tracking,map_progress,modified=cached
    result={}
    for aid,rule in (RULES['rules'].items() if visited is not None else []):
        rows=[{'id':city,'name':RULES['labels'].get(city,city.replace('_',' ').title()),'visited':city in visited} for city in rule['cities']]
        count=sum(row['visited'] for row in rows)
        result[aid]={'kind':'visited_cities','items':rows,'completed':count,'target':rule['target'],
            'needed':max(0,rule['target']-count),'unvisited':len(rows)-count,'source':'Сохранение выбранного профиля',
            'rule_version':rule['source_game_version']}
    if cargos is not None:
        cargo_labels=RULES.get('cargo_labels',{})
        for aid,rule in RULES.get('cargo_rules',{}).items():
            rows=[{'id':cargo,'name':cargo_labels.get(cargo,cargo.replace('_',' ').title()),'visited':cargo in cargos} for cargo in rule['cargos']]
            count=sum(row['visited'] for row in rows)
            result[aid]={'kind':'transported_cargos','items':rows,'completed':count,'target':rule['target'],
                'needed':max(0,rule['target']-count),'unvisited':len(rows)-count,
                'source':'Накопительный список перевезённых грузов в сохранении',
                'rule_version':rule['source_game_version']}
    if GAME=='ets2':result['999_km_with_each_brand']=brands
    result.update(viewpoints)
    result['st_all_routes']=routes
    result.update(baltic)
    result.update(tracking)
    return {'profile':{k:v for k,v in selected.items() if k!='save_path'},'save_name':path.parent.name,
        'save_updated':datetime.fromtimestamp(modified,timezone.utc).isoformat(),'achievements':result,'map_progress':map_progress}
