FROM python:3.10-slim

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    python3-dev \
    python3-tk \
    pybind11-dev \
    tk-dev \
    tcl-dev \
    libgl1 \
    libglib2.0-0 \
    libx11-dev \
    libxcb1 \
    libxext6 \
    libxrender1 \
    libsm6 \
    libxkbcommon-x11-0 \
    libxcb-icccm4 \
    libxcb-image0 \
    libxcb-keysyms1 \
    libxcb-render-util0 \
    libxcb-xinerama0 \
    libxcb-randr0 \
    libxcb-shape0 \
    libxcb-sync1 \
    libxcb-xfixes0 \
    qtbase5-dev \
    qt5-qmake \
    && rm -rf /var/lib/apt/lists/*



WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt


COPY ECGViewer/ /app/ECGViewer 
COPY main.py /app
COPY ecg_analysis /app/ecg_analysis
COPY data/ /app/data

WORKDIR /app/ECGViewer/
RUN bash build.bash --clean
RUN bash build.bash
WORKDIR /app
CMD ["python", "main.py"]

