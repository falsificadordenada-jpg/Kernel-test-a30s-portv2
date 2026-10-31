stage 08-libplatform    do_libplatform
stage 09-firehose       do_firehose
stage 10-kernel         do_kernel
stage 11-coletar        collect || true
python3 - "$OUT" "$REPORT" <<'PYREP' >> "$REPORT" 2>&1
import sys, os, re, glob, collections
out, rep = sys.argv[1], sys.argv[2]
RUN = re.compile(r'/Users/runner/work/[^ ]*?/work/')
def clean(s): return RUN.sub('', s)
logs = sorted(glob.glob(os.path.join(out, '*.log')))
def lines(p): return open(p, errors='replace').read().splitlines()
print(); print('=== ERROS MAIS FREQUENTES (todas as etapas; sem ruido de xcrun) ===')
c = collections.Counter()
for p in logs:
    for l in lines(p):
        if 'error:' in l and 'xcrun: error' not in l:
            l = clean(l); l = re.sub(r'^\S+ ', '', l); l = re.sub(r':\d+:\d+:', ':', l); c[l[:200]] += 1
for l, n in c.most_common(50): print('%4d %s' % (n, l))
print(); print('=== ARQUIVOS COM ERRO (kernel) ===')
k = os.path.join(out, '10-kernel.log'); c = collections.Counter()
if os.path.exists(k):
    for l in lines(k):
        if 'error:' in l and 'xcrun: error' not in l:
            c[re.sub(r':\d+:\d+:.*', '', clean(l))[:160]] += 1
for l, n in c.most_common(40): print('%4d %s' % (n, l))
print(); print('=== TODOS OS ERROS POR ETAPA (unicos, sem caminho do runner) ===')
pat = re.compile(r'error:|: \*\*\*|^ld: |fatal:|Undefined symbols|No such file|not found|SyntaxError')
for p in logs:
    ls = [l for l in lines(p) if pat.search(l) and 'xcrun: error' not in l]
    if not ls: continue
    print('--- %s: %d linhas de erro' % (os.path.basename(p), len(ls)))
    seen = set()
    for l in ls:
        l = clean(l)[:260]
        if l in seen: continue
        seen.add(l); print(l)
        if len(seen) >= 40: break
print(); print('=== SIMBOLOS INDEFINIDOS NO LINK (se houver) ===')
if os.path.exists(k):
    L = lines(k)
    for i, l in enumerate(L):
        if 'Undefined symbols' in l:
            print('\n'.join(x[:200] for x in L[i:i+40])); break
print(); print('=== FIREHOSE: avisos suspeitos ===')
f = os.path.join(out, '09-firehose.log')
if os.path.exists(f):
    seen = set()
    for l in lines(f):
        if re.search(r'implicit declaration|unknown type name|undeclared identifier', l):
            l = clean(l)[:240]
            if l not in seen: seen.add(l); print(l)
print(); print('=== ETAPAS ===')
seen = set()
for l in lines(rep):
    if re.match(r'(PASS|FAIL)', l) and l not in seen: seen.add(l); print(l)
PYREP
echo "---------- RELATORIO ----------"; cat "$REPORT"
[ -n "$(ls "$OUT"/kernel 2>/dev/null)" ] || { echo "SEM KERNEL"; exit 1; }
exit $FAILED
