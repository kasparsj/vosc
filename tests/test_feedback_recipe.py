"""Feedback descriptor and generated-source boundaries, independent of native GL."""
import copy
import sys
import tempfile
from pathlib import Path
import unittest
from test_scene_recipe_schema import arg, descriptor, cli
from scene_recipe_schema import validate_descriptor, RecipeValidationError


def feedback_descriptor():
    result = descriptor()
    result['assets'][0]['kind'] = 'feedback_fragment'
    result['commands'][1]['address'] = '/tex'
    commands = result['commands']
    commands.append({'address': '/tex/size', 'args': [arg('i', 0), arg('f', 800.0), arg('f', 600.0)]})
    commands.append({'address': '/tex/speed', 'args': [arg('i', 0), arg('f', 1.0)]})
    for name, value in [('internalformat',32856), ('textureTarget',3553), ('filter',9729), ('aspectRatio',1), ('static',1), ('blendMode',0)]:
        commands.append({'address':'/tex/set', 'args':[arg('i',0),arg('s',name),arg('i',value)]})
    commands.append({'address':'/tex/var','args':[arg('i',0),arg('s','gain'),arg('f',0.5),arg('f',0.25)]})
    return result


class FeedbackRecipeTests(unittest.TestCase):
    def test_valid_and_generated_prefix_feature_vector(self):
        value = feedback_descriptor()
        validate_descriptor(value)
        source = cli.render_scene(value)
        self.assertIn('"shader_pp:assets/layer-0.frag"', source)
        self.assertLess(source.index('sceneFeedbackReplayVersion'), source.index('visuals = OSCVisuals'))
        self.assertIn("visuals.send('/tex/var', 0, \"gain\", [0.5, 0.25])",source)
        self.assertEqual(value['commands'][1]['args'][1]['value'],'assets/layer-0.frag')

    def test_bounds_missing_storage_and_constant_types(self):
        base = feedback_descriptor()
        cases = []
        for width, height in [(0.,600.),(800.5,600.),(801.,600.),(800.,601.)]:
            value=copy.deepcopy(base)
            value['commands'][4]['args'][1:]=[arg('f',width),arg('f',height)]
            cases.append(value)
        for name in ['internalformat','textureTarget','filter','static']:
            value=copy.deepcopy(base)
            value['commands']=[c for c in value['commands'] if not(c['address']=='/tex/set' and c['args'][1]['value']==name)]
            cases.append(value)
        for kind,value in [('i',1),('s','t')]:
            item=copy.deepcopy(base);item['commands'][-1]['args'][-1]=arg(kind,value);cases.append(item)
        for value in cases:
            with self.subTest(value=value),self.assertRaises(RecipeValidationError): validate_descriptor(value)

    def test_kind_misuse_and_second_instance(self):
        value=feedback_descriptor();value['commands'][1]['address']='/shader'
        with self.assertRaises(RecipeValidationError):validate_descriptor(value)
        value=feedback_descriptor();value['commands'][0]['args'][0]['value']=2
        command=copy.deepcopy(value['commands'][1]);command['args'][0]['value']=1
        value['commands'].append(command)
        with self.assertRaises(RecipeValidationError):validate_descriptor(value)

    def test_control_bound_duplicate_and_builtin(self):
        for name in ['time','srctex','gain']:
            value=feedback_descriptor()
            value['commands'].append({'address':'/tex/var','args':[arg('i',0),arg('s',name),arg('f',1.0)]})
            with self.assertRaises(RecipeValidationError):validate_descriptor(value)
        value=feedback_descriptor()
        for i in range(64):value['commands'].append({'address':'/tex/var','args':[arg('i',0),arg('s',f'u{i}'),arg('f',1.0)]})
        with self.assertRaises(RecipeValidationError):validate_descriptor(value)


