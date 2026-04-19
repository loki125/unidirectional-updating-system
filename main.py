#!/usr/bin/env python3

import tkinter as tk
from tkinter import scrolledtext
import subprocess
import threading

# Global variables for processes
network_proc = None
update_proc = None
store_proc = None

# --- Custom Collapsible Log UI Component ---
class CollapsibleLog(tk.Frame):
    def __init__(self, parent, title):
        super().__init__(parent)
        self.title = title
        self.show = True
        
        # The Toggle Button
        self.toggle_btn = tk.Button(self, text=f"▼ {self.title}", anchor="w", font=("sans-serif", 9, "bold"), command=self.toggle)
        self.toggle_btn.pack(fill="x")
        
        # The Text Console
        self.console = scrolledtext.ScrolledText(self, bg="#1e1e1e", fg="#d4d4d4", font=("Consolas", 9), state="disabled", height=5)
        self.console.pack(fill="both", expand=True, pady=(0, 5))
        
    def toggle(self):
        self.show = not self.show
        if self.show:
            self.toggle_btn.config(text=f"▼ {self.title}")
            self.console.pack(fill="both", expand=True, pady=(0, 5))
            # Tell Tkinter THIS frame should expand again to fill empty window space
            self.pack(fill="both", expand=True) 
        else:
            self.toggle_btn.config(text=f"▶ {self.title}")
            self.console.pack_forget()
            # Tell Tkinter to stop stretching this frame so they stack compactly
            self.pack(fill="x", expand=False)
            
    def log(self, message):
        """Thread-safe logging to this specific text box"""
        def append():
            self.console.config(state="normal")
            self.console.insert(tk.END, message + "\n")
            self.console.see(tk.END)
            self.console.config(state="disabled")
        self.after(0, append)

# --- Helper Function for Streams ---
def stream_logs(stream, logger):
    for line in stream:
        logger.log(line.strip())

# --- Process Functions ---
def run_network():
    global network_proc
    cmd = ["bash", "network/run.sh"]
    if net_type_var.get() == "static":
        cmd.append("--static")
    else:
        cmd.append("--dynamic")
    if net_build_var.get():
        cmd.append("--build")
        
    net_logs.log(f"[RUNNING] {' '.join(cmd)}")
    network_proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    threading.Thread(target=stream_logs, args=(network_proc.stdout, net_logs), daemon=True).start()

def stop_network():
    global network_proc
    if network_proc:
        net_logs.log("[STOPPING] Sending Ctrl+C to Network script...")
        network_proc.terminate()
        network_proc = None

