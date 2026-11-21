#!/usr/bin/env python3
"""Diagnostic GL interposition; never acceptance timing, never attaches to a process."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
from test_scene_recipe import OwnedRenderer, free_udp_port, collect_cleanup
from test_feedback_composition import setup_commands, replay, checked, FIXTURES


def analyze(events, start=0, stop=None):
    active=None;intervals=[];available=set();alloc=set();deleted=set();errors=[];draw=None
    for event in events:
        kind=event['event'];a=event['a']
        if kind=='query_alloc':alloc.add(a)
        elif kind=='query_delete':deleted.add(a)
        elif kind=='query_begin':
            if active or event['c']:errors.append('overlapping elapsed queries')
            available.discard(a);active={'query':a,'begin':event['seq'],'draws':[]}
        elif kind=='query_end':
            if not active or active['query']!=a:errors.append('unmatched query end')
            elif start<=active['begin'] and (stop is None or event['seq']<stop):intervals.append(active)
            active=None
        elif kind=='query_available':
            if event['c']:available.add(a)
            else:available.discard(a)
        elif kind=='query_result' and a not in available:errors.append('result requested without available result')
        elif kind in ('draw_arrays','draw_elements') and active:
            draw={'call':kind,'mode':a,'count':event['b'],'type':event['c']};active['draws'].append(draw)
        elif kind=='draw_state' and active and draw:draw['program']=a
        elif kind=='draw_depth' and active and draw:draw['depth_test']=bool(a)
        elif kind=='position_bytes' and active and draw:draw['position_bytes']=[a,event['b']]
        elif kind=='finish':errors.append('glFinish observed')
    if events and events[-1]['seq']>=65535:errors.append('bounded trace exhausted')
    return {'allocated_queries':sorted(alloc),'deleted_queries':sorted(deleted),
            'errors':errors,'intervals':intervals}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',required=True,type=Path);parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();out=args.output.resolve();out.mkdir(mode=0o700)
    root=Path(__file__).resolve().parents[1];library=out/'gltrace.dylib'
    subprocess.run(['xcrun','clang++','-std=c++17','-dynamiclib',str(root/'tests/performance/FeedbackGlTrace.cpp'),'-framework','OpenGL','-o',str(library)],check=True,stdout=(out/'compile.log').open('w'),stderr=subprocess.STDOUT)
    identities={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in [Path(__file__),root/'tests/performance/FeedbackGlTrace.cpp',library]}
    trace=out/'calls.jsonl';saved={k:os.environ.get(k) for k in ('DYLD_INSERT_LIBRARIES','VOSC_GL_TRACE')}
    renderer=None;evidence={'source_sha256':identities,'status':'FAILED','diagnostic_only':True,'timings_eligible':False,'segments':{}}
    try:
        os.environ.update(DYLD_INSERT_LIBRARIES=str(library),VOSC_GL_TRACE=str(trace))
        renderer=OwnedRenderer(args.binary.resolve(),out,'trace',free_udp_port(),[])
        for key,value in saved.items():
            if value is None:os.environ.pop(key,None)
            else:os.environ[key]=value
        client=renderer.client()
        try:
            for label in ('a','b'):
                replay(renderer,setup_commands(FIXTURES/f'attribution-{label}.frag',True,True))
                time.sleep(.5)
                before=json.loads(trace.read_text().splitlines()[-1])['seq']
                scene=checked(client,'get_scene');time.sleep(1.5)
                after=json.loads(trace.read_text().splitlines()[-1])['seq']
                evidence['segments'][label]={'start':before,'stop':after,'scene':scene}
        finally:client.close()
    finally:
        for key,value in saved.items():
            if value is None:os.environ.pop(key,None)
            else:os.environ[key]=value
        if renderer:evidence['cleanup'],error=collect_cleanup(renderer);evidence['cleanup_error']=error
    events=[json.loads(line) for line in trace.read_text().splitlines()];all_trace=analyze(events)
    evidence['trace']=all_trace;evidence['binary_sha256']=hashlib.sha256(args.binary.read_bytes()).hexdigest()
    if len(all_trace['allocated_queries'])!=16 or all_trace['errors']:raise RuntimeError(all_trace['errors'] or 'expected two eight-query pools')
    # Native allocates draw pool first, update pool second. Record actual pool IDs.
    allocations=[e['a'] for e in events if e['event']=='query_alloc'];draw_pool=set(allocations[:8]);update_pool=set(allocations[8:16])
    evidence['draw_pool']=sorted(draw_pool);evidence['update_pool']=sorted(update_pool)
    signatures={}
    for label,segment in evidence['segments'].items():
        intervals=analyze(events,segment['start'],segment['stop'])['intervals']
        draws=[i for i in intervals if i['query'] in draw_pool];updates=[i for i in intervals if i['query'] in update_pool]
        signatures[label]=Counter(json.dumps(i['draws'],sort_keys=True) for i in draws)
        segment['draw_signatures']=dict(signatures[label]);segment['draw_intervals']=len(draws);segment['update_intervals']=len(updates)
        if len(draws)<10 or len(updates)<10 or not all(i['draws'] for i in updates):raise RuntimeError('insufficient executed trace')
    evidence['draw_signature_equal']=set(signatures['a'])==set(signatures['b'])
    evidence['shutdown_deleted_all_queries']=set(allocations)==set(all_trace['deleted_queries'])
    evidence['status']='PASS' if evidence['draw_signature_equal'] and evidence['shutdown_deleted_all_queries'] and not evidence['cleanup_error'] else 'FAILED'
    (out/'evidence.json').write_text(json.dumps(evidence,indent=2)+'\n');print(evidence['status'])
    return 0 if evidence['status']=='PASS' else 1

if __name__=='__main__':raise SystemExit(main())
