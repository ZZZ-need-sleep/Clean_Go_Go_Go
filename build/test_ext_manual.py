import importlib.util
import os
import shutil
import subprocess
import time

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

fixture = r"tests\fixture_extdel_manual"
data = r"build\extdel_manual"
for path in (fixture, data):
    if os.path.exists(path):
        shutil.rmtree(path)
shutil.copytree(r"tests\fixture", fixture)
os.makedirs(data)

p = subprocess.Popen([r"dist\Genshin.exe", "--data-dir", data, "--test-scan", fixture],
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print("testscan", p.wait(timeout=60), flush=True)

p = subprocess.Popen([r"dist\Genshin.exe", "--data-dir", data],
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
dlg = uip.find_dialog(None, 15)
if dlg:
    uip.click_button(dlg, 6)
main = uip.find_main(15)
time.sleep(1)
uip.user32.PostMessageW(main, 0x0201, 1, (347 << 16) | 60)
dlg = uip.find_dialog("按后缀删除", 5)
if dlg:
    edit = uip.user32.GetDlgItem(dlg, 1302)
    lst = uip.user32.GetDlgItem(dlg, 1305)
    uip.user32.SendMessageW(edit, 0x000C, 0, "docx")
    uip.user32.SendMessageW(dlg, 0x0111, 1303, 0)
    time.sleep(0.5)
    uip.user32.SendMessageW(dlg, 0x0111, 1307, 0)
    time.sleep(0.3)
    uip.user32.PostMessageW(dlg, 0x0111, 1308, 0)
    md = uip.find_dialog("手动确认删除", 5)
    print("manual", hex(md) if md else None, flush=True)
    if md:
        e1 = uip.user32.GetDlgItem(md, 1502)
        e2 = uip.user32.GetDlgItem(md, 1503)
        ok = uip.user32.GetDlgItem(md, 1)
        uip.user32.SendMessageW(e1, 0x000C, 0, "确认删除")
        uip.user32.SendMessageW(e2, 0x000C, 0, "确认删除")
        uip.user32.SendMessageW(md, 0x0111, (0x0300 << 16) | 1502, e1)
        uip.user32.SendMessageW(md, 0x0111, (0x0300 << 16) | 1503, e2)
        time.sleep(0.3)
        print("ok enabled", uip.user32.IsWindowEnabled(ok), flush=True)
        uip.user32.PostMessageW(md, 0x0111, 1, 0)
        time.sleep(4)
uip.user32.PostMessageW(main, 0x0010, 0, 0)
time.sleep(1)
if p.poll() is None:
    p.terminate()
print("exit", p.wait(timeout=10), flush=True)

for rel in [r"办公\年度报告.docx", r"办公\新文件.docx", r"保护区\机密.docx", r"办公\预算表.xlsx"]:
    path = os.path.join(fixture, rel)
    print(rel, "exists" if os.path.exists(path) else "DELETED", flush=True)
