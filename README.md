  ispstats
=======

For experimenting with the OMAP3 isp histogram functionality via the v4l2 
interface. Testing with a driver for an Aptina mt9p031 sensor.

Run with no arguments, the default is to do 4 image reads to get a histogram 
for a 400x400 region in the center of a 2560x1920 bayer input and dump the 
results. 

You can specify the number of histogram bins (32,64,128 or 256), the number
of frames to read and the gain to be applied to each of the four color 
components.

The results are dumped to stdout. This is a WIP. The results are consistent
though not yet predictable for me. Needs more study.

Right now isp_user.h is taken from arch/arm/plat-omap/include/plat/ and dropped
right into this project.

The newer kernel omap isp code has this header moved to include/linux/omap3isp 
for more convenient user land access.


There is a little shell script to run the program in a loop. 

ispstats assumes you have another application already streaming the camera,
it does not start or stop streaming on its own.



