@echo off
gst-launch-1.0 -v rtspsrc location=rtsp://192.168.1.18:8554/live latency=100 ! rtph264depay ! h264parse ! avdec_h264 ! videoconvert ! d3d11videosink sync=false
pause