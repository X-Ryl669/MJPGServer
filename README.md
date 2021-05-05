# Dual stream motion JPEG server

This repository contains an HTTP server that's streaming both a low resolution MJPG stream from your webcam, and allow simultaneously (or as close as simultaneously that's possible) capturing a full resolution JPEG picture from the camera.


This is very useful for monitoring a 3D printer (via Octoprint for example) without consuming too much bandwidth and still be able to capture high resolution pictures to make a timelapse of the 3D part being built (via Octolapse).

The server is very lightweight. It does not use CPU for image processing or rescaling and instead only rely on the webcam supporting MJPG encoding by itself. It should be suitable for embedded and low power system like a Raspberry Pi board.



## How does it compare to competitors ?

While there are numerous software that are able to stream a MJPG stream from a webcam, as far as I know, none of them allow simultaneously capture a stream in multiple resolution. Either the software will capture the stream in high resolution and then, decode, rescale and re-encode a low resolution stream, like [this](https://github.com/jacksonliam/mjpg-streamer/pull/149), or it's not possible at all.

The most common software is [mjpg-streamer](https://github.com/jacksonliam/mjpg-streamer).
With this software, the only way to support dual stream is to have 2 camera and running 2 instances of the server (one serving the first camera, while the other serving the second camera)


This repository allows to use a single camera for this purpose.


## Requirements

The software here is made for linux. It might work on other POSIX system with a V4L2 subsystem, but it's not tested. It won't work for Windows either since it's using Video4Linux 2 API to fetch the stream of the camera.

It requires a camera that's supporting MJPG format (most recent webcam do). It won't work for camera not supporting such format, since there is no JPEG encoder in the software.

It'll obviously work better if the camera supports multiple resolutions (in that case, the highest supported resolution is used for the picture and a VGA resolution stream is used for the low resolution stream).


## Technical internal working

The software runs a V4L2 thread that's fetching the low resolution stream as long as there are connected clients.

When the full resolution URL is requested, the low resolution stream is stopped, the camera resolution is changed to the high resolution and the streaming is started for getting one single frame that's answered to the request, then the low resolution stream is restarted. In effect, it appears like a small pause in the live stream (less than 3 or 4 frames on my computer) so it shouldn't be too painful.

## License

This code is dual licensed under GPLv3 license and a commercial license. 

The basic idea here is that the code should be free as long as you don't earn money from it (it's ok to use this code to make wonderful timelapses, but not to build a commercial service with it).

If you need to use this software commercially, feel free to send me an email, I'll answer promptly.

## Example usage

If you need some documentation to use it, feel free to read this [blog item](https://blog.cyril.by/en/3d-printers/easiest-way-to-make-timelapse-of-your-3d-prints)

Documentation for building and testing it is [here](https://blog.cyril.by/en/documentation/documentation-for-mjpgserver)