def run_docker(folder, build_var, logger):
    cmd = ["docker", "compose", "up"]
    if build_var.get():
        cmd.append("--build")
        
    logger.log(f"[RUNNING] {' '.join(cmd)} in {folder}")
    proc = subprocess.Popen(cmd, cwd=folder, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    threading.Thread(target=stream_logs, args=(proc.stdout, logger), daemon=True).start()
    return proc

def stop_docker(folder, logger):
    logger.log(f"[STOPPING] docker compose down in {folder}...")
    res = subprocess.run(["docker", "compose", "down"], cwd=folder, capture_output=True, text=True)
    if res.stdout: logger.log(res.stdout.strip())
    if res.stderr: logger.log(res.stderr.strip())

def stop_all_containers():
    sys_logs.log("[SYSTEM] Fetching all Docker containers...")
    res = subprocess.run(["docker", "ps", "-aq"], capture_output=True, text=True)
    container_ids = res.stdout.strip().split()
    
    if not container_ids:
        sys_logs.log("[SYSTEM] No Docker containers found to stop.")
        return
        
    sys_logs.log(f"[SYSTEM] Stopping {len(container_ids)} container(s)...")
    stop_cmd = ["docker", "stop"] + container_ids
    proc = subprocess.Popen(stop_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    threading.Thread(target=stream_logs, args=(proc.stdout, sys_logs), daemon=True).start()

# --- Auto-Cleanup on Close ---
def on_closing():
    sys_logs.log("\n[SYSTEM] Closing Panel. Initiating automatic cleanup...")
    stop_network()
    
    def cleanup_thread():
        res = subprocess.run(["docker", "ps", "-aq"], capture_output=True, text=True)
        container_ids = res.stdout.strip().split()
        if container_ids:
            sys_logs.log(f"[SYSTEM] Auto-stopping {len(container_ids)} container(s)... (Please wait)")
            subprocess.run(["docker", "stop"] + container_ids, capture_output=True)
            sys_logs.log("[SYSTEM] All containers stopped.")
        else:
            sys_logs.log("[SYSTEM] No containers to stop.")
            
        sys_logs.log("[SYSTEM] Exiting gracefully...")
        root.after(1000, root.destroy)

    threading.Thread(target=cleanup_thread, daemon=True).start()

# ===========================
# UI SETUP
# ===========================
root = tk.Tk()
root.title("System Control Panel")
root.geometry("900x850")
root.padx = 10
root.pady = 10
root.protocol("WM_DELETE_WINDOW", on_closing)

# --- Top Frame: Controls ---
controls_frame = tk.Frame(root)
controls_frame.pack(fill="x", padx=10, pady=5)

# 1. Network
frame_net = tk.LabelFrame(controls_frame, text="1. Network (run.sh)", padx=10, pady=10)
frame_net.pack(side="left", fill="both", expand=True, padx=5)

net_type_var = tk.StringVar(value="static")
tk.Radiobutton(frame_net, text="--static", variable=net_type_var, value="static").pack(anchor="w")
tk.Radiobutton(frame_net, text="--dynamic", variable=net_type_var, value="dynamic").pack(anchor="w")
net_build_var = tk.BooleanVar()
tk.Checkbutton(frame_net, text="--build", variable=net_build_var).pack(anchor="w")

tk.Button(frame_net, text="Start Network", command=run_network, bg="lightgreen").pack(side="left", pady=5)
tk.Button(frame_net, text="Stop (Ctrl+C)", command=stop_network, bg="salmon").pack(side="right", pady=5)

# 2. Update Manager
frame_upd = tk.LabelFrame(controls_frame, text="2. Update Manager", padx=10, pady=10)
frame_upd.pack(side="left", fill="both", expand=True, padx=5)

upd_build_var = tk.BooleanVar()
tk.Checkbutton(frame_upd, text="--build", variable=upd_build_var).pack(anchor="w")

tk.Button(frame_upd, text="Compose Up", command=lambda: globals().update(update_proc=run_docker("update-manager", upd_build_var, upd_logs)), bg="lightgreen").pack(side="left", pady=5)
tk.Button(frame_upd, text="Compose Down", command=lambda: stop_docker("update-manager", upd_logs), bg="salmon").pack(side="right", pady=5)

# 3. Store Node
frame_store = tk.LabelFrame(controls_frame, text="3. Store Node", padx=10, pady=10)
frame_store.pack(side="left", fill="both", expand=True, padx=5)

store_build_var = tk.BooleanVar()
tk.Checkbutton(frame_store, text="--build", variable=store_build_var).pack(anchor="w")

tk.Button(frame_store, text="Compose Up", command=lambda: globals().update(store_proc=run_docker("store-node", store_build_var, store_logs)), bg="lightgreen").pack(side="left", pady=5)
tk.Button(frame_store, text="Compose Down", command=lambda: stop_docker("store-node", store_logs), bg="salmon").pack(side="right", pady=5)

# --- Global Actions ---
global_frame = tk.Frame(root)
global_frame.pack(fill="x", padx=15, pady=5)
tk.Button(global_frame, text="Stop ALL Docker Containers", command=stop_all_containers, bg="red", fg="white", font=("sans-serif", 10, "bold")).pack(side="right")

# --- Log Panels Section ---
logs_container = tk.Frame(root)
logs_container.pack(fill="both", expand=True, padx=10, pady=5)

# Initialize the 4 individual log boxes
sys_logs = CollapsibleLog(logs_container, "System Activity")
sys_logs.pack(fill="both", expand=True)

net_logs = CollapsibleLog(logs_container, "Network Logs (run.sh)")
net_logs.pack(fill="both", expand=True)

upd_logs = CollapsibleLog(logs_container, "Update Manager Logs")
upd_logs.pack(fill="both", expand=True)

store_logs = CollapsibleLog(logs_container, "Store Node Logs")
store_logs.pack(fill="both", expand=True)

# Greet the user in the system logs
sys_logs.log("[SYSTEM] Control Panel Initialized. Ready to run commands.")

# Run the UI
root.mainloop()