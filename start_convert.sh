ffmpeg \
-f avfoundation \
-pixel_format uyvy422 \
-i "1:none" \
-vf "fps=30,setpts=N/(30*TB)" \
-c:v libx264 \
-preset ultrafast \
-tune zerolatency \
-pix_fmt yuv420p \
-g 30 \
-keyint_min 30 \
-sc_threshold 0 \
-hls_time 1 \
-hls_list_size 5 \
-hls_flags delete_segments \
live-stream/public/live/index.m3u8
