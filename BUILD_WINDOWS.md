# Building & Packaging ECG Viewer on Windows

## Prerequisites

You need three things: a C++ compiler, Qt5, and Python packages.
The easiest path is **conda** — it handles nearly everything.

---

## Step 1: Install Anaconda / Miniconda

If you don't already have it, download and install from:
- Miniconda (lightweight): https://docs.conda.io/en/latest/miniconda.html
- Anaconda (full): https://www.anaconda.com/download

---

## Step 2: Create the conda environment

Open **Anaconda Powershell Prompt** (search for it in the Start menu) and run:

```bash
conda create -n ecg python=3.10 qt=5 pybind11 numpy scipy pandas matplotlib pyqt cmake ninja -y
conda activate ecg
pip install pyinstaller
```

> **Important:** Use conda's `pyqt` package — do **not** `pip install PyQt5`. The pip version
> bundles its own Qt DLLs which conflict with conda's Qt and cause crashes when packaging.
> If you've previously installed PyQt5 via pip, remove it:
> ```bash
> pip uninstall PyQt5 PyQt5-sip PyQt5-Qt5 -y
> ```

---

## Step 3: Set up the C++ compiler

### Use Visual Studio Build Tools

Download "Build Tools for Visual Studio 2022" from https://visualstudio.microsoft.com/downloads/  
Select **"Desktop development with C++"** workload.

If you already have **Build Tools for Visual Studio 2022** installed (or full Visual Studio),
you can load the compiler into your current terminal instead.

**From Powershell:**
```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64
conda activate ecg
```

**Or** open the **"x64 Native Tools Command Prompt for VS 2022"** from the Start menu, then
activate conda manually (since conda isn't on PATH in that prompt):
```bash
C:\Users\YourName\anaconda3\condabin\conda activate ecg
```
Replace `YourName` with your Windows username, and `anaconda3` with `miniconda3` if using Miniconda.

> **Note:** If you don't have Visual Studio Build Tools and don't want to install them,
> https://visualstudio.microsoft.com/downloads/ — select the
> **"Desktop development with C++"** workload during install.

---

## Step 4: Build the C++ modules

Navigate to the project folder and run the build script:

```bash
cd path\to\project\ECGViewer
.\build.bat
```

If the build succeeds, you'll see `.pyd` files appear in the `ECGViewer/` directory.

If it fails with "No CMAKE_CXX_COMPILER could be found", go back to Step 3 — the compiler
isn't on your PATH.

---

## Step 5: Run the application

```bash
cd ..
python main.py
```

This launches the ECG Viewer GUI. If the C++ viewer modules built successfully, the Qt/C++
backend will be used automatically. If they're missing, it falls back to the Matplotlib backend.

---

## Step 6: Package into a standalone .exe (optional)

From the **project root** (not the ECGViewer subdirectory):

```bash
pyinstaller ecgviewer.spec --clean
```

The standalone application is created in `dist/ECGViewer/`. To distribute it, zip up the
entire `dist/ECGViewer/` folder — recipients just extract and run `ECGViewer.exe`.
No Python, conda, or any other install required on their machine.

> **Note:** The exe only works on **Windows 64-bit**. For other platforms, build separately
> on that platform.

---

## Troubleshooting

### "No CMAKE_CXX_COMPILER could be found"

The C++ compiler isn't on your PATH. See Step 3 — load Visual Studio's environment into your terminal.

### "Could not find Qt5" during CMake configure

Make sure your conda environment is activated (`conda activate ecg`). The CMakeLists.txt
auto-detects Qt from the conda environment via `CONDA_PREFIX`.

### "Could not find pybind11"

```bash
conda install pybind11 -y
```
Or: `pip install pybind11` (the pip package includes CMake config files).

### "This application failed to start because no Qt platform plugin could be initialized"

This means Qt can't find its platform plugins. Common causes:

- **Running from source:** Make sure conda environment is activated. Conda sets up the
  plugin paths automatically.
- **Running the packaged exe:** The `ecgviewer.spec` bundles conda's Qt plugins and sets
  `QT_PLUGIN_PATH` via a runtime hook. If you still get this error, rebuild with
  `pyinstaller ecgviewer.spec --clean`.
- **Mixed Qt installs:** If you have both `pip install PyQt5` and conda's `pyqt`, they
  conflict. Remove the pip version: `pip uninstall PyQt5 PyQt5-sip PyQt5-Qt5 -y`

### "DLL load failed while importing QtWidgets"

Same cause as above — conflicting Qt versions. Make sure you're using **only** conda's
`pyqt` package, not pip's `PyQt5`.

### PyInstaller: "Failed to execute script" or the exe closes instantly

Run the exe from a terminal to see the actual error:
```bash
cd dist\ECGViewer
.\ECGViewer.exe
```

### Build warnings about `checked_array_iterator` deprecation

These are harmless MSVC warnings from Qt internals. They don't affect the build and are
silenced in the CMakeLists.txt via `/D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING`.

---

## Quick Reference

| Task | Command |
|------|---------|
| Create environment | `conda create -n ecg python=3.10 qt=5 pybind11 numpy scipy pandas matplotlib pyqt cmake ninja -y` |
| Activate environment | `conda activate ecg` |
| Install compiler (conda) | `conda install vs2022_win-64 -c conda-forge -y` then deactivate/reactivate |
| Build C++ modules | `cd ECGViewer && .\build.bat` |
| Run from source | `python main.py` |
| Package into exe | `pyinstaller ecgviewer.spec --clean` |
| Clean rebuild | `cd ECGViewer && .\build.bat --clean-build` |
