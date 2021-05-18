# Configuration keys for MJPGServer

You can set all configuration directly from a JSON file.
You'll then need to run `./mjpgsrv -j /path/to/config.json` to start the server


The available keys are the following:

| Key                   |  Expected value format              | Meaning                                                       | Default value |
|-----------------------|-------------------------------------|---------------------------------------------------------------|---------------|
| port                  | unsigned integer in range [1-65535] | The HTTP port to listen on                                    |  8080         |
| closeDeviceTimeoutSec | unsigned integer in seconds         | Delay before closing unused device, 0 to disable the function |  0            |
| device                | string                              | Path to the V4L2 camera device to open                        | /dev/video0   |
| daemonize             | boolean (true or false)             | If true, the server runs in background & drop priviledges(*)  | false         |
| logLevel              | -1, 0, 1, 2, 3                      | -1: debug, 0: info, 1: warning, 2: error, 3: silent           | 0             |
| lowResWidth           | unsigned integer in pixels          | The row size of the preview (low resolution) video stream     | 640           |
| lowResHeight          | unsigned integer in pixels          | The column size of the preview (low resolution) video stream  | 480           |
| highResWidth          | unsigned integer in pixels          | The row size of the full resolution picture                   | max detected  |
| highResHeight         | unsigned integer in pixels          | The column size of the full resolution picture                | max detected  |
| stabPicCount          | unsigned integer in frames          | The number of unstable frames to drop when switching res      | 0             | 
| securityToken         | string                              | If given, access to the stream will require this secret token | *empty*       |

## Interactions between the configuration keys

When using `daemonize` mode, the server open the TCP/HTTP port and then drop priviledges (to avoid running with root priviledges). However, if you 
have enabled `closeDeviceTimeoutSec`, you might get an error once the software tries to re-open a closed device since it does not have the required
priviledges anymore to do so.

So either disable `closeDeviceTimeoutSec` (by setting 0) or make sure the unpriviledged user has the right to reopen the video device.

`securityToken`, when used, requires appending a `?token=yoursecuritytokenhere` to the streams' URL. This is to prevent free-access to the streams if the 
server is directly accessible to the Internet. This is security-by-a-secret which is transmitted in clear on the HTTP's parameter.
This means that on HTTP protocol, it's free to snoop on the IP link. You should only rely on this single security if you have a reverse HTTPS proxy so the information is not transmitted in clear.
Using it on plain HTTP is better than nothing (at least it keeps private eyes out of the view) but don't forget it's still limited, security wise.

Support for `Authorization: Digest` is planned but currently, it's interfers with Octoprint's internal streaming service.

