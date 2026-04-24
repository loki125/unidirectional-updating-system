#!/usr/bin/env python3

import tkinter as tk
from tkinter import scrolledtext, ttk
import subprocess
import threading
import webbrowser
import os
import sys
import signal

# --- 1. SUDO ENFORCEMENT ---
if os.geteuid() != 0:
    print("\033[91m[ERROR] This script must be run as root!\033[0m")
    print("Please use: sudo ./rk.py")
    sys.exit(1)

# --- Global Process Trackers ---
net_process = None

# --- Visual Constants ---
BG_MAIN = "#1e1e1e"      
BG_PANEL = "#2d2d2d"     
FG_TEXT = "#eeeeee"      
ACCENT_GREEN = "#28a745" 
ACCENT_RED = "#dc3545"   
ACCENT_GRAY = "#4a4a4a"  
FONT_BOLD = ("Segoe UI", 9, "bold")
FONT_NORM = ("Segoe UI", 9)

# --- Visual Feedback Helpers ---
def shake_widget(widget, original_color, count=0):
    if count == 0:
        widget.config(bg="#721c24") 
    offsets = [5, -10, 10, -10, 10, -5, 0]
    if count < len(offsets):
        widget.place(x=offsets[count], y=0, relwidth=1, relheight=1)
        root.after(50, lambda: shake_widget(widget, original_color, count + 1))
    else:
        widget.place(x=0, y=0, relwidth=1, relheight=1)
        root.after(1000, lambda: widget.config(bg=original_color))

# --- Custom Collapsible Log UI Component ---
class CollapsibleLog(tk.Frame):
    def __init__(self, parent, title):
        super().__init__(parent, bg=BG_MAIN)
        self.title = title
        self.show = True
        self.toggle_btn = tk.Button(self, text=f"▼ {self.title}", anchor="w", 
                                   bg=BG_PANEL, fg=FG_TEXT, relief="flat",
                                   font=FONT_BOLD, command=self.toggle,
                                   activebackground="#3e3e3e", activeforeground="white")
        self.toggle_btn.pack(fill="x")
        self.console = scrolledtext.ScrolledText(self, bg="#000000", fg="#d4d4d4", 
                                                font=("Consolas", 9), state="disabled", 
                                                height=8, borderwidth=0)
        self.console.pack(fill="both", expand=True, pady=(0, 5))
        
    def toggle(self):
        self.show = not self.show
        if self.show:
            self.toggle_btn.config(text=f"▼ {self.title}")
            self.console.pack(fill="both", expand=True, pady=(0, 5))
            self.pack(fill="both", expand=True, pady=2)
        else:
            self.toggle_btn.config(text=f"▶ {self.title}")
            self.console.pack_forget()
            self.pack(fill="x", expand=False, pady=2)
            
    def log(self, message):
        def append():
            self.console.config(state="normal")
            self.console.insert(tk.END, message + "\n")
            self.console.see(tk.END)
            self.console.config(state="disabled")
        self.after(0, append)

# --- Process Functions ---
def stream_logs(stream, logger):
    for line in stream:
        logger.log(line.strip())

def run_network():
    global net_process
    cmd = ["bash", "network/run.sh"]
    cmd.append("--static" if net_type_var.get() == "static" else "--dynamic")
    if net_build_var.get(): cmd.append("--build")
    net_logs.log(f"[RUNNING] {' '.join(cmd)}")
    
    # start_new_session=True creates a process group so we can cleanly kill the bash script AND its children on exit
    net_process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1, start_new_session=True)
    threading.Thread(target=stream_logs, args=(net_process.stdout, net_logs), daemon=True).start()

