@echo off
set GIO_USE_PROXY_RESOLVER=none
gst-launch-1.0 -e rtspsrc location=rtsp://192.168.1.18:8554/live latency=100 ! rtph264depay ! h264parse ! tee name=t t. ! queue ! avdec_h264 ! videoconvert ! d3d11videosink sync=false t. ! queue ! mp4mux ! filesink location=record_rtsp.mp4
pause