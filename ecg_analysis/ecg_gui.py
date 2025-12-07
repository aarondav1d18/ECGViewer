#!/usr/bin/env python3
from __future__ import annotations

import tkinter as tk
from tkinter import filedialog, messagebox
from tkinter import ttk
from typing import List, Optional, Tuple, Callable


class ToolTip:
    def __init__(self, widget: tk.Widget, text: str, delay: int = 500) -> None:
        self.widget = widget
        self.text = text
        self.delay = delay
        self._after_id: str | None = None
        self._tip_window: tk.Toplevel | None = None

        widget.bind("<Enter>", self._on_enter)
        widget.bind("<Leave>", self._on_leave)
        widget.bind("<ButtonPress>", self._on_leave)

    def _on_enter(self, _event: tk.Event) -> None:
        self._schedule_show()

    def _on_leave(self, _event: tk.Event) -> None:
        self._cancel_show()
        self._hide_tip()

    def _schedule_show(self) -> None:
        self._cancel_show()
        self._after_id = self.widget.after(self.delay, self._show_tip)

    def _cancel_show(self) -> None:
        if self._after_id is not None:
            self.widget.after_cancel(self._after_id)
            self._after_id = None

    def _show_tip(self) -> None:
        if self._tip_window is not None:
            return

        # Default tooltip position (right + below widget)
        x = self.widget.winfo_rootx() + 20
        y = self.widget.winfo_rooty() + self.widget.winfo_height() + 4

        # Get tooltip size (estimate first)
        est_width = 260
        est_height = 60

        # Get root window boundaries
        root = self.widget.winfo_toplevel()
        root_x = root.winfo_rootx()
        root_y = root.winfo_rooty()
        root_w = root.winfo_width()
        root_h = root.winfo_height()

        # Clamp X so tooltip stays inside the window
        if x + est_width > root_x + root_w:
            x = (root_x + root_w) - est_width - 8
        if x < root_x:
            x = root_x + 8

        # Clamp Y so tooltip stays inside the window
        if y + est_height > root_y + root_h:
            y = self.widget.winfo_rooty() - est_height - 8
        if y < root_y:
            y = root_y + 8

        # Create tooltip window
        self._tip_window = tw = tk.Toplevel(self.widget)
        tw.wm_overrideredirect(True)
        tw.wm_geometry(f"+{int(x)}+{int(y)}")

        label = ttk.Label(
            tw,
            text=self.text,
            background="#ffffe0",
            relief="solid",
            borderwidth=1,
            padding=(6, 3, 6, 3),
            wraplength=280,
        )
        label.pack()

        # Now that it's created, get its *actual* size and clamp again
        tw.update_idletasks()
        w = tw.winfo_width()
        h = tw.winfo_height()

        # Re-adjust if needed
        adj_x = x
        adj_y = y

        if adj_x + w > root_x + root_w:
            adj_x = (root_x + root_w) - w - 8
        if adj_x < root_x:
            adj_x = root_x + 8

        if adj_y + h > root_y + root_h:
            adj_y = self.widget.winfo_rooty() - h - 8
        if adj_y < root_y:
            adj_y = root_y + 8

        tw.wm_geometry(f"+{int(adj_x)}+{int(adj_y)}")

    def _hide_tip(self) -> None:
        if self._tip_window is not None:
            self._tip_window.destroy()
            self._tip_window = None


