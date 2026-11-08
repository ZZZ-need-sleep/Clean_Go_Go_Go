import importlib.util
import os
import shutil
import subprocess
import time

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

fixture = r"tests\fixture_extdel"
data = r"build\extdel_data"
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
print("main", hex(main) if main else None, flush=True)
time.sleep(1)
uip.user32.PostMessageW(main, 0x0201, 1, (347 << 16) | 110)
time.sleep(0.8)
dlg = uip.find_dialog("按后缀删除", 5)
print("extdel dialog", hex(dlg) if dlg else None, flush=True)
if dlg:
    edit = uip.user32.GetDlgItem(dlg, 1302)
    lst = uip.user32.GetDlgItem(dlg, 1305)
    uip.user32.SendMessageW(edit, 0x000C, 0, "docx;xlsx;pdf")
    uip.user32.SendMessageW(dlg, 0x0111, 1303, 0)  # search
    time.sleep(1)
    count = uip.user32.SendMessageW(lst, 0x018B, 0, 0)  # LB_GETCOUNT
    print("matches", count, flush=True)
    uip.user32.SendMessageW(dlg, 0x0111, 1307, 0)  # select all button
    time.sleep(0.5)
    sel = uip.user32.SendMessageW(lst, 0x0190, 0, 0)  # LB_GETSELCOUNT
    print("selected via button", sel, flush=True)
    if sel == 0:
        uip.user32.SendMessageW(lst, 0x0185, 1, -1)  # LB_SETSEL
        time.sleep(0.3)
        sel = uip.user32.SendMessageW(lst, 0x0190, 0, 0)
        print("selected direct", sel, flush=True)
    uip.user32.PostMessageW(dlg, 0x0111, 1308, 0)  # delete (async; confirmation is modal)
    time.sleep(0.8)
    # Any modal dialog now? print titles.
    for hwnd in uip.enum_windows():
        if uip.class_name(hwnd) == "#32770" and uip.user32.IsWindowVisible(hwnd):
            print("modal", hex(hwnd), repr(uip.window_text(hwnd)), flush=True)
    conf = uip.find_dialog("确认按后缀删除", 5)
    print("confirm", hex(conf) if conf else None, flush=True)
    if conf:
        uip.click_button(conf, 6)
        time.sleep(2)
    else:
        other = uip.find_dialog(None, 2)
        if other:
            print("closing other", repr(uip.window_text(other)), flush=True)
            uip.click_button(other, 1)
    time.sleep(1)
print("dialogs after", [(hex(h), uip.window_text(h)) for h in uip.enum_windows() if uip.class_name(h) == "#32770" and uip.user32.IsWindowVisible(h)], flush=True)
uip.user32.PostMessageW(main, 0x0010, 0, 0)
time.sleep(1)
if p.poll() is None:
    p.terminate()
print("exit", p.wait(timeout=10), flush=True)

for rel in [r"办公\年度报告.docx", r"办公\预算表.xlsx", r"嵌套\深层\旧合同.pdf",
            r"办公\新文件.docx", r"保护区\机密.docx", r"办公\config.ini"]:
    path = os.path.join(fixture, rel)
    print(rel, "exists" if os.path.exists(path) else "DELETED", flush=True)
