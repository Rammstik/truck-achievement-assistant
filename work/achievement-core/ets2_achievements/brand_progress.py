"""Evidence from delivery history, NOT Steam's per-brand achievement flags.

Rule: achievement_truck_brand_data .achievement.999km_brands, ETS2 1.61.1.9.
Log slots are reverse engineered; see README. Only the observed v1/24-slot
layout is enabled. Unrecognised data is reported, never interpreted as zero.
"""
import re

BRANDS={'daf':'DAF','iveco':'Iveco','man':'MAN','mercedes':'Mercedes-Benz',
        'renault':'Renault','scania':'Scania','volvo':'Volvo'}
OWN_JOB_TYPES={'compn','on_compn','spec_oversize'}
EXCLUDED_JOB_TYPES={'quick','freerm'}

def brand_details(blocks):
    logs=[b for b in blocks if b['class']=='delivery_log']
    unavailable={'kind':'truck_brands','available':False,
                 'reason':'В этом сохранении не найден поддерживаемый журнал доставок (версия 1, 24 поля).'}
    if len(logs)!=1:return unavailable
    log=logs[0]['attrs'];refs=log.get('entries')
    if log.get('version')!=1 or not isinstance(refs,list) or not all(isinstance(r,str) for r in refs):return unavailable
    # Require a recorded ownership match as additional evidence. Sold models may
    # be missing: that reduces coverage rather than establishing non-completion.
    owned=set()
    for block in blocks:
        if block['class']=='game_progress':
            models=block['attrs'].get('owned_trucks',[])
            if isinstance(models,list):owned.update(m for m in models if isinstance(m,str))
    units={};duplicate_ids=set()
    for block in blocks:
        if block['class']=='delivery_log_entry':
            uid=block['id']
            if uid in units:duplicate_ids.add(uid)
            units[uid]=block['attrs']
    totals={brand:{'km':0,'jobs':0} for brand in BRANDS}
    excluded=0;unreadable=0;unverified=0;seen=set()
    for ref in refs:
        if ref in seen:continue
        seen.add(ref)
        params=units.get(ref,{}).get('params')
        if (ref in duplicate_ids or not isinstance(params,list) or len(params)!=24
                or not all(isinstance(v,str) for v in params)):
            unreadable+=1;continue
        job_type=params[18]
        if job_type in EXCLUDED_JOB_TYPES:excluded+=1;continue
        if job_type not in OWN_JOB_TYPES:unreadable+=1;continue
        vehicle=re.fullmatch(r'vehicle\.([a-z_]+)\.[a-z0-9_.]+',params[16])
        if (not vehicle or vehicle[1] not in BRANDS or params[16] not in owned):
            unverified+=1;continue
        endpoints=(params[1].startswith('company.volatile.') and params[2].startswith('company.volatile.'))
        # Special Transport stores a route ID instead of company endpoints.
        if job_type=='spec_oversize' and params[1]==params[2]=='':
            endpoints=bool(re.fullmatch(r'[a-z0-9_]+',params[19]))
        if (not endpoints or
                not params[3].startswith('cargo.') or
                not re.fullmatch(r'[0-9]+',params[6]) or len(params[6])>7):
            unreadable+=1;continue
        km=int(params[6])
        if km>100000:unreadable+=1;continue
        row=totals[vehicle[1]];row['km']+=km;row['jobs']+=1
    items=[{'id':brand,'name':name,'logged_km':totals[brand]['km'] if totals[brand]['jobs'] else None,
            'jobs':totals[brand]['jobs'],'threshold_seen':totals[brand]['jobs']>0 and totals[brand]['km']>=999}
           for brand,name in BRANDS.items()]
    return {'kind':'truck_brands','available':True,'estimated':True,'items':items,
            'observed_completed':sum(r['threshold_seen'] for r in items),
            'distance_target':999,'target':5,'records':len(seen),
            'counted_jobs':sum(r['jobs'] for r in items),'excluded_records':excluded,
            'unreadable_records':unreadable,'unverified_records':unverified,
            'source':'Журнал доставок выбранного сохранения; оценка по записанной дистанции заказов',
            'rule_version':'1.61.1.9'}
