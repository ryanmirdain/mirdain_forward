"""Build the real drag-and-drop DMG from an existing Forward app bundle."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import dmgbuild
from ds_store import DSStore
from mac_alias import Alias

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('app',type=Path)
parser.add_argument('--output',type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[3]
assets=Path(__file__).resolve().parent
output=args.output or root/'dist/mirdain-forward-local-grid.dmg'
if output.exists(): raise SystemExit(f'Output already exists: {output}')
subprocess.run(['codesign','--verify','--deep','--strict',str(args.app)],check=True)
state={}
def prepare(mount,options):
    mount=Path(mount);state['mount']=mount
    subprocess.run(['codesign','--verify','--deep','--strict',str(mount/'Mirdain Forward.app')],check=True)
    # Match CPack's background location so the same saved layout works in release builds.
    background=mount/'.background';background.mkdir()
    shutil.move(mount/'.background.tiff',background/'background.tiff')

def progress(event):
    if event.get('operation')=='dsstore::create' and event.get('type')=='operation::finished':
        mount=state['mount']
        with DSStore.open(str(mount/'.DS_Store'),'r+') as store:
            view=store['.']['icvp']
            view['backgroundImageAlias']=Alias.for_file(str(mount/'.background/background.tiff')).to_bytes()
            store['.']['icvp']=view
        shutil.copyfile(mount/'.DS_Store',assets/'Finder.DS_Store')
    if event.get('type')=='operation::finished':
        print(event.get('operation'),flush=True)

with tempfile.TemporaryDirectory(prefix='mirdain-dmg-') as temp:
    # Avoid file-provider metadata from the synced source checkout entering signed bundles.
    payload=Path(temp)/'Mirdain Forward.app'
    subprocess.run(['ditto',str(args.app),str(payload)],check=True)
    subprocess.run(['codesign','--verify','--deep','--strict',str(payload)],check=True)
    licenses=Path(temp)/'Licenses';licenses.mkdir()
    shutil.copyfile(root/'LICENSE',licenses/'LICENSE')
    shutil.copyfile(root/'packaging/OpenSSL-LICENSE.txt',licenses/'OpenSSL-LICENSE.txt')
    dmgbuild.build_dmg(str(output),'Mirdain Forward Setup',settings={
        'format':'UDZO', 'filesystem':'HFS+',
        'files':[str(payload),str(licenses)],
        'symlinks':{'Applications':'/Applications'},
        'hide':['Licenses'],
        'background':str(assets/'background.tiff'),
        'icon':str(root/'assets/Icon.icns'),
        'window_rect':((180,140),(720,500)),
        'icon_size':96, 'text_size':13,
        'icon_locations':{'Mirdain Forward.app':(190,231),'Applications':(530,231)},
        'show_toolbar':False,'show_sidebar':False,'show_status_bar':False,
        'show_pathbar':False,'show_tab_view':False,
        'create_hook':prepare,
    },callback=progress)
print(output)
