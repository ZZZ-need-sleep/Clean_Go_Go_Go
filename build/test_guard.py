import importlib.util
import subprocess
from ctypes import wintypes

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

p = subprocess.Popen(
    [r"build\badname.exe"],
    stdin=subprocess.DEVNULL,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)
print("pid", p.pid, flush=True)
dlg = uip.find_dialog(None, 15)
print("dialog", hex(dlg) if dlg else None, flush=True)
if dlg:
    texts = []
    for child in uip.enum_windows(dlg):
        texts.append(uip.window_text(child))
    with open(r"build\guard_text.txt", "w", encoding="utf-8") as f:
        f.write("TITLE: " + uip.window_text(dlg) + "\n")
        for t in texts:
            f.write(repr(t) + "\n")
    print("TITLE", repr(uip.window_text(dlg)), flush=True)
    print("TEXTS", [repr(t) for t in texts], flush=True)
    btn = uip.user32.GetDlgItem(dlg, 1)
    print("button", hex(btn) if btn else None, flush=True)
    if btn:
        uip.user32.SendMessageW(btn, 0x00F5, 0, 0)  # BM_CLICK
    else:
        uip.click_button(dlg, 1)
try:
    print("exit", p.wait(timeout=10), flush=True)
except subprocess.TimeoutExpired:
    print("timeout; terminate", flush=True)
    p.terminate()
    print("exit", p.wait(timeout=5), flush=True)
