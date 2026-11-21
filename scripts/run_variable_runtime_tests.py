#!/usr/bin/env python3
"""Link focused Variable tests against an existing isolated macOS app build."""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    args = parser.parse_args()
    lines = (args.build / 'build.log').read_text().splitlines()
    compile_args = next(shlex.split(line.strip()) for line in lines
                        if ' -c ' in line and '/Variable.cpp ' in line)
    link_args = next(shlex.split(line.strip()) for line in reversed(lines)
                     if ' -filelist ' in line and ' -o ' in line)
    with tempfile.TemporaryDirectory(prefix='vosc-variable-tests-') as temporary:
        output = Path(temporary)
        (output / 'MacOS').mkdir()
        frameworks = args.build.resolve() / 'products/voscDebug.app/Contents/Frameworks'
        (output / 'Frameworks').symlink_to(frameworks)
        source = ROOT / 'tests/osc/VariableRuntimeTests.cpp'
        compile_args[compile_args.index('-c') + 1] = str(source)
        compile_args[compile_args.index('-o') + 1] = str(output / 'tests.o')
        # Redirect all compiler artifacts away from the original native build.
        for flag, name in [('-MF', 'tests.d'), ('--serialize-diagnostics', 'tests.dia')]:
            if flag in compile_args:
                compile_args[compile_args.index(flag) + 1] = str(output / name)
        subprocess.run(compile_args, cwd=ROOT, check=True)
        filelist_index = link_args.index('-filelist') + 1
        objects = Path(link_args[filelist_index]).read_text().splitlines()
        objects = [item for item in objects if Path(item).name != 'main.o']
        objects.append(str(output / 'tests.o'))
        filelist = output / 'objects.list'
        filelist.write_text('\n'.join(objects) + '\n')
        link_args[filelist_index] = str(filelist)
        link_args[link_args.index('-o') + 1] = str(output / 'MacOS/tests')
        subprocess.run(link_args, cwd=ROOT, check=True)
        subprocess.run([str(output / 'MacOS/tests')], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
