@echo off
rem Webcam motion capture for the njin Animation Editor: a Python venv with
rem MediaPipe (Apache-2.0) in this folder, plus the two MediaPipe model files.
cd /d "%~dp0"
set PY=python
where %PY% >nul 2>nul || set PY=py -3
if not exist .venv\Scripts\python.exe (
  %PY% -m venv .venv
  if errorlevel 1 (
    echo Could not create the venv. Install Python 3.10 - 3.13 from python.org first.
    exit /b 1
  )
)
.venv\Scripts\python.exe -m pip install --upgrade pip
.venv\Scripts\python.exe -m pip install -r requirements.txt
if errorlevel 1 exit /b 1
set BASE=https://storage.googleapis.com/mediapipe-models
if not exist pose_landmarker_full.task curl -fSL -o pose_landmarker_full.task %BASE%/pose_landmarker/pose_landmarker_full/float16/latest/pose_landmarker_full.task
if not exist hand_landmarker.task curl -fSL -o hand_landmarker.task %BASE%/hand_landmarker/hand_landmarker/float16/latest/hand_landmarker.task
if not exist pose_landmarker_full.task exit /b 1
echo Mocap ready. In the editor: Mocap window, Start.