class ECGGuiApp:
    def __init__(self, root: tk.Tk, run_callback: Callable) -> None:
        self.root = root
        self.run_callback = run_callback
        root.title("ECG Viewer Launcher")

        # Variables 
        self.file_var = tk.StringVar()
        self.window_var = tk.StringVar(value="0.4")
        self.ymin_var = tk.StringVar()
        self.ymax_var = tk.StringVar()
        self.show_artifacts_var = tk.BooleanVar(value=True)  # NEW: artefact toggle
        self.status_var = tk.StringVar(value="Select an ECG file and click Run.")

        # Root layout 
        root.minsize(580, 420)
        root.columnconfigure(0, weight=1)
        root.rowconfigure(0, weight=1)

        main_frame = ttk.Frame(root, padding=12)
        main_frame.grid(row=0, column=0, sticky="nsew")
        main_frame.columnconfigure(0, weight=1)
        main_frame.rowconfigure(4, weight=1)  # help box expands

        style = ttk.Style(root)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass

        self._build_menu()

        # Header 
        header = ttk.Label(
            main_frame,
            text="ECG Viewer",
            font=("TkDefaultFont", 14, "bold"),
        )
        header.grid(row=0, column=0, sticky="w")

        subheader = ttk.Label(
            main_frame,
            text="Choose an ECG file and configure the viewer settings below.",
            foreground="gray40",
        )
        subheader.grid(row=1, column=0, sticky="w", pady=(0, 8))

        # File selection 
        file_frame = ttk.LabelFrame(main_frame, text="Input ECG file")
        file_frame.grid(row=2, column=0, sticky="ew", pady=(0, 8))
        file_frame.columnconfigure(1, weight=1)

        ttk.Label(file_frame, text="File:").grid(row=0, column=0, sticky="w", padx=6, pady=6)
        self.file_entry = ttk.Entry(file_frame, textvariable=self.file_var)
        self.file_entry.grid(row=0, column=1, sticky="ew", padx=(0, 6), pady=6)
        browse_btn = ttk.Button(file_frame, text="Browse…", command=self.browse_file)
        browse_btn.grid(row=0, column=2, sticky="e", padx=6, pady=6)
        ToolTip(browse_btn, "Choose a .txt ECG file from disk.")

        # Viewer settings 
        basic_frame = ttk.LabelFrame(main_frame, text="Viewer settings")
        basic_frame.grid(row=3, column=0, sticky="ew", pady=(0, 8))
        basic_frame.columnconfigure(1, weight=1)

        ttk.Label(basic_frame, text="Window length (s):").grid(
            row=0, column=0, sticky="w", padx=6, pady=(6, 2)
        )
        win_entry = ttk.Entry(basic_frame, textvariable=self.window_var, width=10)
        win_entry.grid(row=0, column=1, sticky="w", padx=(0, 6), pady=(6, 2))
        ToolTip(win_entry, "How many seconds of ECG to display at once.")

        ttk.Label(basic_frame, text="Y-limits (optional):").grid(
            row=1, column=0, sticky="w", padx=6
        )

        ttk.Label(basic_frame, text="Min:").grid(row=1, column=1, sticky="w", padx=(0, 4))
        ymin_entry = ttk.Entry(basic_frame, textvariable=self.ymin_var, width=8)
        ymin_entry.grid(row=1, column=1, sticky="w", padx=(35, 2))
        ttk.Label(basic_frame, text="Max:").grid(row=1, column=2, sticky="w", padx=(0, 4))
        ymax_entry = ttk.Entry(basic_frame, textvariable=self.ymax_var, width=8)
        ymax_entry.grid(row=1, column=2, sticky="w", padx=(35, 2))

        show_art_chk = ttk.Checkbutton(
            basic_frame,
            text="Show original ECG with artefacts",
            variable=self.show_artifacts_var,
        )
        show_art_chk.grid(row=2, column=0, columnspan=4, sticky="w", padx=6, pady=(6, 4))
        ToolTip(
            show_art_chk,
            "If ticked, the viewer shows the original noisy ECG together with the cleaned signal.\n"
            "If unticked, only the visually corrected (clean) ECG is shown."
        )

        # Help box 
        help_frame = ttk.LabelFrame(main_frame, text="How to use the ECG viewer")
        help_frame.grid(row=4, column=0, sticky="nsew", pady=(0, 8))
        help_frame.columnconfigure(0, weight=1)
        help_frame.rowconfigure(0, weight=1)

        # inner card-like frame
        help_inner = ttk.Frame(help_frame)
        help_inner.grid(row=0, column=0, sticky="nsew", padx=4, pady=4)
        help_inner.columnconfigure(0, weight=1)
        help_inner.rowconfigure(0, weight=1)

        card_bg = "#f7f7f7"

        help_text = tk.Text(
            help_inner,
            wrap="word",
            borderwidth=1,
            relief="solid",
            highlightthickness=0,
            padx=10,
            pady=8,
            background=card_bg,
            font=("TkDefaultFont", 9),
        )
        help_text.grid(row=0, column=0, sticky="nsew")

        scroll = ttk.Scrollbar(help_inner, orient="vertical", command=help_text.yview)
        scroll.grid(row=0, column=1, sticky="ns")
        help_text.configure(yscrollcommand=scroll.set)

        help_text.tag_configure("heading", font=("TkDefaultFont", 9, "bold"))
        help_text.tag_configure("indent", lmargin1=16, lmargin2=24)

        help_text.insert("end", "Inputs\n", ("heading",))
        help_text.insert("end", "– In this version the ECG viewer only accepts .txt files.\n\n", ("indent",))

        help_text.insert("end", "Navigation\n", ("heading",))
        help_text.insert("end", "– Move through the ECG using the slider at the bottom.\n", ("indent",))
        help_text.insert("end", "– The Left and Right buttons also move through the recording.\n", ("indent",))
        help_text.insert("end", "– You can use the keyboard: Left/A = move left, Right/D = move right.\n", ("indent",))
        help_text.insert("end", "– You can click and drag the ECG left/right to traverse the recording.\n\n", ("indent",))

        help_text.insert("end", "Zooming\n", ("heading",))
        help_text.insert("end", "– Use the mouse wheel to zoom in and out on the time axis.\n", ("indent",))
        help_text.insert("end", "– The Zoom In and Zoom Out buttons change how much of the ECG you see at once.\n", ("indent",))
        help_text.insert("end", "– Rect Zoom lets you draw a box around an area to zoom into.\n\n", ("indent",))

        help_text.insert("end", "Viewing\n", ("heading",))
        help_text.insert("end", "– Reset View returns the time window and y-axis to a normal, clear layout.\n", ("indent",))
        help_text.insert("end", "– The viewer shows a cleaned version of the ECG signal by default.\n", ("indent",))
        help_text.insert("end", "– You can choose to show or hide the original ECG with artefacts.\n\n", ("indent",))

        help_text.insert("end", "Key Points (P, Q, R, S, T)\n", ("heading",))
        help_text.insert("end", "– Coloured markers show the P, Q, R, S and T points on the ECG trace.\n", ("indent",))
        help_text.insert("end", "– Each P/Q/R/S/T point is shown as a coloured dot on the line and a vertical line with a label (e.g. R @ 1.23456s).\n", ("indent",))
        help_text.insert("end", "– The QRS detections are displayed as vertical lines with labels at the time they occur.\n", ("indent",))
        help_text.insert("end", "– P and T wave detection is not perfect and may show false positives or miss some waves.\n", ("indent",))
        help_text.insert("end", "– When you hover over a key point, the cursor changes to a hand. You can click and drag to move the point left/right in time.\n", ("indent",))
        help_text.insert("end", "– While hovering a key point, you can press Backspace or Delete to remove it.\n", ("indent",))
        help_text.insert("end", "– At the bottom of the viewer there are tabs:\n", ("indent",))
        help_text.insert("end", "   • The “Traversal” tab contains the movement, zoom and Notes… controls.\n", ("indent",))
        help_text.insert("end", "   • The “Manual keypoints” tab lets you add new P, Q, R, S or T points.\n", ("indent",))
        help_text.insert("end", "– When you add a new key point from the Manual keypoints tab, it is placed in the middle of the current window and can then be dragged to the desired location.\n\n", ("indent",))

        help_text.insert("end", "Notes\n", ("heading",))
        help_text.insert("end", "– Click the “Notes…” button on the Traversal tab to open the Notes Manager.\n", ("indent",))
        help_text.insert("end", "– In the Notes Manager you can create, edit and delete notes linked to specific times in the ECG.\n", ("indent",))
        help_text.insert("end", "– New notes are created at the centre of the current ECG window and can be moved by dragging their marker on the plot.\n", ("indent",))
        help_text.insert("end", "– Notes appear on the ECG as a vertical marker with a text label. You can drag these in the same way as key points.\n", ("indent",))
        help_text.insert("end", "– Double-clicking a note in the Notes Manager (or on the plot) will jump the view to that time and open a dialog where you can edit the tag, time, voltage and detailed text.\n", ("indent",))
        help_text.insert("end", "– You can save notes to a JSON file and load them again later. Saved note files use the .json extension.\n\n", ("indent",))

        help_text.insert("end", "Exit\n", ("heading",))
        help_text.insert("end", "– Click Exit when you're finished.\n\n", ("indent",))


        help_text.insert(
            "end",
            "Tip: If the view becomes confusing, press Reset View.\n"
        )

        help_text.configure(state="disabled")

        # Bottom bar 
        bottom_frame = ttk.Frame(main_frame)
        bottom_frame.grid(row=5, column=0, sticky="ew")
        bottom_frame.columnconfigure(0, weight=1)

        status_label = ttk.Label(
            bottom_frame,
            textvariable=self.status_var,
            foreground="gray40",
        )
        status_label.grid(row=0, column=0, sticky="w")

        btn_frame = ttk.Frame(bottom_frame)
        btn_frame.grid(row=0, column=1, sticky="e")

        style.configure("Accent.TButton", padding=(8, 4))

        self.run_button = ttk.Button(
            btn_frame,
            text="Run viewer",
            style="Accent.TButton",
            command=self.run_viewer,
            state="disabled",
        )
        self.run_button.grid(row=0, column=0, padx=(0, 4))

        ttk.Button(btn_frame, text="Close", command=root.destroy).grid(row=0, column=1)

        # Bindings 
        self.file_entry.focus_set()
        self.file_var.trace_add("write", self._on_file_change)
        root.bind("<Return>", lambda e: self.run_viewer())
        root.bind("<Control-o>", lambda e: self.browse_file())

        root.update_idletasks()
        self._center_window()

    def _build_menu(self) -> None:
        menubar = tk.Menu(self.root)

        file_menu = tk.Menu(menubar, tearoff=False)
        file_menu.add_command(label="Open…", accelerator="Ctrl+O", command=self.browse_file)
        file_menu.add_separator()
        file_menu.add_command(label="Exit", command=self.root.destroy)
        menubar.add_cascade(label="File", menu=file_menu)

        help_menu = tk.Menu(menubar, tearoff=False)
        help_menu.add_command(label="About", command=self._show_about)
        menubar.add_cascade(label="Help", menu=help_menu)

        self.root.config(menu=menubar)

    def _show_about(self) -> None:
        messagebox.showinfo(
            "About ECG Viewer",
            "A simple launcher for the ECG Qt viewer."
        )

    def _center_window(self) -> None:
        self.root.update_idletasks()
        w = self.root.winfo_width()
        h = self.root.winfo_height()
        sw = self.root.winfo_screenwidth()
        sh = self.root.winfo_screenheight()
        x = (sw - w) // 2
        y = int((sh - h) * 0.4)
        self.root.geometry(f"{w}x{h}+{x}+{y}")

    def _on_file_change(self, *_args) -> None:
        if self.file_var.get().strip():
            self.run_button.config(state="normal")
            self.status_var.set("Ready to run.")
        else:
            self.run_button.config(state="disabled")
            self.status_var.set("Select an ECG file.")

    def browse_file(self) -> None:
        path = filedialog.askopenfilename(
            title="Select ECG text file",
            filetypes=[("Text files", "*.txt *.csv"), ("All files", "*.*")],
        )
        if path:
            self.file_var.set(path)

    def run_viewer(self) -> None:
        file_path = self.file_var.get().strip()

        if not file_path:
            messagebox.showerror("Missing file", "Please select an ECG file.")
            return

        if not file_path.lower().endswith((".txt", "csv")):
            messagebox.showerror("Invalid file", "Please select a .txt or .csv file.")
            return

        # Window length
        try:
            window_val = float(self.window_var.get().strip())
        except ValueError:
            messagebox.showerror("Invalid window", "Window length must be numeric.")
            return

        # Y limits
        ymin = self.ymin_var.get().strip()
        ymax = self.ymax_var.get().strip()

        ylim: Optional[Tuple[float, float]] = None
        if ymin or ymax:
            if not (ymin and ymax):
                messagebox.showerror("Invalid Y-limits", "Provide both Min and Max values.")
                return
            try:
                ymin_f = float(ymin)
                ymax_f = float(ymax)
            except ValueError:
                messagebox.showerror("Invalid Y-limits", "Y-limits must be numbers.")
                return
            ylim = (ymin_f, ymax_f)

        hide_artifacts = not self.show_artifacts_var.get()

        # Close the launcher and run the actual viewer
        self.root.destroy()

        # Call into main.run_ecg_viewer(...)
        self.run_callback(
            file_path,
            window=window_val,
            ylim=ylim,
            hide_artifacts=hide_artifacts,
            bandpass=False,  # or add a checkbox if you want GUI control
        )



def main() -> None:
    root = tk.Tk()
    ECGGuiApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
