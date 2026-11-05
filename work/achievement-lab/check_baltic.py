"""Achievement-condition edge cases, using synthetic delivery-log records."""
from pathlib import Path
import copy,json,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'achievement-core'))
from ets2_achievements.baltic_delivery_progress import baltic_delivery_details as read, RULES

def job(uid,src='radus.kouvola',dst='tradeaux.vilnius',damage='0.000',remaining='42',kind='compn',pay='1000'):
    p=['0']*24
    p[1]='company.volatile.'+src;p[2]='company.volatile.'+dst;p[3]='cargo.concrete'
    p[5]=pay;p[6]='120';p[7]=damage;p[8]=remaining;p[18]=kind;p[14]='500'
    return {'class':'delivery_log_entry','id':uid,'attrs':{'params':p}}

def blocks(*jobs):
    return [{'class':'delivery_log','id':'log','attrs':{'version':1,'entries':[j['id'] for j in jobs]}},*jobs]

def concrete(b):return read(b)['ba_concrete']
def tour(b):return read(b)['ba_job_countries']

assert len(RULES['plants'])==4 and len(RULES['pairs'])==5
assert not concrete([])['available']
assert concrete(blocks())['available'] and concrete(blocks())['observed_deliveries']==0
# Repeated deliveries count, a duplicate reference does not, orphan units do not.
b=blocks(*(job(str(i)) for i in range(10)));b[0]['attrs']['entries'].append('0');b.append(job('orphan'))
assert concrete(b)['observed_deliveries']==10
# Delivery TO a plant, wrong city, and same-city wrong company are excluded.
assert concrete(blocks(job('a','other.vilnius','radus.kouvola'),job('b','radus.nis'),job('c','other.kouvola')))['observed_deliveries']==0
assert concrete(blocks(*(job(str(i),r['id']) for i,r in enumerate(RULES['plants']))))['observed_deliveries']==4
# Concrete has no cargo-damage/lateness condition, but a non-positive payment is not asserted success.
assert concrete(blocks(job('a',damage='0.2',remaining='-1')))['observed_deliveries']==1
assert concrete(blocks(job('a',pay='0'),job('b',pay='NaN')))['observed_deliveries']==0
# Country pairs are unordered and unique. All five can be completed in reverse.
routes=[('vilnius','pskov'),('riga','vilnius'),('tallinn','riga'),('pskov','tallinn'),('helsinki','pskov')]
b=blocks(*(job(str(i),'other.'+s,'other.'+d) for i,(s,d) in enumerate(routes)))
assert tour(b)['observed_completed']==5
b=blocks(job('a','other.pskov','other.vilnius'),job('b','other.vilnius','other.pskov'))
assert tour(b)['observed_completed']==1 and len(tour(b)['items'][0]['deliveries'])==2
# Fines (set above) do not disqualify a job. Internal zero time remaining is allowed.
assert tour(blocks(job('a','other.pskov','other.vilnius',remaining='0')))['observed_completed']==1
for damage,remaining,kind in [('0.001','42','compn'),('0','-1','quick'),('0','-1','on_compn')]:
    r=tour(blocks(job('a','other.pskov','other.vilnius',damage,remaining,kind)))
    assert r['observed_completed']==0 and len(r['items'][0]['rejected'])==1
assert tour(blocks(job('a','other.pskov','other.vilnius',kind='on_compn',remaining='1')))['observed_completed']==1
for damage,remaining,kind in [('NaN','42','compn'),('0','?','compn'),('0','42','on_compn'),('-0.1','42','compn')]:
    r=tour(blocks(job('a','other.pskov','other.vilnius',damage,remaining,kind)))
    assert r['observed_completed']==0 and len(r['items'][0]['uncertain'])==1
assert tour(blocks(job('a','other.mod_city','other.vilnius')))['unknown_city_records']==1
assert tour(blocks(job('a','other.pskov','other.kaliningrad')))['observed_completed']==0
assert tour(blocks(job('a','other.pskov','other.riga')))['observed_completed']==0
# Duplicate IDs, malformed formats, free roam, unknown log versions fail closed.
b=blocks(job('a'),job('a'));assert concrete(b)['observed_deliveries']==0
b=blocks(job('a'));b[1]['attrs']['params'].pop();assert concrete(b)['unreadable_records']==1
b=blocks(job('a'));b[0]['attrs']['version']=2;assert not concrete(b)['available']
assert concrete(blocks(job('a',kind='freerm')))['observed_deliveries']==0
b=blocks(job('a'));before=copy.deepcopy(b);read(b);assert b==before
json.dumps(read(b),allow_nan=False)
print('PASS: exact factories/source direction, repeats, five unordered pairs, damage, lateness, fines, unknown data, log identity, immutable input.')
