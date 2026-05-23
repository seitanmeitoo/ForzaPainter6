"""Tkinter UI for vinyl-painter — étape 1 (image → JSON géométrique)."""
from __future__ import annotations

import queue
import threading
import time
from pathlib import Path

import numpy as np
import tkinter as tk
from tkinter import filedialog, messagebox, colorchooser, ttk
from PIL import Image, ImageTk

from legacy.image_utils import load_image, compose_on_background
from legacy.shapegen import (
    Engine, EngineConfig, Profile, PRESETS, DEFAULT_PRESET,
    SUPPORTED_SHAPE_TYPES, DEFAULT_SHAPE_TYPES, LOSSY_SHAPE_TYPES,
    FH6_MAX_SHAPES,
    HAS_CUPY, gpu_available, gpu_device_name,
)
from legacy.shapegen.render import render_shapes
from legacy.fh6_export import to_fh6_payload, save_fh6_json


PREVIEW_PANE_SIZE = 480
UI_REFRESH_MS = 100


# Display order in the UI and labels. Native FH6 types first, then converted ones.
SHAPE_TYPE_LABELS = {
    "rectangle": "Rectangles",
    "rotated_ellipse": "Ellipses tournées",
    "rotated_rectangle": "Carrés tournés",
    "circle": "Cercles",
    "ellipse": "Ellipses (non tournées)",
    "triangle": "Triangles",
}


