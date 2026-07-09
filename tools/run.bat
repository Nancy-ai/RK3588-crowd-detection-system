@echo off
chcp 65001

REM 1. 启动 MediaMTX
start "MediaMTX" cmd /k "cd /d D:\mediaMTX && mediamtx.exe"

REM 2. 启动 FastAPI 后台
start "Backend" cmd /k "cd /d E:\workspace_pro\yolov8_project\backend && uvicorn main:app --host 0.0.0.0 --port 8000"

REM 3. 等待服务启动
timeout /t 3

REM 4. 自动打开网页
start http://127.0.0.1:8000

pause