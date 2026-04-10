FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PYTHONDONTWRITEBYTECODE=1
ENV PYTHONUNBUFFERED=1
ENV PIP_DISABLE_PIP_VERSION_CHECK=1

# System + build deps
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3.10 \
    python3.10-dev \
    python3-pip \
    python3-venv \
    build-essential \
    cmake \
    bash \
    git \
    pybind11-dev \
    qtbase5-dev \
    qtbase5-dev-tools \
    qt5-qmake \
    python3-tk \
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
  && rm -rf /var/lib/apt/lists/*

RUN ln -sf /usr/bin/python3.10 /usr/bin/python && \
    ln -sf /usr/bin/pip3 /usr/bin/pip

WORKDIR /deps

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

WORKDIR /work