class App:
    def __init__(self) -> None:
        self.root = tk.Tk()
        self.root.title("vinyl-painter — image vers vinyle FH6")
        self.root.geometry("1100x820")
        self.root.minsize(900, 700)

        # State
        self.image_path: Path | None = None
        self.source_rgb: np.ndarray | None = None
        self.source_alpha: np.ndarray | None = None
        self.engine: Engine | None = None
        self.worker_thread: threading.Thread | None = None
        self.event_queue: queue.Queue = queue.Queue()
        self.last_preview: np.ndarray | None = None
        self.shapes_snapshot: list = []
        self.gen_start_time: float = 0.0
        self.source_photo: ImageTk.PhotoImage | None = None
        self.preview_photo: ImageTk.PhotoImage | None = None

        # Tk variables
        self.transparency_mode = tk.StringVar(value="keep")  # "keep" | "fill"
        self.bg_color_var = tk.StringVar(value="#2a2a2a")
        self.type_vars: dict[str, tk.BooleanVar] = {
            t: tk.BooleanVar(value=(t in DEFAULT_SHAPE_TYPES))
            for t in SUPPORTED_SHAPE_TYPES
        }
        default_profile = PRESETS[DEFAULT_PRESET]
        self.preset_var = tk.StringVar(value=DEFAULT_PRESET)
        self.stop_at_var = tk.IntVar(value=default_profile.stop_at)
        self.random_samples_var = tk.IntVar(value=default_profile.random_samples)
        self.mutated_samples_var = tk.IntVar(value=default_profile.mutated_samples)
        self.gpu_available = gpu_available()
        self.use_gpu_var = tk.BooleanVar(value=False)
        self.progress_var = tk.DoubleVar(value=0.0)
        self.status_var = tk.StringVar(value="Choisissez une image pour commencer.")

        self._build_ui()
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    # ---- UI construction ----------------------------------------------------

    def _build_ui(self) -> None:
        pad = {"padx": 8, "pady": 4}

        # Top: image picker
        top = ttk.Frame(self.root)
        top.pack(fill="x", **pad)
        ttk.Button(top, text="📁 Choisir une image…", command=self._on_choose_image).pack(side="left")
        self.image_info_label = ttk.Label(top, text="Aucune image chargée")
        self.image_info_label.pack(side="left", padx=12)

        # Transparency section
        self.transparency_frame = ttk.LabelFrame(self.root, text="Transparence")
        self.transparency_frame.pack(fill="x", **pad)
        ttk.Radiobutton(
            self.transparency_frame,
            text="Garder la transparence (aucune forme sur les zones vides)",
            variable=self.transparency_mode, value="keep",
            command=self._on_transparency_mode_change,
        ).pack(anchor="w", padx=8, pady=2)
        fill_row = ttk.Frame(self.transparency_frame)
        fill_row.pack(anchor="w", padx=8, pady=2, fill="x")
        ttk.Radiobutton(
            fill_row, text="Remplacer la transparence par :",
            variable=self.transparency_mode, value="fill",
            command=self._on_transparency_mode_change,
        ).pack(side="left")
        self.bg_swatch = tk.Button(
            fill_row, text="    ", bg=self.bg_color_var.get(),
            relief="raised", borderwidth=2, command=self._on_pick_bg_color,
        )
        self.bg_swatch.pack(side="left", padx=8)
        self.bg_label = ttk.Label(fill_row, textvariable=self.bg_color_var)
        self.bg_label.pack(side="left")
        self.transparency_frame.pack_forget()  # caché jusqu'à détection alpha

        # Shape types — natifs FH6 (cochés par défaut) en ligne 1, autres (décochés) en ligne 2
        types_frame = ttk.LabelFrame(self.root, text="Types de formes autorisés (avant génération)")
        types_frame.pack(fill="x", **pad)
        native_row = ttk.Frame(types_frame)
        native_row.pack(fill="x", padx=8, pady=(4, 0))
        ttk.Label(native_row, text="Natifs FH6 (sans perte) :", foreground="#222").pack(side="left", padx=(0, 8))
        for t in ("rectangle", "rotated_ellipse"):
            ttk.Checkbutton(native_row, text=SHAPE_TYPE_LABELS[t], variable=self.type_vars[t]).pack(side="left", padx=8)
        other_row = ttk.Frame(types_frame)
        other_row.pack(fill="x", padx=8, pady=(2, 6))
        ttk.Label(other_row, text="Convertis à l'export :", foreground="#555").pack(side="left", padx=(0, 8))
        for t in ("circle", "ellipse", "rotated_rectangle", "triangle"):
            cb = ttk.Checkbutton(other_row, text=SHAPE_TYPE_LABELS[t], variable=self.type_vars[t])
            cb.pack(side="left", padx=8)
        note = ttk.Label(
            types_frame,
            text="Cercles et ellipses non tournées se convertissent sans perte. "
                 "Carrés tournés et triangles sont approximés par des ellipses tournées (perte visuelle).",
            foreground="#666", wraplength=900, justify="left",
        )
        note.pack(fill="x", padx=8, pady=(0, 6))

        # Generation settings
        gen_frame = ttk.LabelFrame(self.root, text="Génération")
        gen_frame.pack(fill="x", **pad)

        row1 = ttk.Frame(gen_frame)
        row1.pack(fill="x", padx=8, pady=4)
        ttk.Label(row1, text="Preset :").pack(side="left")
        preset_combo = ttk.Combobox(
            row1, textvariable=self.preset_var, values=list(PRESETS.keys()),
            state="readonly", width=14,
        )
        preset_combo.pack(side="left", padx=4)
        preset_combo.bind("<<ComboboxSelected>>", self._on_preset_change)

        ttk.Label(row1, text=f"Formes (max FH6 : {FH6_MAX_SHAPES}) :").pack(side="left", padx=(20, 0))
        ttk.Spinbox(row1, from_=50, to=FH6_MAX_SHAPES, increment=50, textvariable=self.stop_at_var, width=8).pack(side="left", padx=4)

        ttk.Label(row1, text="Random samples :").pack(side="left", padx=(20, 0))
        ttk.Spinbox(row1, from_=50, to=10000, increment=50, textvariable=self.random_samples_var, width=8).pack(side="left", padx=4)

        ttk.Label(row1, text="Mutations :").pack(side="left", padx=(20, 0))
        ttk.Spinbox(row1, from_=20, to=2000, increment=20, textvariable=self.mutated_samples_var, width=8).pack(side="left", padx=4)

        row_gpu = ttk.Frame(gen_frame)
        row_gpu.pack(fill="x", padx=8, pady=(2, 0))
        gpu_state = "normal" if self.gpu_available else "disabled"
        gpu_cb = ttk.Checkbutton(row_gpu, text="Accélération GPU (CUDA)", variable=self.use_gpu_var, state=gpu_state)
        gpu_cb.pack(side="left")
        if self.gpu_available:
            gpu_text = f"GPU : {gpu_device_name() or 'CUDA détecté'}"
            gpu_fg = "#1a6f1a"
        elif HAS_CUPY:
            gpu_text = "CuPy installé mais aucun GPU NVIDIA détecté."
            gpu_fg = "#7a6a1a"
        else:
            gpu_text = "Non disponible — installez CuPy (voir README)."
            gpu_fg = "#888"
        ttk.Label(row_gpu, text=gpu_text, foreground=gpu_fg).pack(side="left", padx=12)

        row2 = ttk.Frame(gen_frame)
        row2.pack(fill="x", padx=8, pady=6)
        self.generate_button = ttk.Button(row2, text="▶ Générer", command=self._on_generate)
        self.generate_button.pack(side="left")
        self.cancel_button = ttk.Button(row2, text="■ Annuler", command=self._on_cancel, state="disabled")
        self.cancel_button.pack(side="left", padx=8)
        self.save_button = ttk.Button(row2, text="💾 Enregistrer sous…", command=self._on_save, state="disabled")
        self.save_button.pack(side="right")

        # Preview panels
        panels = ttk.Frame(self.root)
        panels.pack(fill="both", expand=True, **pad)

        source_box = ttk.LabelFrame(panels, text="Image source")
        source_box.pack(side="left", fill="both", expand=True, padx=4)
        self.source_canvas = tk.Canvas(source_box, bg="#1e1e1e", highlightthickness=0)
        self.source_canvas.pack(fill="both", expand=True, padx=4, pady=4)

        preview_box = ttk.LabelFrame(panels, text="Preview")
        preview_box.pack(side="right", fill="both", expand=True, padx=4)
        self.preview_canvas = tk.Canvas(preview_box, bg="#1e1e1e", highlightthickness=0)
        self.preview_canvas.pack(fill="both", expand=True, padx=4, pady=4)

        # Progress bar
        prog_frame = ttk.Frame(self.root)
        prog_frame.pack(fill="x", **pad)
        self.progress_bar = ttk.Progressbar(prog_frame, variable=self.progress_var, maximum=100)
        self.progress_bar.pack(fill="x", side="top")
        ttk.Label(prog_frame, textvariable=self.status_var).pack(anchor="w", pady=(2, 0))

    # ---- Event handlers -----------------------------------------------------

    def _on_choose_image(self) -> None:
        if self.worker_thread is not None and self.worker_thread.is_alive():
            messagebox.showinfo("Génération en cours", "Annulez la génération avant de changer d'image.")
            return
        path = filedialog.askopenfilename(
            title="Choisir une image",
            filetypes=[("Images", "*.png *.jpg *.jpeg *.bmp"), ("Tous les fichiers", "*.*")],
        )
        if not path:
            return
        try:
            rgb, alpha = load_image(Path(path))
        except Exception as exc:
            messagebox.showerror("Erreur", f"Impossible de charger l'image :\n{exc}")
            return
        self.image_path = Path(path)
        self.source_rgb = rgb
        self.source_alpha = alpha
        h, w = rgb.shape[:2]
        info = f"{self.image_path.name}  ·  {w} × {h}"
        if alpha is not None:
            info += "  ·  alpha détecté"
            self.transparency_frame.pack(fill="x", padx=8, pady=4, after=self.image_info_label.master)
        else:
            info += "  ·  pas d'alpha"
            self.transparency_frame.pack_forget()
        self.image_info_label.config(text=info)
        self._refresh_source_preview()
        # Reset previous generation
        self.shapes_snapshot = []
        self.last_preview = None
        self.preview_canvas.delete("all")
        self.save_button.config(state="disabled")
        self.progress_var.set(0)
        self.status_var.set("Image chargée. Cliquez « Générer » pour démarrer.")

    def _on_transparency_mode_change(self) -> None:
        # Visual hint only — la valeur est lue au moment de générer.
        pass

    def _on_pick_bg_color(self) -> None:
        rgb, hex_color = colorchooser.askcolor(
            color=self.bg_color_var.get(), title="Choisir une couleur de fond",
        )
        if hex_color:
            self.bg_color_var.set(hex_color)
            self.bg_swatch.configure(bg=hex_color)
            self.transparency_mode.set("fill")

    def _on_preset_change(self, _event=None) -> None:
        preset = PRESETS.get(self.preset_var.get())
        if preset is None:
            return
        self.stop_at_var.set(preset.stop_at)
        self.random_samples_var.set(preset.random_samples)
        self.mutated_samples_var.set(preset.mutated_samples)

    def _on_generate(self) -> None:
        if self.source_rgb is None:
            messagebox.showinfo("Aucune image", "Choisissez d'abord une image.")
            return
        types = [t for t in SUPPORTED_SHAPE_TYPES if self.type_vars[t].get()]
        if not types:
            messagebox.showerror("Aucun type sélectionné", "Cochez au moins un type de forme.")
            return

        # Confirmation si une génération existe déjà
        if self.shapes_snapshot:
            ok = messagebox.askyesno(
                "Régénérer ?",
                "Cela va relancer la génération depuis zéro (peut prendre de 30 s à plusieurs minutes selon les réglages).\n\nContinuer ?",
            )
            if not ok:
                return

        # Build profile — clamp stop_at à la limite FH6
        stop_at = max(1, min(FH6_MAX_SHAPES, int(self.stop_at_var.get())))
        if stop_at != int(self.stop_at_var.get()):
            self.stop_at_var.set(stop_at)
        profile = Profile(
            shape_types=types,
            stop_at=stop_at,
            random_samples=int(self.random_samples_var.get()),
            mutated_samples=int(self.mutated_samples_var.get()),
            preview_every=max(10, stop_at // 60),
        )

        # Prepare image + alpha mask depending on transparency choice
        rgb = self.source_rgb
        alpha = self.source_alpha
        alpha_mask = None
        if alpha is not None and self.transparency_mode.get() == "fill":
            bg_rgb = self._hex_to_rgb(self.bg_color_var.get())
            rgb = compose_on_background(rgb, alpha, bg_rgb)
        elif alpha is not None and self.transparency_mode.get() == "keep":
            alpha_mask = alpha

        # Reset state for new generation
        self.shapes_snapshot = []
        self.last_preview = None
        use_gpu = bool(self.use_gpu_var.get() and self.gpu_available)
        try:
            self.engine = Engine(
                rgb,
                EngineConfig(profile=profile, use_gpu=use_gpu),
                alpha_mask=alpha_mask,
            )
        except RuntimeError as exc:
            messagebox.showerror("Erreur GPU", str(exc))
            return
        self.gen_start_time = time.time()

        self.generate_button.config(state="disabled")
        self.cancel_button.config(state="normal")
        self.save_button.config(state="disabled")
        self.progress_var.set(0)
        self.status_var.set("Lancement de la génération…")

        self.worker_thread = threading.Thread(target=self._run_worker, daemon=True)
        self.worker_thread.start()
        self.root.after(UI_REFRESH_MS, self._drain_queue)

    def _on_cancel(self) -> None:
        if self.engine is not None:
            self.engine.request_stop()
            self.status_var.set("Annulation demandée…")

    def _on_save(self) -> None:
        if not self.shapes_snapshot:
            return
        default_name = "vinyl.fh6.json"
        if self.image_path is not None:
            default_name = f"{self.image_path.stem}.fh6.json"
        path = filedialog.asksaveasfilename(
            title="Enregistrer le JSON",
            defaultextension=".json",
            initialfile=default_name,
            filetypes=[("JSON FH6", "*.json"), ("Tous les fichiers", "*.*")],
        )
        if not path:
            return
        h, w = self.source_rgb.shape[:2]
        if self.source_alpha is not None and self.transparency_mode.get() == "keep":
            bg = (0, 0, 0, 0)
        elif self.source_alpha is not None and self.transparency_mode.get() == "fill":
            r, g, b = self._hex_to_rgb(self.bg_color_var.get())
            bg = (r, g, b, 255)
        else:
            median = np.median(self.source_rgb.reshape(-1, 3), axis=0).astype(int)
            bg = (int(median[0]), int(median[1]), int(median[2]), 255)
        payload, lossy_count = to_fh6_payload(self.shapes_snapshot, (w, h), bg)
        if lossy_count > 0:
            proceed = messagebox.askokcancel(
                "Conversion avec perte",
                f"{lossy_count} forme(s) ont été approximées par des ellipses tournées "
                f"(carrés tournés et/ou triangles). Le rendu en jeu sera moins fidèle "
                f"que la preview pour ces formes.\n\nÉcrire quand même le fichier ?",
            )
            if not proceed:
                self.status_var.set("Sauvegarde annulée.")
                return
        try:
            save_fh6_json(payload, Path(path))
        except Exception as exc:
            messagebox.showerror("Erreur", f"Impossible d'écrire le fichier :\n{exc}")
            return
        suffix = f"  ({lossy_count} approximée(s))" if lossy_count > 0 else ""
        self.status_var.set(f"Sauvegardé : {path}{suffix}")

    def _on_close(self) -> None:
        if self.engine is not None:
            self.engine.request_stop()
        self.root.after(50, self.root.destroy)

    # ---- Worker thread ------------------------------------------------------

    def _run_worker(self) -> None:
        try:
            assert self.engine is not None
            for event in self.engine.run():
                self.event_queue.put(event)
        except Exception as exc:  # safety net
            from legacy.shapegen.engine import EngineEvent
            self.event_queue.put(EngineEvent(kind="error", message=str(exc)))

    def _drain_queue(self) -> None:
        from legacy.shapegen.engine import EngineEvent  # local for type ref
        drained_any = False
        try:
            while True:
                event: EngineEvent = self.event_queue.get_nowait()
                drained_any = True
                self._handle_event(event)
        except queue.Empty:
            pass
        if self.worker_thread is not None and self.worker_thread.is_alive():
            self.root.after(UI_REFRESH_MS, self._drain_queue)
        elif drained_any:
            # final drain pass already done; the engine is finished
            pass

    def _handle_event(self, event) -> None:
        if event.kind == "shape_committed":
            self._update_progress(event.shape_count, event.rms)
        elif event.kind == "preview":
            self.last_preview = event.canvas
            self.shapes_snapshot = list(self.engine.shapes) if self.engine else []
            self._refresh_preview_from_canvas(event.canvas)
            self._update_progress(event.shape_count, event.rms)
        elif event.kind == "done":
            self.shapes_snapshot = list(self.engine.shapes) if self.engine else []
            if event.canvas is not None:
                self.last_preview = event.canvas
                self._refresh_preview_from_canvas(event.canvas)
            else:
                self._refresh_preview_from_shapes()
            self.progress_var.set(100)
            elapsed = time.time() - self.gen_start_time
            msg = event.message or f"Terminé : {event.shape_count} formes en {elapsed:0.1f} s (RMS {event.rms:0.2f})."
            self.status_var.set(msg)
            self.generate_button.config(state="normal")
            self.cancel_button.config(state="disabled")
            if self.shapes_snapshot:
                self.save_button.config(state="normal")
        elif event.kind == "error":
            messagebox.showerror("Erreur du moteur", event.message)
            self.generate_button.config(state="normal")
            self.cancel_button.config(state="disabled")

    def _update_progress(self, count: int, rms: float) -> None:
        target = max(1, int(self.stop_at_var.get()))
        pct = min(100.0, 100.0 * count / target)
        self.progress_var.set(pct)
        elapsed = time.time() - self.gen_start_time
        self.status_var.set(f"Forme {count} / {target}  ·  RMS {rms:0.2f}  ·  {elapsed:0.1f} s")

    # ---- Preview rendering --------------------------------------------------

    def _refresh_source_preview(self) -> None:
        if self.source_rgb is None:
            return
        if self.source_alpha is not None:
            arr = np.dstack([self.source_rgb, self.source_alpha])
            img = Image.fromarray(arr, mode="RGBA")
        else:
            img = Image.fromarray(self.source_rgb, mode="RGB")
        self.source_photo = self._fit_to_canvas(img, self.source_canvas)
        self._draw_on_canvas(self.source_canvas, self.source_photo)

    def _refresh_preview_from_canvas(self, canvas: np.ndarray) -> None:
        if canvas is None:
            return
        if self.source_alpha is not None and self.transparency_mode.get() == "keep":
            # composite over a checkerboard so the user sees the cut-out
            from legacy.shapegen.render import _checkerboard
            h, w = canvas.shape[:2]
            board = _checkerboard(w, h)
            a = (self.source_alpha.astype(np.float32) / 255.0)[:, :, None]
            blended = (a * canvas.astype(np.float32) + (1.0 - a) * board.astype(np.float32)).astype(np.uint8)
            img = Image.fromarray(blended, mode="RGB")
        else:
            img = Image.fromarray(canvas, mode="RGB")
        self.preview_photo = self._fit_to_canvas(img, self.preview_canvas)
        self._draw_on_canvas(self.preview_canvas, self.preview_photo)

    def _refresh_preview_from_shapes(self) -> None:
        if self.source_rgb is None or not self.shapes_snapshot:
            return
        h, w = self.source_rgb.shape[:2]
        bg = (40, 40, 40) if self.source_alpha is not None and self.transparency_mode.get() == "keep" \
            else tuple(self.source_rgb.reshape(-1, 3).mean(axis=0).astype(int))
        canvas = render_shapes(self.shapes_snapshot, w, h, background=bg,
                               alpha_mask=self.source_alpha if self.transparency_mode.get() == "keep" else None)
        self._refresh_preview_from_canvas(canvas)

    def _fit_to_canvas(self, img: Image.Image, canvas_widget: tk.Canvas) -> ImageTk.PhotoImage:
        cw = max(64, canvas_widget.winfo_width() or PREVIEW_PANE_SIZE)
        ch = max(64, canvas_widget.winfo_height() or PREVIEW_PANE_SIZE)
        scaled = img.copy()
        scaled.thumbnail((cw - 8, ch - 8), Image.LANCZOS)
        return ImageTk.PhotoImage(scaled)

    def _draw_on_canvas(self, canvas_widget: tk.Canvas, photo: ImageTk.PhotoImage) -> None:
        canvas_widget.delete("all")
        cw = canvas_widget.winfo_width() or PREVIEW_PANE_SIZE
        ch = canvas_widget.winfo_height() or PREVIEW_PANE_SIZE
        canvas_widget.create_image(cw // 2, ch // 2, image=photo)

    # ---- Utils --------------------------------------------------------------

    @staticmethod
    def _hex_to_rgb(hex_color: str) -> tuple[int, int, int]:
        h = hex_color.lstrip("#")
        return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)

    # ---- Entry point --------------------------------------------------------

    def run(self) -> None:
        self.root.mainloop()
