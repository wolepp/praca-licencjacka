#!/bin/bash

fps=60/1
fmt=YUY2
whd=1920
hhd=1080

wnn=28
hnn=28

wbox=224
hbox=224

src="usbcam"

sink=hdmi
plane=30

export GST_DEBUG="sdxnet:6"

gst-launch-1.0 \
  xlnxvideosrc src-type="${src}" io-mode="dmabuf" ! \
  videoconvert ! \
  videocrop top=428 bottom=428 left=848 right=848 ! \
  videoscale ! \
  "video/x-raw, width=${wnn}, height=${hnn}, format=${fmt}" ! \
  sdxnet ! \
  videoscale ! \
  "video/x-raw, width=${wbox}, height=${hbox}" ! \
  videobox autocrop=true ! \
  "video/x-raw, width=${whd}, height=${hhd}" ! \
  xlnxvideosink sink-type=${sink} \
    plane-id=${plane} fullscreen-overlay=true


