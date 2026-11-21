"""Actual generated feedback folder consumption with isolated checkout SC."""
from pathlib import Path
import tempfile
import unittest
import unicodedata
from test_feedback_recipe import feedback_descriptor, cli
from test_scene_recipe_sclang import SCLANG, _Renderer, _run_scene, _bundle_messages

@unittest.skipUnless(SCLANG.is_file(), 'SuperCollider unavailable')
class FeedbackSclangTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.receipts=Path(tempfile.mkdtemp(prefix='vosc-feedback-generated-sc-receipts-'))
        print('FEEDBACK_GENERATED_SC_RECEIPTS='+str(cls.receipts))

    def record(self,label,output,packet=None,**extra):
        import json
        from test_scene_recipe_sclang import quark_checkout_path, quark_source_identity
        selected=extra.pop('quark_path',quark_checkout_path())
        value={'output':output,'packet_hex':packet.hex() if packet else None,
               'quark':quark_source_identity(selected) if selected else None,**extra}
        (self.receipts/(label+'.json')).write_text(json.dumps(value,indent=2,ensure_ascii=False)+'\n')

    def test_generated_relocated_feedback_wire(self):
        with tempfile.TemporaryDirectory(prefix='vosc-feedback-sc-') as temp:
            original=Path(temp)/'original'
            cli.write_folder(original,feedback_descriptor(),scd=True)
            relocated=Path(temp)/'Unicode ā space'
            original.rename(relocated)
            scene=relocated/'scene.scd'
            renderer=_Renderer();renderer.start()
            code,output,pid=_run_scene(scene,renderer.port)
            renderer.join(timeout=3)
            self.assertEqual(code,0,output)
            self.assertIsNone(renderer.error)
            self.assertIn('VOSC_SCENE_RESULT sent',output)
            messages=[m for m in _bundle_messages(renderer.packet or b'') if m[0]=='/scene/command']
            sources=[m for m in messages if m[2][2]=='/tex']
            self.assertEqual(len(sources),1)
            self.assertEqual(unicodedata.normalize('NFC', sources[0][2][-1]),unicodedata.normalize('NFC', 'shader_pp:'+str((relocated/'assets/layer-0.frag').resolve())))
            controls=[m for m in messages if m[2][2]=='/tex/var']
            self.assertEqual(controls[0][1],',sisisff')
            self.assertEqual(controls[0][2][-2:],[.5,.25])
            self.record('relocated-wire',output,renderer.packet)

    def test_generated_feedback_distinct_renderer_root(self):
        import json
        with tempfile.TemporaryDirectory(prefix='vosc-feedback-roots-') as temp:
            folder=Path(temp)/'local ā space';cli.write_folder(folder,feedback_descriptor(),scd=True)
            renderer_root='/private/renderer separate λ root'
            scene=folder/'scene.scd'
            scene.write_text(scene.read_text().replace('var rendererRoot = sceneRoot;',f'var rendererRoot = {json.dumps(renderer_root,ensure_ascii=False)};'))
            renderer=_Renderer();renderer.start()
            code,output,_=_run_scene(scene,renderer.port);renderer.join(timeout=3)
            self.assertEqual(code,0,output);self.assertIsNone(renderer.error)
            messages=[m for m in _bundle_messages(renderer.packet or b'') if m[0]=='/scene/command']
            source=next(m for m in messages if m[2][2]=='/tex')
            self.assertEqual(source[2][-1],'shader_pp:'+renderer_root+'/assets/layer-0.frag')
            self.assertTrue((folder/'assets/layer-0.frag').is_file())
            self.record('distinct-renderer-root',output,renderer.packet,renderer_root=renderer_root)

    def test_feedback_feature_gate_rejects_missing_old_and_incompatible_before_packets(self):
        import re
        import shutil
        import socket
        from test_scene_recipe_sclang import quark_checkout_path
        selected=quark_checkout_path()
        with tempfile.TemporaryDirectory(prefix='vosc-feedback-feature-') as temp:
            root=Path(temp)
            cases=[('missing',None,True)]
            for label,replacement in [('old',''),('incompatible','*sceneFeedbackReplayVersion { ^2 }')]:
                fixture=root/label;fixture.mkdir()
                for source in selected.glob('*.sc'):shutil.copyfile(source,fixture/source.name)
                main=fixture/'OSCVisuals.sc';source=main.read_text()
                source,count=re.subn(r'\*sceneFeedbackReplayVersion\s*\{\s*\^1\s*\}',replacement,source)
                self.assertEqual(count,1);main.write_text(source);cases.append((label,fixture,False))
            for label,quark,stock in cases:
                with self.subTest(case=label):
                    folder=root/('scene-'+label);cli.write_folder(folder,feedback_descriptor(),scd=True)
                    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as listener:
                        listener.bind(('127.0.0.1',0));listener.settimeout(.15)
                        code,output,_=_run_scene(folder/'scene.scd',listener.getsockname()[1],quark=quark,stock_only=stock)
                        self.assertEqual(code,0,output)
                        self.assertNotIn('VOSC_SCENE_RESULT sent',output)
                        if label!='missing':self.assertIn('feedback replay API 1 is required',output)
                        with self.assertRaises(socket.timeout):listener.recvfrom(65535)
                        self.record('feature-'+label,output,packet_count=0,quark_path=quark)