@unittest.skipUnless(sys.platform == "darwin", "scene-folder publication requires macOS atomic rename")
class FeedbackCliPublicationTests(unittest.TestCase):
    """Real CLI processes receive descriptors over a private Unix API socket."""
    def test_feedback_cli_publication_faults_preserve_foreign_content(self):
        import hashlib
        import json
        import os
        import socket
        import subprocess
        import sys
        import threading

        root = Path(__file__).resolve().parents[1]
        receipts = []
        cases = ('success', 'existing', 'symlink', 'unsafe_asset', 'shared_parent',
                 'feedback_write', 'scene_write', 'readme_write', 'feedback_partial',
                 'scene_partial', 'readme_partial', 'rename', 'race')
        bootstrap = r'''
import importlib.util,sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('feedback_cli',sys.argv[1]);cli=importlib.util.module_from_spec(spec);spec.loader.exec_module(cli)
mode=sys.argv[2];destination=Path(sys.argv[4]);real_open=Path.open;real_rename=cli._rename_exclusive
filename={'feedback_write':'layer-0.frag','scene_write':'scene.scd','readme_write':'README.md',
          'feedback_partial':'layer-0.frag','scene_partial':'scene.scd','readme_partial':'README.md'}.get(mode)
class PartialWriter:
    def __init__(self,stream):self.stream=stream
    def __enter__(self):self.stream.__enter__();return self
    def __exit__(self,*args):return self.stream.__exit__(*args)
    def __getattr__(self,name):return getattr(self.stream,name)
    def write(self,data):
        written=self.stream.write(data[:max(1,len(data)//2)]);self.stream.flush()
        raise OSError('injected '+mode+' after '+str(written)+' units')
def checked_open(path,*args,**kwargs):
    if filename and path.name==filename and '.scene.staging-' in str(path) and (args[0] if args else kwargs.get('mode')) in ('w','xb'):
        if mode.endswith('_partial'):return PartialWriter(real_open(path,*args,**kwargs))
        raise OSError('injected '+mode)
    return real_open(path,*args,**kwargs)
def checked_rename(source,target,**kwargs):
    if mode=='rename':raise OSError('injected rename')
    if mode=='race':
        target.mkdir();(target/'foreign.txt').write_text('concurrent foreign data')
    return real_rename(source,target,**kwargs)
Path.open=checked_open;cli._rename_exclusive=checked_rename
sys.argv=[sys.argv[1],'--socket',sys.argv[3],'export',str(destination)]
raise SystemExit(cli.main())
'''
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory(prefix='vosc-fb-cli-') as temporary:
                private = Path(temporary); parent = private / 'publication'; parent.mkdir(mode=0o700)
                (parent / 'unrelated.txt').write_text('preserved unrelated content')
                destination = parent / 'scene'; endpoint = private / 'engine.sock'
                descriptor_value = feedback_descriptor()
                if case == 'existing':
                    destination.mkdir(); (destination / 'foreign.txt').write_text('existing foreign data')
                if case == 'symlink': destination.symlink_to(parent / 'unrelated.txt')
                if case == 'shared_parent': parent.chmod(0o777)
                if case == 'unsafe_asset':
                    descriptor_value['assets'][0]['name'] = 'assets/../escape.frag'
                    descriptor_value['commands'][1]['args'][1]['value'] = 'assets/../escape.frag'
                def inventory():
                    result = {}
                    for path in sorted(parent.rglob('*')):
                        name = str(path.relative_to(parent))
                        if path.is_symlink(): result[name] = {'symlink':os.readlink(path)}
                        elif path.is_file(): result[name] = hashlib.sha256(path.read_bytes()).hexdigest()
                        else: result[name] = 'directory'
                    return result
                before = inventory(); server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                server.bind(str(endpoint)); endpoint.chmod(0o600); server.listen(1); server.settimeout(10)
                errors = []; requests = []
                def serve():
                    try:
                        connection,_ = server.accept()
                        with connection:
                            stream=connection.makefile('rb'); request=json.loads(stream.readline());requests.append(request)
                            response={'id':request['id'],'ok':True,'status':'ok','revision':1,'frame':2,'external_epoch':0,'result':{'recipe':descriptor_value}}
                            connection.sendall(json.dumps(response).encode()+b'\n')
                    except BaseException as error: errors.append(repr(error))
                    finally:server.close()
                thread=threading.Thread(target=serve,daemon=True);thread.start()
                run=subprocess.run([sys.executable,'-c',bootstrap,str(root/'scripts/scene_recipe.py'),case,str(endpoint),str(destination)],capture_output=True,text=True,timeout=15)
                thread.join(2)
                self.assertFalse(thread.is_alive());self.assertEqual(errors,[])
                self.assertEqual([r['method'] for r in requests],['export_scene'])
                self.assertEqual(run.returncode,0 if case=='success' else 2,run.stdout+run.stderr)
                if case in ('feedback_write','scene_write','readme_write','feedback_partial','scene_partial','readme_partial','rename'):
                    self.assertIn('injected '+case,run.stdout)
                after=inventory()
                for name,value in before.items():self.assertEqual(after[name],value)
                self.assertFalse(any('.staging-' in name for name in after))
                if case=='success':
                    relocated=parent/'Relocated ā folder';destination.rename(relocated)
                    self.assertEqual((relocated/'assets/layer-0.frag').read_text(),descriptor_value['assets'][0]['data'])
                    self.assertIn('shader_pp:assets/layer-0.frag',(relocated/'scene.scd').read_text())
                elif case=='race':self.assertEqual((destination/'foreign.txt').read_text(),'concurrent foreign data')
                elif case not in ('existing','symlink'):self.assertFalse(destination.exists())
                receipts.append({'case':case,'return_code':run.returncode,'stdout':run.stdout,'stderr':run.stderr,'before':before,'after':after})
                parent.chmod(0o700)
        receipt_root=Path(tempfile.mkdtemp(prefix='vosc-feedback-cli-receipts-'))
        (receipt_root/'evidence.json').write_text(json.dumps(receipts,indent=2)+'\n')
        print('FEEDBACK_CLI_RECEIPTS='+str(receipt_root))

if __name__=='__main__':unittest.main()
