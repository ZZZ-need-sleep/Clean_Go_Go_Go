import importlib.util
import subprocess
import time

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

p = subprocess.Popen(
    [r"dist\Genshin.exe", "--data-dir", r"build\corrupt_data"],
    stdin=subprocess.DEVNULL,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)
print("pid", p.pid, flush=True)
dlg = uip.find_dialog(None, 15)
print("dialog", hex(dlg) if dlg else None, flush=True)
lines = []
if dlg:
    lines.append("TITLE: " + uip.window_text(dlg))
    for child in uip.enum_windows(dlg):
        lines.append(repr(uip.class_name(child)) + " " + repr(uip.window_text(child)))
with open(r"build\error_text.txt", "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
print("\n".join(lines), flush=True)
time.sleep(0.5)
p.terminate()
print("exit", p.wait(timeout=10), flush=True)
