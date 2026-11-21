#!/usr/bin/env python3
"""Owned 800x600 feedback replay collector; no active renderer/SC session access."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import socket
import subprocess
import sys
import time
import uuid
from test_scene_recipe import (ROOT, OwnedRenderer, capture, collect_cleanup, free_udp_port,
    normalize_descriptor, osc, osc_bundle, recipe_messages, receive_results, run_sclang, source_snapshot)
from feedback_oracle import spatial_frames, compare_png_pixels
FIXTURES=ROOT/'tests/fixtures/feedback'


def setup_commands(fragment, continuous=False, gradient=False):
    commands = [('/layers/reset',(2,'stack')),('/tex',(0,'shader_pp:'+str(fragment.resolve()))),
        ('/tex/size',(0,800.0,600.0)),('/tex/speed',(0,1.0)),
        *[('/tex/set',(0,name,value)) for name,value in (
            ('internalformat',32856),('textureTarget',3553),('filter',9729),
            ('aspectRatio',1),('blendMode',0),('static',int(not continuous)))],
        ('/shader',(1,str((FIXTURES/'gradient.frag').resolve()))),
        ('/layer/alpha',(1,.25)),('/layer/blend',(1,1)),('/layer/visible',(1,int(gradient)))]
    if fragment.name == 'temporal.frag':
        commands.append(('/tex/var',(0,'decay',.5)))
    return commands


def replay(renderer, commands, *, final_gate=-1):
    token=str(uuid.uuid4())
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as listener:
        listener.bind(('127.0.0.1',0))
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:
            sender.sendto(osc_bundle(recipe_messages(token,listener.getsockname()[1],commands,
                final_gate=final_gate)),('127.0.0.1',renderer.port))
        receipt=receive_results(listener,1)[0]
    if receipt['token']!=token or receipt['status']!='applied':
        raise RuntimeError(f'feedback setup failed: {receipt}')
    return receipt


def checked(client,method):
    response=client.call(method,{})
    if not response.get('ok'): raise RuntimeError(f'{method} failed: {response}')
    return response['result']


def updates(client):
    return checked(client,'get_scene')['layers'][0]['shader']['textures']['tex']['feedback']['updates']


def capture_step(renderer,client,step,path):
    current=updates(client)
    if current>step: raise RuntimeError(f'named frame already exceeded: {current}>{step}')
    while current<step:
        osc(renderer.port,'/tex/set',0,'needsUpdate',1)
        deadline=time.monotonic()+3
        while time.monotonic()<deadline and updates(client)==current: time.sleep(.01)
        actual=updates(client)
        if actual!=current+1: raise RuntimeError(f'exact step failed: {current}->{actual}')
        current=actual
    width,height,pixels,receipt=capture(client,path)
    if (width,height)!=(800,600): raise RuntimeError(f'capture dimensions {(width,height)}')
    return bytes(channel for pixel in pixels for channel in pixel),receipt


def export_folder(renderer,folder):
    result=subprocess.run([sys.executable,str(ROOT/'scripts/scene_recipe.py'),'--socket',
        str(renderer.socket),'export',str(folder),'--scd'],cwd=ROOT,capture_output=True,text=True,timeout=30)
    if result.returncode: raise RuntimeError(f'CLI export failed: {result.stdout}\n{result.stderr}')
    return {'return_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr}


def context(performance):
    return {**performance['context'],'feedback_generation':performance['feedback']['feedback_generation']}


def collect_pairs(client,count,limit,destination):
    first=checked(client,'get_performance');identity=context(first)
    if not first['feedback']['supported']: raise RuntimeError('GPU timer unsupported')
    warm_start=first['feedback']['last_attempt_frame'];deadline=time.monotonic()+10
    while True:
        current=checked(client,'get_performance')
        if context(current)!=identity: raise RuntimeError('context changed during warmup')
        if current['feedback']['last_attempt_frame']>=warm_start+120: break
        if time.monotonic()>=deadline: raise RuntimeError('120 warmup frames unavailable in 10s')
        time.sleep(.025)
    baseline=current['feedback']['last_attempt_frame'];baseline_attempts=current['feedback']['attempts'];started=time.monotonic();samples={};polls=[]
    try:
        while len(samples)<count:
            current=checked(client,'get_performance');polls.append(current)
            if context(current)!=identity: raise RuntimeError('context changed during collection')
            for sample in current['feedback']['samples']:
                if sample['frame']>baseline:
                    if samples.setdefault(sample['frame'],sample)!=sample:
                        raise RuntimeError('originating-frame sample changed')
            if time.monotonic()-started>limit:
                raise RuntimeError(f'insufficient pairs: {len(samples)}/{count} in {limit}s')
            time.sleep(.025)
    finally:
        destination.write_text(json.dumps({'context':identity,'baseline_frame':baseline,
            'elapsed_seconds':time.monotonic()-started,'samples':sorted(samples.values(),key=lambda s:s['frame']),
            'polls':polls},indent=2)+'\n')
    selected=sorted(samples.values(),key=lambda s:s['frame'])[:count]
    last_attempt=current['feedback']['last_attempt_frame']
    attempts=current['feedback']['attempts']-baseline_attempts
    if attempts != last_attempt-baseline: raise RuntimeError('originating-frame attempt counter is discontinuous')
    coverage=count/attempts
    sums=sorted(s['update_ms']+s['draw_ms'] for s in selected)
    result={'context':identity,'samples':selected,'eligible_attempts':attempts,'coverage':coverage,
        'last_attempt_frame':last_attempt,'collected_pairs':len(samples),
        'missing_frames':[f for f in range(baseline+1,last_attempt+1) if f not in samples],
        'update_drops':current['feedback']['dropped_samples'],'draw_drops':current['feedback']['draw_dropped_samples'],
        'pending_updates':current['feedback']['pending_queries'],'pending_draws':current['feedback']['draw_pending_queries'],
        'frame_interval_ms':current['frame_interval_ms'],
        'p95_ms':sums[math.ceil(.95*count)-1],'max_ms':max(sums),'raw_path':str(destination)}
    if coverage<.95: raise RuntimeError(f'paired coverage {coverage:.3%} below 95%')
    return result


def collect_demo(renderer, client, output):
    replay(renderer, setup_commands(FIXTURES / 'demo.frag', True, True))
    _, _, before, _ = capture(client, output / 'continuous-demo-before.png')
    demo = collect_pairs(client, 600, 30, output / 'continuous-demo.json')
    _, _, after, _ = capture(client, output / 'continuous-demo-after.png')
    demo['animated'] = before != after
    demo['passed'] = demo['animated'] and demo['p95_ms'] <= 8 and demo['max_ms'] <= 16
    return demo


def native_failure_matrix(renderer, client, output):
    base=setup_commands(FIXTURES/'spatial.frag')
    cases={}
    for label, width, height in (('zero',0.0,600.0),('fractional',1.5,600.0),
            ('wide',801.0,600.0),('tall',800.0,601.0),('overflow',1e30,1e30)):
        cases[label]=[(address,(0,width,height) if address=='/tex/size' else args) for address,args in base]
    cases['missing-size']=[c for c in base if c[0]!='/tex/size']
    cases['missing-format']=[c for c in base if c!=('/tex/set',(0,'internalformat',32856))]
    cases['duplicate-source']=base+[('/tex',(0,'shader_pp:'+str((FIXTURES/'spatial.frag').resolve())))]
    cases['second-instance']=base+[('/tex',(1,'shader_pp:'+str((FIXTURES/'spatial.frag').resolve())))]
    cases['unsupported-target']=[(a,(0,'textureTarget',34037) if a=='/tex/set' and v[1]=='textureTarget' else v) for a,v in base]
    cases['extra-attachment']=base+[('/tex/set',(0,'numColorbuffers',2))]
    cases['history']=base+[('/tex/set',(0,'numFrames',2))]
    cases['expression-control']=base+[('/tex/var',(0,'decay','t'))]
    cases['builtin-control']=base+[('/tex/var',(0,'time',1.0))]
    cases['integer-control']=base+[('/tex/var',(0,'gain',1))]
    temporal=setup_commands(FIXTURES/'temporal.frag')
    cases['wrong-arity']=[(a,(0,'decay',.5,.5) if a=='/tex/var' else v) for a,v in temporal]
    bad=output/'syntax-error.frag';bad.write_text('#version 330\nsyntax failure;\n')
    cases['compile-failure']=setup_commands(bad)
    receipts={}
    for label,commands in cases.items():
        token=str(uuid.uuid4())
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as listener:
            listener.bind(('127.0.0.1',0))
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:
                sender.sendto(osc_bundle(recipe_messages(token,listener.getsockname()[1],commands,
                    final_gate=-1)),('127.0.0.1',renderer.port))
            receipt=receive_results(listener,1)[0]
        if receipt['token']!=token or receipt['status']!='failed':
            raise RuntimeError(f'malformed native replay passed {label}: {receipt}')
        inspection=checked(client,'get_scene')
        for layer in inspection['layers']:
            for texture in layer['shader']['textures'].values():
                if texture.get('feedback',{}).get('updates',0):
                    raise RuntimeError(f'invalid feedback setup executed {label}')
        receipts[label]={'receipt':receipt,'inspection':inspection}
    minimum=[(a,(0,1.0,1.0) if a=='/tex/size' else v) for a,v in base]
    receipts['minimum_1x1']=replay(renderer,minimum)
    receipts['minimum_inspection']=checked(client,'get_scene')
    receipts['recovery']=replay(renderer,base)
    return receipts


def correctness_fixture(args, output, fixture, evidence):
    renderer=None
    port=free_udp_port()
    try:
        renderer=OwnedRenderer(args.binary.resolve(),output,'producer',port,args.expect_sanitizer)
        evidence['producer']=renderer.owner
        evidence['setup']=replay(renderer,setup_commands(FIXTURES/(fixture+'.frag')))
        client=renderer.client()
        try:
            evidence['inspection']=checked(client,'get_scene')
            evidence['export']=checked(client,'export_scene')['recipe']
            original=output/'Exported feedback'
            evidence['cli']=export_folder(renderer,original)
            capture_step(renderer,client,8,output/'producer-dirty-n8.png')
            if fixture=='spatial': evidence['native_failures']=native_failure_matrix(renderer,client,output)
        finally: client.close()
        relocated=output/'Relocated feedback ü with spaces';os.rename(original,relocated)
        expected_descriptor=json.loads(json.dumps(evidence['export']))
        decay=.5
        if fixture=='temporal':
            # Edit the ordinary generated sender and its retained source asset.
            scene=relocated/'scene.scd';text=scene.read_text()
            import re
            text,count=re.subn(r"(visuals\.send\('/tex/var', 0, \"decay\", )0\.5(\);)",r"\g<1>0.75\2",text)
            if count!=1: raise RuntimeError(f'expected one editable decay command: {count}')
            scene.write_text(text);decay=.75
            for command in expected_descriptor['commands']:
                if command['address']=='/tex/var': command['args'][2]['value']=decay
            for asset in expected_descriptor['assets']:
                if asset['kind']=='feedback_fragment':
                    path=relocated/asset['name'];path.write_text(path.read_text()+'\n// edited retained feedback source\n')
                    asset['data']=path.read_text()
        evidence['producer_cleanup'],error=collect_cleanup(renderer);renderer=None
        if error: raise RuntimeError(error)
        if fixture=='spatial': expected=dict(spatial_frames())
        else:
            expected={n:bytes((round(255*decay**n),round(255*max(0,1-n/16)),64,255))*480000 for n in (1,2,4,8)}
        evidence['consumers']=[]
        for index in range(2):
            renderer=OwnedRenderer(args.binary.resolve(),output,f'consumer-{index}',port,args.expect_sanitizer)
            item={'owner':renderer.owner};evidence['consumers'].append(item)
            item['sc']=run_sclang(args.sclang,relocated/'scene.scd',output,port,f'feedback-{index}',args.visuals_quark)
            client=renderer.client()
            try:
                item['frames']={}
                for step in (1,2,4,8):
                    actual,receipt=capture_step(renderer,client,step,output/f'consumer-{index}-n{step}.png')
                    result=compare_png_pixels(actual,expected[step]);item['frames'][str(step)]={**result,'capture':receipt,
                        'sha256':hashlib.sha256(actual).hexdigest()}
                    if result['bad_pixels']: raise RuntimeError(f'{fixture} oracle consumer={index} n={step}: {result}')
                before=updates(client);time.sleep(.25);after=updates(client)
                item['static_hold']={'before':before,'after':after}
                if before!=8 or after!=8: raise RuntimeError('static feedback advanced without needsUpdate')
                item['reexport']=checked(client,'export_scene')['recipe']
                if normalize_descriptor(item['reexport'])!=normalize_descriptor(expected_descriptor):
                    (output/'expected-reexport.json').write_text(json.dumps(expected_descriptor,indent=2))
                    raise RuntimeError('native reexport changed after restart')
                client.close()
                item['dirty_replay']=run_sclang(args.sclang,relocated/'scene.scd',output,port,f'dirty-reset-{index}',args.visuals_quark)
                client=renderer.client()
                actual,_=capture_step(renderer,client,1,output/f'consumer-{index}-reset-n1.png')
                item['dirty_reset']=compare_png_pixels(actual,expected[1])
                if item['dirty_reset']['bad_pixels']: raise RuntimeError('dirty replay did not reset white')
                if args.performance and index==1 and fixture=='spatial':
                    evidence['performance']={'demo': collect_demo(renderer, client, output)}
            finally: client.close()
            item['cleanup'],error=collect_cleanup(renderer);renderer=None
            if error: raise RuntimeError(error)
        evidence['status']='COLLECTED'
        if args.performance and fixture=='spatial' and not evidence['performance']['demo']['passed']:
            evidence['status']='FAILED'
    finally:
        if renderer:
            evidence['final_cleanup'],error=collect_cleanup(renderer)
            if error: evidence['cleanup_error']=error;evidence['status']='FAILED'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('binary','sclang','visuals-quark','output'): parser.add_argument('--'+name,required=True,type=Path)
    parser.add_argument('--expect-sanitizer',action='append',default=[])
    parser.add_argument('--performance',action='store_true')
    parser.add_argument('--fixture',choices=('spatial','temporal','all'),default='all')
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
    if args.performance and args.expect_sanitizer: parser.error('performance requires an ordinary build')
    evidence={'status':'FAILED','source_before':source_snapshot(args.visuals_quark),
        'fixtures':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in FIXTURES.glob('*.frag')},
        'collector_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'gaps':['No allocation/FBO failure injection in this collector',
            'Input/expression/MCP/malformed corpus require separate evidence']}
    (output/'frozen-inputs.json').write_text(json.dumps(evidence,indent=2)+'\n')
    try:
        evidence['runs']={}
        for fixture in ('spatial','temporal') if args.fixture=='all' else (args.fixture,):
            folder=output/fixture;folder.mkdir();run={'status':'FAILED'};evidence['runs'][fixture]=run
            correctness_fixture(args,folder,fixture,run)
        evidence['status']='COLLECTED' if all(r['status']=='COLLECTED' for r in evidence['runs'].values()) else 'FAILED'
    except Exception as error: evidence['error']=repr(error)
    finally:
        evidence['source_after']=source_snapshot(args.visuals_quark)
        evidence['source_stable']=evidence['source_before']==evidence['source_after']
        if not evidence['source_stable']: evidence['status']='FAILED'
        (output/'evidence.json').write_text(json.dumps(evidence,indent=2)+'\n')
    return 0 if evidence['status']=='COLLECTED' else 1


if __name__=='__main__': sys.exit(main())