def run_docker(folder, build_var, logger):
    do_build = build_var.get()
    
    def execute():
        cmd = ["docker", "compose", "up", "-d"]
        if do_build: cmd.append("--build")
        logger.log(f"[RUNNING] {' '.join(cmd)} in {folder}")
        
        result = subprocess.run(cmd, cwd=folder, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if result.stdout:
            for line in result.stdout.splitlines():
                logger.log(line)
        
        if result.returncode != 0:
            logger.log(f"[ERROR] Failed to start {folder} (Exit code: {result.returncode})")
            return
            
        logs_cmd = ["docker", "compose", "logs", "-f"]
        proc = subprocess.Popen(logs_cmd, cwd=folder, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        stream_logs(proc.stdout, logger)

    threading.Thread(target=execute, daemon=True).start()

def stop_docker(folder, logger):
    cmd = ["docker", "compose", "down"]
    logger.log(f"[STOPPING] {' '.join(cmd)} in {folder}")
    proc = subprocess.Popen(cmd, cwd=folder, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    threading.Thread(target=stream_logs, args=(proc.stdout, logger), daemon=True).start()

def open_web_browser():
    url = "http://localhost:8080"
    upd_logs.log(f"[SYSTEM] Opening {url} in browser...")
    sudo_user = os.environ.get("SUDO_USER")
    
    if sudo_user:
        display = os.environ.get("DISPLAY", ":0")
        # Passing DBUS and XDG variables explicitly so Firefox attaches to the existing user session
        cmd = [
            "sudo", "-u", sudo_user, "sh", "-c", 
            f"DISPLAY={display} DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u {sudo_user})/bus XDG_RUNTIME_DIR=/run/user/$(id -u {sudo_user}) xdg-open {url}"
        ]
        subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        webbrowser.open(url)

def run_static_vm():
    path = qcow_path_entry.get().strip()
    if not path or not os.path.exists(path):
        store_logs.log(f"[ERROR] Path does not exist: {path}")
        shake_widget(qcow_path_entry, BG_MAIN)
        return
    def execute():
        cmd = ["bash", "./static-vm.sh", path]
        store_logs.log(f"[RUNNING] {' '.join(cmd)}")
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        threading.Thread(target=stream_logs, args=(proc.stdout, store_logs), daemon=True).start()
        exit_code = proc.wait()
        if exit_code != 0:
            root.after(0, lambda: shake_widget(qcow_path_entry, BG_MAIN))
    threading.Thread(target=execute, daemon=True).start()

# --- Cleanup Action upon Window Exit ---
def on_closing():
    # Ignore subsequent exit clicks
    root.protocol("WM_DELETE_WINDOW", lambda: None)
    
    if not sys_logs.show: sys_logs.toggle()
    sys_logs.log("[SYSTEM] Shutting down... Cleaning up services. Please wait...")
    
    def cleanup():
        global net_process
        
        # 1. Stop Network process if running
        if net_process and net_process.poll() is None:
            sys_logs.log("[SYSTEM] Stopping Network Script...")
            try:
                # Kills the bash script and all child processes it may have spawned
                os.killpg(os.getpgid(net_process.pid), signal.SIGTERM)
            except Exception as e:
                sys_logs.log(f"[WARNING] Issue stopping network: {e}")

        # 2. Stop Docker Compose containers
        for folder in ["update-manager", "store-node"]:
            if os.path.isdir(folder):
                sys_logs.log(f"[SYSTEM] Stopping {folder}...")
                subprocess.run(["docker", "compose", "down"], cwd=folder, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        
        sys_logs.log("[SYSTEM] Cleanup complete. Goodbye!")
        root.after(1000, root.destroy) # Close GUI automatically after 1s
        
    threading.Thread(target=cleanup, daemon=True).start()


# ===========================
# UI SETUP
# ===========================
root = tk.Tk()
root.title("System Control Panel (ROOT)")
root.geometry("1000x950")
root.configure(bg=BG_MAIN)
root.protocol("WM_DELETE_WINDOW", on_closing) # Bind cleanup function to window exit

controls_frame = tk.Frame(root, bg=BG_MAIN)
controls_frame.pack(fill="x", padx=15, pady=15)

def create_section(parent, title):
    frame = tk.LabelFrame(parent, text=f" {title} ", bg=BG_PANEL, fg=FG_TEXT, 
                          font=FONT_BOLD, padx=10, pady=10, relief="flat",
                          highlightbackground="#444", highlightthickness=1)
    return frame

# 1. Network Section
frame_net = create_section(controls_frame, "NETWORK")
frame_net.pack(side="left", fill="both", expand=True, padx=5)
net_type_var = tk.StringVar(value="static")
tk.Radiobutton(frame_net, text="--static", variable=net_type_var, value="static", bg=BG_PANEL, fg=FG_TEXT, selectcolor=BG_MAIN).pack(anchor="w")
tk.Radiobutton(frame_net, text="--dynamic", variable=net_type_var, value="dynamic", bg=BG_PANEL, fg=FG_TEXT, selectcolor=BG_MAIN).pack(anchor="w")
net_build_var = tk.BooleanVar(value=False)
tk.Checkbutton(frame_net, text="--build", variable=net_build_var, bg=BG_PANEL, fg=FG_TEXT, selectcolor=BG_MAIN).pack(anchor="w", pady=(5, 10))
tk.Button(frame_net, text="START NETWORK", command=run_network, bg=ACCENT_GREEN, fg="white", font=FONT_BOLD, relief="flat").pack(side="bottom", fill="x")

# 2. Update Manager Section
frame_upd = create_section(controls_frame, "UPDATE MANAGER")
frame_upd.pack(side="left", fill="both", expand=True, padx=5)
upd_build_var = tk.BooleanVar(value=False)
tk.Checkbutton(frame_upd, text="--build", variable=upd_build_var, bg=BG_PANEL, fg=FG_TEXT, selectcolor=BG_MAIN).pack(anchor="w", pady=(0, 5))

upd_ops_frame = tk.Frame(frame_upd, bg=BG_PANEL)
upd_ops_frame.pack(side="bottom", fill="x")
tk.Button(upd_ops_frame, text="UP", command=lambda: run_docker("update-manager", upd_build_var, upd_logs), bg=ACCENT_GREEN, fg="white", font=FONT_BOLD, relief="flat", width=6).pack(side="left", padx=2)
tk.Button(upd_ops_frame, text="DOWN", command=lambda: stop_docker("update-manager", upd_logs), bg=ACCENT_GRAY, fg="white", font=FONT_BOLD, relief="flat", width=6).pack(side="left", padx=2)
tk.Button(upd_ops_frame, text="WEB", command=open_web_browser, bg=ACCENT_RED, fg="white", font=FONT_BOLD, relief="flat", width=6).pack(side="right", padx=2)

# 3. Store Node Section
frame_store = create_section(controls_frame, "STORE NODE")
frame_store.pack(side="left", fill="both", expand=True, padx=5)
store_build_var = tk.BooleanVar(value=False)
tk.Checkbutton(frame_store, text="--build", variable=store_build_var, bg=BG_PANEL, fg=FG_TEXT, selectcolor=BG_MAIN).pack(anchor="w")

static_vm_container = tk.Frame(frame_store, bg=BG_PANEL)
tk.Label(static_vm_container, text="QCOW2 Path:", font=FONT_NORM, bg=BG_PANEL, fg="#aaa").pack(anchor="w")
entry_shake_holder = tk.Frame(static_vm_container, bg=BG_MAIN, height=25)
entry_shake_holder.pack(fill="x", pady=(2, 5))
entry_shake_holder.pack_propagate(False)
qcow_path_entry = tk.Entry(entry_shake_holder, bg=BG_MAIN, fg="white", insertbackground="white", borderwidth=0)
qcow_path_entry.place(x=0, y=0, relwidth=1, relheight=1)
tk.Button(static_vm_container, text="RUN STATIC VM", command=run_static_vm, bg=ACCENT_RED, fg="white", font=FONT_BOLD, relief="flat").pack(fill="x", pady=(0, 5))

def toggle_vm_ui(*args):
    if net_type_var.get() == "static": static_vm_container.pack(fill="x", pady=5)
    else: static_vm_container.pack_forget()
net_type_var.trace_add("write", toggle_vm_ui)
toggle_vm_ui()

store_ops_frame = tk.Frame(frame_store, bg=BG_PANEL)
store_ops_frame.pack(side="bottom", fill="x")
tk.Button(store_ops_frame, text="UP", command=lambda: run_docker("store-node", store_build_var, store_logs), bg=ACCENT_GREEN, fg="white", font=FONT_BOLD, relief="flat", width=10).pack(side="left", padx=2)
tk.Button(store_ops_frame, text="DOWN", command=lambda: stop_docker("store-node", store_logs), bg=ACCENT_GRAY, fg="white", font=FONT_BOLD, relief="flat", width=10).pack(side="right", padx=2)

# --- Logs Section ---
logs_container = tk.Frame(root, bg=BG_MAIN)
logs_container.pack(fill="both", expand=True, padx=15, pady=5)

sys_logs = CollapsibleLog(logs_container, "SYSTEM ACTIVITY")
sys_logs.pack(fill="x", expand=False, pady=2) 

net_logs = CollapsibleLog(logs_container, "NETWORK LOGS")
net_logs.pack(fill="x", expand=False, pady=2)

upd_logs = CollapsibleLog(logs_container, "UPDATE MANAGER")
upd_logs.pack(fill="x", expand=False, pady=2)

store_logs = CollapsibleLog(logs_container, "STORE NODE")
store_logs.pack(fill="x", expand=False, pady=2)

sys_logs.toggle()
sys_logs.log("[SYSTEM] Control Panel Initialized with Root Privileges.")

root.mainloop()