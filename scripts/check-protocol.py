"""프로토콜 상수 대조 — 펌웨어 · SDK · 문서.

세 곳이 같은 명령 번호·상태 코드·UUID 를 쓰는지 본다. 값이 어긋나거나, 한쪽에만
있거나, 문서에 없는 것이 있으면 0 이 아닌 값으로 끝난다.

    python3 scripts/check-protocol.py

동작까지 보는 것은 sdk/test/conformance.test.js 다. 이건 이름과 숫자만 본다 —
그래서 빠르고, 새 명령을 한쪽에만 추가했을 때 바로 걸린다.

저장소 루트에서 실행해야 한다.
"""
import re, sys

FW  = open('firmware/zephyr/src/app/protocol.h', encoding='utf-8').read()
SDK = open('sdk/src/protocol.ts', encoding='utf-8').read()
DOC = open('docs/protocol.md', encoding='utf-8').read()

def fw_defines(prefix):
    out = {}
    for m in re.finditer(r'#define\s+%s(\w+)\s+(0x[0-9a-fA-F]+|\d+)' % prefix, FW):
        out[m.group(1)] = int(m.group(2), 0)
    return out

def sdk_block(name):
    m = re.search(r'export const %s = \{(.*?)\}' % name, SDK, re.S)
    if not m: return {}
    return {k: int(v, 0) for k, v in re.findall(r'(\w+)\s*:\s*(0x[0-9a-fA-F]+|\d+)', m.group(1))}

def doc_codes(pattern):
    return {int(m, 16) for m in re.findall(pattern, DOC)}

rows = []
def cmp(label, fw, sdk, docset=None):
    keys = sorted(set(fw) | set(sdk))
    for k in keys:
        f, s = fw.get(k), sdk.get(k)
        if f is None:   verdict, note = 'SDK 전용', '펌웨어에 없음 → UNKNOWN_CMD'
        elif s is None: verdict, note = '펌웨어 전용', 'SDK 가 안 씀'
        elif f != s:    verdict, note = '값 불일치', f'fw=0x{f:02x} sdk=0x{s:02x}'
        else:
            v = f
            if docset is not None and v not in docset:
                verdict, note = '문서 누락', f'0x{v:02x} 이 docs/protocol.md 에 없음'
            else:
                verdict, note = 'OK', ''
        rows.append((label, k, f'0x{f:02x}' if f is not None else '—',
                     f'0x{s:02x}' if s is not None else '—', verdict, note))

cmd_doc = doc_codes(r'### `0x([0-9A-Fa-f]{2}) [A-Z_]+`')
evt_doc = doc_codes(r'### `0x([Aa][0-9A-Fa-f]) [A-Z_]+`')
sw_doc  = doc_codes(r'\| `0x([0-9A-Fa-f]{4})` \|')

cmp('CMD', fw_defines('NU_CMD_'), sdk_block('CMD'), cmd_doc)
cmp('EVT', fw_defines('NU_EVT_'), sdk_block('EVT'), evt_doc)
cmp('SW',  fw_defines('NU_SW_'),  sdk_block('SW'),  sw_doc)
cmp('FLAG', fw_defines('NU_FLAG_'), sdk_block('FLAG'))

w = [max(len(str(r[i])) for r in rows + [('구분','이름','펌웨어','SDK','판정','비고')]) for i in range(6)]
hdr = ('구분','이름','펌웨어','SDK','판정','비고')
print('  '.join(h.ljust(w[i]) for i, h in enumerate(hdr)))
print('  '.join('-' * w[i] for i in range(6)))
bad = 0
for r in rows:
    if r[4] != 'OK': bad += 1
    print('  '.join(str(c).ljust(w[i]) for i, c in enumerate(r)))
print()

# UUID
fw_uuid = re.findall(r'#define NU_UUID128_(\w+)\s*\\\n\s*([0-9a-fx,]+)', FW)
sdk_uuid = dict(re.findall(r"export const (\w+_UUID)\s*=\s*'([0-9a-f-]+)'", SDK))
print('UUID')
for name, bytes_ in fw_uuid:
    b = [int(x, 16) for x in bytes_.split(',')]
    canon = bytes(reversed(b)).hex()
    canon = f'{canon[0:8]}-{canon[8:12]}-{canon[12:16]}-{canon[16:20]}-{canon[20:32]}'
    key = {'SERVICE': 'SERVICE_UUID', 'RX': 'RX_UUID', 'TX': 'TX_UUID'}[name]
    s = sdk_uuid.get(key, '—')
    ok = 'OK' if s == canon else '불일치'
    if ok != 'OK': bad += 1
    print(f'  {name:8} fw={canon}  sdk={s}  {ok}')
    if canon not in DOC:
        bad += 1
        print(f'           → 문서에 없음')

print(f'\n불일치 {bad}건')
sys.exit(1 if bad else 0)
