#!/usr/bin/env python3
"""Generate the five SVG design boards and an offline, clickable review prototype.

Python standard library only. Optional PNG export: inkscape <file.svg> -o <file.png>.
This is a design artifact; it does not implement the Android menu or game saving.
"""
from pathlib import Path
from html import escape
import json

ROOT = Path(__file__).resolve().parent
BG = '#101513'
PANEL = '#191f1c'
SURFACE = '#222a25'
STROKE = '#414b41'
INK = '#f0eee5'
MUTED = '#a6afa3'
GOLD = '#d7bc7d'
ACTIVE = '#45432e'
PARTS = []

def raw(s): PARTS.append(s)
def rect(x,y,w,h,fill,stroke='none',radius=6,extra=''):
    raw(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" fill="{fill}" stroke="{stroke}" {extra}/>')
def line(x1,y1,x2,y2,color=STROKE):
    raw(f'<path d="M{x1} {y1}H{x2}" stroke="{color}"/>' if y1==y2 else f'<path d="M{x1} {y1}L{x2} {y2}" stroke="{color}"/>')
def txt(x,y,s,size=18,color=INK,weight=400,extra=''):
    raw(f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}" font-weight="{weight}" {extra}>{escape(s)}</text>')
def small(x,y,s): txt(x,y,s,14,MUTED,600,'letter-spacing="1.2"')
def button(x,y,w,label,selected=False,action='',h=48,disabled=False,group='',value=''):
    attrs = f'data-action="{action}"' if action else ''
    if group: attrs += f' data-group="{group}" data-value="{value}" aria-pressed="{str(selected).lower()}"'
    raw(f'<g class="button {"selected" if selected else ""}" role="button" tabindex="{ -1 if disabled else 0}" aria-label="{escape(label)}" aria-disabled="{str(disabled).lower()}" {attrs} opacity="{.4 if disabled else 1}">')
    rect(x,y,w,h,ACTIVE if selected else SURFACE,GOLD if selected else STROKE)
    txt(x+w/2,y+h/2+6,label,17,GOLD if selected else INK,600,'text-anchor="middle"')
    raw('</g>')
def choices(y,label,labels,selected,group,width=650):
    txt(178,y+30,label,17)
    gap=8
    w=(width-gap*(len(labels)-1))/len(labels)
    for i,l in enumerate(labels): button(452+i*(w+gap),y,w,l,i==selected,group=group,value=str(i))
def toggle(x,y,w,label,on,group):
    raw(f'<g class="button toggle {"selected" if on else ""}" role="button" tabindex="0" aria-label="{escape(label)}" aria-pressed="{str(on).lower()}" data-group="{group}" data-toggle="true">')
    rect(x,y,w,54,ACTIVE if on else SURFACE,GOLD if on else STROKE)
    txt(x+18,y+33,label,17,INK,500)
    txt(x+w-18,y+33,'AN' if on else 'AUS',12,GOLD if on else MUTED,700,'text-anchor="end" class="state"')
    raw('</g>')
def base(title,subtitle,page):
    PARTS.clear()
    raw('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1280 720" width="1280" height="720" font-family="DejaVu Sans, sans-serif">')
    raw('<defs><radialGradient id="fog"><stop stop-color="#3b4639"/><stop offset="1" stop-color="#111813"/></radialGradient></defs>')
    rect(0,0,1280,720,'url(#fog)',radius=0)
    # Abstract backdrop, deliberately not an in-game screenshot.
    for i,(x,w) in enumerate([(31,29),(95,17),(204,25),(338,12),(1019,18),(1140,33),(1234,22)]):
        raw(f'<path d="M{x} 0 L{x+w} 0 L{x+w+18} 720 L{x-18} 720 Z" fill="#0c130f" opacity=".65"/>')
        line(x,210,x-78 if i%2 else x+110,80,'#17201a')
    rect(0,0,1280,720,'#080b09',radius=0,extra='opacity=".46"')
    small(144,29,'CUBEEVIL4  /  GUI-KONZEPT 01')
    txt(1136,29,'ENTWURF · BEISPIELDATEN',11,MUTED,500,'text-anchor="end"')
    raw('<g opacity=".90">')
    rect(140,48,1000,626,'#050806',radius=12,extra='opacity=".6" transform="translate(0 8)"')
    rect(140,48,1000,626,PANEL,STROKE,10)
    rect(140,48,1000,4,GOLD,radius=0)
    if page!='home': button(174,74,118,'‹  Zurück',action='home',h=44)
    tx=174 if page=='home' else 330
    txt(tx,101,title,28,INK,600)
    txt(tx,126,subtitle,15,MUTED)
    button(925,76,181,'Weiter spielen',action='resume',h=44)
    line(174,148,1106,148)
    line(174,620,1106,620)
    txt(178,650,'D-PAD  Navigieren     A  Auswählen',15,MUTED)
    txt(1102,650,'B  '+('Zum Spiel' if page=='home' else 'Zurück')+'     SELECT / START  Zum Spiel',15,MUTED,400,'text-anchor="end"')

def finish(name):
    raw('</g></svg>')
    s='\n'.join(PARTS)
    (ROOT/(name+'.svg')).write_text(s,encoding='utf-8')
    return s

def home():
    base('CubeEvil4 Ingame-Menü','','home')
    txt(570,100,'(v0.90.1-beta)',15,MUTED)
    small(178,180,'INSTANT-SPIELSTAND')
    rect(170,196,940,112,'#202720',STROKE,6)
    txt(198,229,'Speicherslot',21,INK,600)
    txt(198,256,'Kapitel 1–1 · r100 · Spielzeit 00:12:34',15,MUTED)
    txt(198,283,'Zuletzt gespeichert: 24.10.2026, 12:24',15,MUTED,400,'id="save-meta"')
    button(648,228,225,'Speichern',True,'save')
    button(881,228,225,'Laden',False,'load')
    small(178,355,'EINSTELLUNGEN & VERWALTUNG')
    for x,y,n,sub,action in [(174,372,'Anzeige','Auflösung, Bildformat & Effekte','display'),(648,372,'Steuerung','Touch, Gyro & Komfortsteuerung','controls'),(174,452,'Performance','Leistungskerne, 60 FPS & Anzeige','performance'),(648,452,'Management','Speicherkarte, Berichte & Quelle','management')]:
        raw(f'<g class="button nav" role="button" tabindex="0" aria-label="{n}" data-action="{action}">')
        rect(x,y,458,66,SURFACE,STROKE)
        txt(x+20,y+28,n,23,INK,700)
        txt(x+20,y+51,sub,15,MUTED,400)
        txt(x+430,y+40,'›',28,GOLD)
        raw('</g>')
    return finish('01-hauptseite')

def display():
    base('Anzeige','Bildqualität & Darstellung','display')
    small(178,179,'BILDQUALITÄT')
    choices(196,'Auflösung',['Dynamisch','1×','2×','3×','4×'],0,'scale')
    txt(452,264,'Beispiel: 1280 × 672 Pixel · Dynamisch · Bildschirm 1920 × 1080',15,MUTED)
    choices(283,'Kantenglättung',['Aus','2× MSAA','4× MSAA'],0,'samples')
    choices(346,'Texturfilter',['Aus','2×','4×','8×','16×'],0,'anisotropy')
    small(178,429,'DARSTELLUNG')
    choices(446,'Bildformat',['4:3 (GC)','4:3','16:9','Gestreckt'],2,'aspect')
    toggle(452,514,650,'Tiefenschärfe',True,'dof')
    txt(178,598,'Auflösung, Kantenglättung und Filter gelten nach dem Schließen des Menüs.',15,MUTED)
    return finish('02-display')

def controls():
    base('Steuerung','Touch, Controller & Zielen','controls')
    choices(176,'Touchsteuerung',['Auto','Erzwingen','Unterdrücken'],0,'touch')
    txt(178,251,'Auto: ohne Controller / nach Antippen. Erzwingen: immer. Unterdrücken: bei Controller aus.',15,MUTED)
    small(178,287,'KOMFORTSTEUERUNG · UNABHÄNGIG KOMBINIERBAR')
    toggle(174,305,458,'Alternative Steuerung',False,'alternate')
    toggle(648,305,458,'Messer per Wischen',True,'knife')
    toggle(174,371,458,'L und R einrasten',False,'latchLR')
    toggle(648,371,458,'B einrasten',False,'latchB')
    toggle(174,437,458,'Gyro-Zielen',True,'gyro')
    txt(877,455,'Gyro-Empfindlichkeit',14,MUTED,400,'text-anchor="middle"')
    line(666,479,1000,479,STROKE)
    line(666,479,810,479,GOLD)
    raw(f'<g role="slider" tabindex="0" aria-label="Gyro-Empfindlichkeit" aria-valuemin="0.5" aria-valuemax="3" aria-valuenow="1" data-slider="true"><rect x="666" y="464" width="334" height="30" fill="none" pointer-events="all"/><rect class="thumb" x="727" y="468" width="12" height="22" rx="3" fill="{GOLD}"/></g>')
    txt(1090,484,'1×',18,INK,500,'text-anchor="end" id="sensitivity-value"')
    txt(178,531,'Alternative Steuerung: L1 Messer · L2 Zielen / rechter Stick · R1 Inventar · R2 Aktion · Y Karte.',15,MUTED)
    txt(178,555,'Einrasten: einmal drücken hält L/R oder B, erneut drücken löst. Gilt auch für Controller.',15,MUTED)
    txt(178,579,'Messer per Wischen: schnell über das Spielbild wischen; vertikal bestimmt die Schnitthöhe.',15,MUTED)
    return finish('03-controls')

def performance():
    base('Performance','Leistung & Bildausgabe','performance')
    small(178,179,'PROZESSOR')
    choices(199,'Leistung erzwingen',['Standard','Leistungskerne nutzen'],0,'fastCores')
    txt(452,269,'Standard: Android entscheidet über die verwendeten Kerne.',15,MUTED)
    txt(452,293,'Leistungskerne können helfen, benötigen mehr Energie und erzeugen mehr Wärme.',15,MUTED)
    txt(178,401,'Bildausgabe',17)
    toggle(452,368,650,'FPS-Anzeige unten links',False,'fps')
    button(452,434,650,'Frame-Skip',disabled=True,h=54)
    toggle(452,500,650,'60 FPS (experimentell)',False,'smooth60')
    txt(452,579,'Zusätzliche Zwischenbilder. Spiellogik weiterhin 30 Bilder/s.',15,MUTED)
    return finish('04-performance')

def management():
    base('Management','Speicherkarte, Diagnose & Projekt','management')
    small(178,179,'SPEICHERKARTE · STECKPLATZ A')
    txt(178,206,'Schreibmaschinen-Spielstände als .gci für Dolphin oder GameCube.',15,MUTED)
    button(174,223,458,'Spielstände exportieren (.gci)',action='export')
    button(648,223,458,'Spielstände importieren (.gci)',action='import')
    small(178,309,'FEHLERPROTOKOLL')
    toggle(174,327,458,'Fehlerprotokoll aufzeichnen',True,'log')
    toggle(648,327,458,'R3 = Grafikaufnahme',False,'capture')
    txt(178,405,'Berichte enthalten das Protokoll dieses und des vorigen Laufs.',15,MUTED)
    button(174,423,458,'Bericht senden …',action='report-send')
    button(648,423,458,'Bericht speichern …',action='report-save')
    small(178,510,'QUELLE')
    button(174,529,932,'GitHub · Andiweli / CubeEvil4-Android  ↗',action='source')
    return finish('05-management')

SCREENS = dict(zip(['home','display','controls','performance','management'],[home(),display(),controls(),performance(),management()]))
html = '''<!doctype html><html lang="de"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>CubeEvil4 · GUI-Entwurf</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#0d110f;color:#f0eee5;font:15px system-ui,sans-serif}header{max-width:1280px;margin:auto;padding:20px 24px;display:flex;justify-content:space-between;gap:20px;align-items:center}header p{margin:6px 0;color:#a6afa3;font-size:13px}nav{display:flex;gap:6px;flex-wrap:wrap}button,a{font:inherit}nav button,.dialog button{background:#222a25;color:#f0eee5;border:1px solid #414b41;border-radius:6px;padding:10px 16px;cursor:pointer}nav button[aria-current=true]{color:#d7bc7d;border-color:#d7bc7d}main{position:relative;max-width:1280px;margin:auto}svg{display:block;width:100%;height:auto}.button{cursor:pointer}.button:focus{outline:none}.button:focus>rect{stroke:#f0eee5;stroke-width:3}.button:hover>rect{stroke:#d7bc7d}.button.selected>rect{fill:#45432e;stroke:#d7bc7d}.button.selected text{fill:#d7bc7d}.button.toggle text:not(.state){fill:#f0eee5}.button[aria-disabled=true]{cursor:default}footer{padding:15px 24px;color:#a6afa3;max-width:1280px;margin:auto;font-size:13px}footer button{color:#d7bc7d;background:transparent;border:0;cursor:pointer;text-decoration:underline;margin-left:15px}.overlay{position:absolute;inset:6.67% 10.94% 6.39%;background:#111813f5;border:1px solid #d7bc7d;border-radius:10px;display:flex;align-items:center;justify-content:center}.dialog{max-width:640px;padding:24px}.dialog h2{font-size:28px}.dialog p{color:#a6afa3;line-height:1.6}.dialog button{margin-right:10px;min-height:48px}.dialog button.primary{border-color:#d7bc7d;color:#d7bc7d}#toast{color:#d7bc7d;min-height:20px}a{color:#d7bc7d} @media(max-width:700px){header{display:block}nav{margin-top:15px}main{overflow:auto}main svg{min-width:850px}.overlay{position:fixed;inset:15% 4%}.dialog h2{font-size:22px}}
</style><header><div><strong>CubeEvil4 · Ingame-Menü</strong><p>Klickbarer Designentwurf · keine Verbindung zum Spiel · deutsche Beispieltexte</p></div><nav aria-label="Entwurfsseiten"></nav></header><main id="screen"></main><footer><span>Ein Menüfenster · Auswahlbuttons · 6 dp Ecken · Touch & Controller</span><button id="empty">Leeren Speicherplatz zeigen</button><button id="busy">Speichersperre zeigen</button><p id="toast" role="status" aria-live="polite"></p></footer>
<script>
const screens=SCREENS_JSON;
const names={home:'Hauptseite',display:'Anzeige',controls:'Steuerung',performance:'Performance',management:'Management'};
let page='home',hasSave=true,busy=false,lastAction='',values={};
const screen=document.getElementById('screen'),toast=document.getElementById('toast');
const nav=document.querySelector('nav');
Object.entries(names).forEach(([id,label])=>{let b=document.createElement('button');b.textContent=label;b.onclick=()=>show(id);b.dataset.page=id;nav.append(b)});
function show(id,focus=false){page=id;screen.innerHTML=screens[id];toast.textContent='';document.querySelectorAll('nav button').forEach(b=>b.setAttribute('aria-current',String(b.dataset.page===id)));Object.entries(values).forEach(([group,value])=>apply(group,value));if(id==='home'){if(!hasSave)document.getElementById('save-meta').textContent='Noch kein Instant-Spielstand vorhanden.';if(busy)document.getElementById('save-meta').textContent='Gerade nicht verfügbar: Raumwechsel / Spielkern noch nicht pausiert.';screen.querySelectorAll('[data-action="load"],[data-action="save"]').forEach(b=>{let disabled=busy||(!hasSave&&b.dataset.action==='load');b.setAttribute('aria-disabled',disabled);b.style.opacity=disabled?.4:1;b.setAttribute('tabindex',disabled?-1:0)})}if(focus)(screen.querySelector('[data-action="home"]')||screen.querySelector('[data-action="save"]')).focus()}
function apply(group,value){screen.querySelectorAll('[data-group="'+group+'"]').forEach(b=>{const on=b.dataset.toggle?!!value:b.dataset.value===value;b.classList.toggle('selected',on);b.setAttribute('aria-pressed',String(on));if(b.querySelector('.state'))b.querySelector('.state').textContent=on?'AN':'AUS'});if(group==='gyro'){let slider=screen.querySelector('[data-slider]');if(slider){slider.setAttribute('aria-disabled',String(!value));slider.style.opacity=value?1:.35;slider.setAttribute('tabindex',value?0:-1)}}}
function closeOverlay(){let d=screen.querySelector('.overlay');if(d)d.remove();let b=screen.querySelector('[data-action="'+lastAction+'"]');if(b)b.focus()}
function confirmAction(action){lastAction=action;const isSave=action==='save';if(isSave&&!hasSave){finishSave();return}const overlay=document.createElement('div');overlay.className='overlay';overlay.setAttribute('role','dialog');overlay.setAttribute('aria-modal','true');overlay.innerHTML='<div class="dialog"><h2 id="confirm-title">'+(isSave?'Instant-Spielstand überschreiben?':'Instant-Spielstand laden?')+'</h2><p>'+(isSave?'Der bisherige Instant-Spielstand wird durch Deinen aktuellen Fortschritt ersetzt. Die Spielstände der Schreibmaschine bleiben erhalten.':'Nicht gespeicherter Fortschritt geht verloren. Du kehrst zum Instant-Spielstand zurück.')+'</p><button id="cancel">Abbrechen</button><button id="confirm" class="primary">'+(isSave?'Überschreiben':'Laden')+'</button></div>';overlay.setAttribute('aria-labelledby','confirm-title');screen.append(overlay);overlay.querySelector('#cancel').onclick=closeOverlay;overlay.querySelector('#confirm').onclick=()=>{closeOverlay();if(isSave)finishSave();else toast.textContent='Vorschau: Instant-Spielstand geladen. In der App würde das Spiel fortgesetzt.'};overlay.querySelector('#cancel').focus()}
function finishSave(){hasSave=true;show('home');document.getElementById('save-meta').textContent='Vorschau: gerade gespeichert · Beispielwerte';toast.textContent='Vorschau: Instant-Spielstand gespeichert.';screen.querySelector('[data-action="save"]').focus()}
function activate(b){if(b.getAttribute('aria-disabled')==='true')return;const action=b.dataset.action,group=b.dataset.group;if(group){let value=b.dataset.toggle?b.getAttribute('aria-pressed')!=='true':b.dataset.value;values[group]=value;apply(group,value);return}if(screens[action])show(action,true);else if(action==='save'||action==='load')confirmAction(action);else if(action==='source')toast.textContent='Projekt: https://github.com/Andiweli/CubeEvil4-Android';else toast.textContent='Vorschau: '+(action==='resume'?'Zurück zum Spiel.':b.getAttribute('aria-label')+' öffnet den bestehenden Ablauf.');}
screen.addEventListener('click',e=>{const b=e.target.closest('.button');if(b)activate(b)});
screen.addEventListener('pointerdown',e=>{const slider=e.target.closest('[data-slider]');if(!slider||slider.getAttribute('aria-disabled')==='true')return;const svg=slider.ownerSVGElement;const point=svg.createSVGPoint();point.x=e.clientX;point.y=e.clientY;const local=point.matrixTransform(svg.getScreenCTM().inverse());const value=Math.round((.5+Math.max(0,Math.min(1,(local.x-666)/334))*2.5)*4)/4;slider.setAttribute('aria-valuenow',value);slider.querySelector('.thumb').setAttribute('x',660+(value-.5)/2.5*334);document.getElementById('sensitivity-value').textContent=value+'×';slider.focus()});
document.addEventListener('keydown',e=>{const overlay=screen.querySelector('.overlay');if(overlay){if(e.key==='Escape'){e.preventDefault();closeOverlay()}if(e.key==='Tab'){const buttons=[...overlay.querySelectorAll('button')];const i=buttons.indexOf(document.activeElement);e.preventDefault();buttons[(i+(e.shiftKey?-1:1)+buttons.length)%buttons.length].focus()}return}if(e.key==='Escape'||e.key==='Backspace'){e.preventDefault();if(page!=='home')show('home',true);else toast.textContent='Vorschau: Zurück zum Spiel.';return}const b=document.activeElement;if(e.key==='Enter'||e.key===' '){if(b.matches('g.button')){e.preventDefault();activate(b)}}if(b.matches('[data-slider]')&&b.getAttribute('aria-disabled')!=='true'&&['ArrowLeft','ArrowRight'].includes(e.key)){e.preventDefault();const val=Math.min(3,Math.max(.5,+(+b.getAttribute('aria-valuenow')+(e.key==='ArrowRight'?.25:-.25)).toFixed(2)));b.setAttribute('aria-valuenow',val);b.querySelector('.thumb').setAttribute('x',660+(val-.5)/2.5*334);document.getElementById('sensitivity-value').textContent=val+'×'}else if(['ArrowUp','ArrowDown','ArrowLeft','ArrowRight'].includes(e.key)&&screen.contains(b)){const all=[...screen.querySelectorAll('g[tabindex="0"]')];const index=all.indexOf(b),next=index+(['ArrowDown','ArrowRight'].includes(e.key)?1:-1);if(next>=0&&next<all.length){e.preventDefault();all[next].focus()}}});
document.getElementById('empty').onclick=()=>{hasSave=!hasSave;busy=false;show('home')};document.getElementById('busy').onclick=()=>{busy=!busy;show('home')};show('home');
</script></html>'''
(ROOT/'index.html').write_text(html.replace('SCREENS_JSON',json.dumps(SCREENS,ensure_ascii=False)),encoding='utf-8')
print('Generated five SVG screens and index.html')
