"""Run serial comparison JS tests with Node or Arduino IDE's bundled Node."""
import os
from pathlib import Path
import shutil
import subprocess
from common import ROOT, CREATE_FLAGS

if __name__ == '__main__':
    node = shutil.which('node')
    env = dict(os.environ)
    if not node:
        node = str(Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'Arduino IDE/Arduino IDE.exe')
        if not Path(node).exists():
            raise SystemExit('Install Node.js or Arduino IDE to execute the JavaScript tests.')
        env['ELECTRON_RUN_AS_NODE'] = '1'
    result = subprocess.run([node, '--preserve-symlinks', '--preserve-symlinks-main',
                             str(ROOT / 'windows/test_serial_compare.js')],
                            env=env, creationflags=CREATE_FLAGS, capture_output=True,
                            text=True, encoding='utf-8', errors='replace', timeout=30)
    print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='')
    if 'PASS browser scripts parse' not in result.stdout:
        raise SystemExit('JavaScript runner did not finish the test suite.')
    raise SystemExit(result.returncode)
