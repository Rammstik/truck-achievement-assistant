import os,sys,copy
from pathlib import Path
os.environ['AA_GAME']='ats'
for root in [Path(__file__).resolve().parents[2]/'achievement-core',Path(__file__).resolve().parents[1]/'core']:
 if (root/'ets2_achievements').is_dir():sys.path.insert(0,str(root));break
from ets2_achievements.game_config import GAME,APP_ID,DOCUMENTS_NAME
from ets2_achievements.tracking import CAT,condition_progress,log_progress,log_match,trigger_progress,tracking_details
assert GAME=='ats' and APP_ID=='270880' and DOCUMENTS_NAME=='American Truck Simulator'
assert len(CAT['goals'])==140
base={'id':'test','valid':True,'cargo':'coil','target':'tay_con_sit2.pierre','target_city':'pierre','source':'a.phoenix','source_city':'phoenix','damage':0,'late':False,'fines':0,'distance':1700,'price':1000,'parking':3,'autoparking':False,'urgency':0,'route':None,'remaining':1}
cond=CAT['units'][CAT['goals']['sd_unique_cargos']['unit']]['attrs']['condition']
rows,_=condition_progress(cond,[base,base]);assert len(rows)==1 and rows[0]['id']=='coil'
wrong={**base,'target':'wrong.pierre'};rows,_=condition_progress(cond,[wrong]);assert not rows
other={**base,'cargo':'other'};rows,_=condition_progress(cond,[base,other]);assert len(rows)==2
rule=CAT['units'][CAT['goals']['hc_perf_job']['unit']]['attrs'];assert log_match(rule,base) is True
assert log_match(rule,{**base,'distance':100}) is False
rule=CAT['units'][CAT['goals']['harddifficlt']['unit']]['attrs'];assert log_match(rule,base) is True
assert log_match(rule,{**base,'parking':1}) is False
rule=CAT['units'][CAT['goals']['hc_100k5jobs']['unit']]['attrs'];r=log_progress('hc_100k5jobs',rule,[base]*5)
assert r['aggregate']=='none' and r['items'][1]['current']==5000 and r['items'][1]['target']==100000
assert trigger_progress('il_cornfield',{})['target']==1
blocks=[];before=copy.deepcopy(blocks);result=tracking_details(blocks,{},set());assert set(result)==set(CAT['goals']) and blocks==before
assert result['maxed_garage']['items'][0]['id']=='large'
print('ATS tracking PASS: 140 conditions; conditional unique cargo, distance, hard parking, simultaneous income window, missing fields and immutable input.')
