import importlib.util
import os
import shutil
import subprocess
import time

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

fixture = r"tests\fixture_extbatch"
data = r"build\extbatch_data"
for path in (fixture, data):
    if os.path.exists(path):
        shutil.rmtree(path)
shutil.copytree(r"tests\fixture", fixture)
os.makedirs(data)

p = subprocess.Popen(
    [r"dist\Genshin.exe", "--data-dir", data, "--test-scan", fixture],
    stdin=subprocess.DEVNULL,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)
print("testscan", p.wait(timeout=60), flush=True)

p = subprocess.Popen(
    [r"dist\Genshin.exe", "--data-dir", data],
    stdin=subprocess.DEVNULL,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)
dlg = uip.find_dialog(None, 15)
if dlg:
    uip.click_button(dlg, 6)
main = uip.find_main(15)
print("main", hex(main) if main else None, flush=True)
time.sleep(1)
uip.user32.PostMessageW(main, 0x0201, 1, (292 << 16) | 30)
time.sleep(0.5)
uip.user32.PostMessageW(main, 0x0201, 1, (660 << 16) | 110)
edlg = uip.find_dialog("后缀保护管理", 5)
print("ext dialog", hex(edlg) if edlg else None, flush=True)
if edlg:
    edit = uip.user32.GetDlgItem(edlg, 1203)
    uip.user32.SendMessageW(edit, 0x000C, 0, "docx")
    uip.user32.SendMessageW(edlg, 0x0111, 1204, 0)
    time.sleep(0.5)
    uip.click_button(edlg, 2)
    time.sleep(1)
uip.user32.PostMessageW(main, 0x0111, 2005, 0)
bdlg = uip.find_dialog("一键清理", 5)
print("batch dialog", hex(bdlg) if bdlg else None, flush=True)
if bdlg:
    uip.click_button(bdlg, 6)
    time.sleep(2)
    done = uip.find_dialog("清理完成", 3)
    print("done dialog", hex(done) if done else None, flush=True)
    if done:
        uip.click_button(done, 1)
    time.sleep(0.5)
uip.user32.PostMessageW(main, 0x0010, 0, 0)
time.sleep(1)
if p.poll() is None:
    p.terminate()
print("exit", p.wait(timeout=10), flush=True)

for rel in [
    r"办公\年度报告.docx",
    r"办公\预算表.xlsx",
    r"嵌套\深层\旧合同.pdf",
    r"保护区\机密.docx",
    r"办公\新文件.docx",
]:
    path = os.path.join(fixture, rel)
    print(rel, "exists" if os.path.exists(path) else "DELETED", flush=True)
