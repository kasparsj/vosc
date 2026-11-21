#!/usr/bin/env python3
"""Private feedback composition, synthetic input/expression, restart and order proof."""
import argparse
import hashlib
import json
from pathlib import Path
import time
from test_feedback_composition import setup_commands, replay, checked, FIXTURES, export_folder
from test_scene_recipe import (OwnedRenderer, free_udp_port, capture, collect_cleanup,
    osc, run_sclang, normalize_descriptor, swap_layer_order, source_snapshot)


def picture(renderer,path):
    client=renderer.client()
    try:
        width,height,pixels,receipt=capture(client,path)
        scene=checked(client,'get_scene')
    finally:client.close()
    if (width,height)!=(800,600):raise AssertionError('expected800x600')
    return pixels,{'capture':receipt,'scene':scene,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}


def sample(renderer,value,path):
    osc(renderer.port,'/input/data',0,value,0.,0);time.sleep(.2)
    return picture(renderer,path)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('binary','sclang','visuals-quark','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--expect-sanitizer',action='append',default=[])
    args=parser.parse_args();out=args.output.resolve();out.mkdir(mode=0o700)
    e={'status':'FAILED','source_before':source_snapshot(args.visuals_quark)};renderer=None;port=free_udp_port()
    try:
        renderer=OwnedRenderer(args.binary.resolve(),out,'mixed-producer',port,args.expect_sanitizer)
        commands=setup_commands(FIXTURES/'spatial.frag',False,True)
        commands[1:1]=[('/input',(0,'',0,256)),('/input/set',(0,'maxAmp',1.)),('/var',('base',.25)),('/var',('mod',0.)),('/var',('mod','base+in_0*.5'))]
        commands=[(a,(1,'mod') if a=='/layer/alpha' else v) for a,v in commands]
        e['setup']=replay(renderer,commands);osc(port,'/onset',0);e['producer']={};baseline={}
        client=renderer.client()
        try:e['descriptor']=checked(client,'export_scene')['recipe']
        finally:client.close()
        for value in (.2,.8):
            baseline[value],e['producer'][str(value)]=sample(renderer,value,out/f'producer-{value}.png')
        if baseline[.2]==baseline[.8]:raise AssertionError('synthetic input did not change feedback composition')
        osc(port,'/layer/visible',1,0);hidden,e['hidden_gradient']=picture(renderer,out/'feedback-only.png')
        osc(port,'/layer/visible',0,0);blank,e['both_hidden']=picture(renderer,out/'both-hidden.png')
        if any(any(p[c]>2 for c in range(3)) for p in blank):raise AssertionError('both hidden not black')
        osc(port,'/layer/visible',1,1);gradient,e['hidden_feedback']=picture(renderer,out/'gradient-only.png')
        if gradient==blank or hidden==blank:raise AssertionError('individual layer did not contribute')
        osc(port,'/layer/visible',0,1)
        alpha=e['producer']['0.8']['scene']['layers'][1]['alpha'];bad=0
        for row in range(600):
            for x in range(800):
                i=row*800+x;g=((x+.5)/800,.2,(599-row+.5)/600)
                predicted=[(1-alpha)*hidden[i][c]+alpha*255*g[c] for c in range(3)]
                bad+=max(abs(baseline[.8][i][c]-predicted[c]) for c in range(3))>2
        if bad:raise AssertionError(f'mixed input blend oracle failed{bad}')
        e['blend_oracle_bad_pixels']=bad
        folder=out/'Mixed feedback ā';e['cli']=export_folder(renderer,folder)
        client=renderer.client()
        try:e['post_sample_descriptor']=checked(client,'export_scene')['recipe']
        finally:client.close()
        if normalize_descriptor(e['descriptor'])!=normalize_descriptor(e['post_sample_descriptor']):raise AssertionError('sample history changed exported mappings')
        e['producer_cleanup'],error=collect_cleanup(renderer);renderer=None
        if error:raise RuntimeError(error)
        renderer=OwnedRenderer(args.binary.resolve(),out,'mixed-consumer',port,args.expect_sanitizer)
        e['replay']=run_sclang(args.sclang,folder/'scene.scd',out,port,'mixed',args.visuals_quark);e['consumer']={}
        for value in (.2,.8):
            pixels,e['consumer'][str(value)]=sample(renderer,value,out/f'consumer-{value}.png')
            if pixels!=baseline[value]:raise AssertionError('same synthetic input differs after restart')
        client=renderer.client()
        try:e['reexport']=checked(client,'export_scene')['recipe']
        finally:client.close()
        if normalize_descriptor(e['reexport'])!=normalize_descriptor(e['descriptor']):raise AssertionError('mixed mappings changed on reexport')
        swapped=swap_layer_order(folder,out/'Swapped mixed feedback')
        e['swapped_replay']=run_sclang(args.sclang,swapped,out,port,'swapped-feedback',args.visuals_quark)
        swapped_pixels,e['swapped']=sample(renderer,.8,out/'swapped.png')
        osc(port,'/layer/visible',0,0);top_feedback,e['swapped_hidden_gradient']=picture(renderer,out/'swapped-feedback-only.png')
        if swapped_pixels!=top_feedback or swapped_pixels==baseline[.8]:raise AssertionError('order remap did not put opaque feedback above gradient')
        e['status']='PASS'
    except Exception as error:e['error']=repr(error)
    finally:
        if renderer:
            e['consumer_cleanup'],error=collect_cleanup(renderer)
            if error:e['cleanup_error']=error;e['status']='FAILED'
        e['source_after']=source_snapshot(args.visuals_quark);e['source_stable']=e['source_before']==e['source_after']
        if not e['source_stable']:e['status']='FAILED'
        (out/'evidence.json').write_text(json.dumps(e,indent=2)+'\n')
    print(e['status'],e.get('error'));return 0 if e['status']=='PASS' else 1

if __name__=='__main__':raise SystemExit(main())
